# SPDX-FileCopyrightText: 2023-2025 Andy Curtis <contactandyc@gmail.com>
# SPDX-FileCopyrightText: 2024-2025 Knode.ai
# SPDX-License-Identifier: Apache-2.0
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>

#define BONUS_FACTOR 0.15  // Second occurrence gets BONUS_FACTOR of full weight
#define MAX_QUERIES 32

static inline int macro_highest_bit_index(uint32_t x) {
	if (x == 0)
		return -1;  // Or handle the zero-case as needed

#ifdef __GNUC__
    // __builtin_clz returns the number of leading zeros; for a 32-bit number,
    // the highest set bit index is 31 - __builtin_clz(x)
    return 31 - __builtin_clz(x);
#else
    // Fallback loop-based method
    int index = 0;
    while (x >>= 1) {
        index++;
    }
    return index;
#endif
}

// ----------------------------------------------------------------------
// Data structures
// ----------------------------------------------------------------------

// Structure representing a term occurrence (assumed to be sorted by position)
typedef struct {
    size_t position;      // Token position in the document
    double weight;        // Full weight for this occurrence
    uint32_t term_index;  // Index (0–63) identifying which top term it is
    uint32_t query_mask;  // Bit mask: each bit represents one query (up to 32)
} snippet_position_t;

// Structure representing a snippet (a candidate segment)
typedef struct {
    size_t start;                  // Starting token index of the snippet
    size_t end;                    // Ending token index of the snippet
    double density;                // Density score (score divided by normalization)
    double first_instance_weight;  // Sum of weights for the first occurrence of each term
    size_t match_count;            // Total number of matches (first and second occurrences)
    size_t distinct_match_count;   // Count of distinct (first-occurrence) matches
    uint64_t mask;                 // Bitmask of terms seen (for the winning query)
    double score;                  // Combined score computed as (density + first_instance_weight) * distinct_match_count
    int query_index;               // The query index for which this snippet is best
} snippet_t;

// Node structure for linked list of snippet segments.
// The boolean next_in_cluster indicates if the following node belongs to the same cluster.
typedef struct snippet_node {
    snippet_t snippet;
    bool next_in_cluster;
    struct snippet_node *next;
} snippet_node_t;

// ----------------------------------------------------------------------
// Function: find_best_snippet_for_range_multi
//
//   Scans term occurrences within [range_start, range_end] using a sliding
//   window (up to max_snippet tokens) to determine the window with the highest
//   combined score. It supports multiple queries by maintaining per-query
//   accumulators. The winning snippet is stored in *best.
// ----------------------------------------------------------------------
bool find_best_snippet_for_range_multi(snippet_position_t *positions, size_t num_positions, uint32_t query_mask,
                                         size_t range_start, size_t range_end,
                                         size_t max_snippet,
                                         snippet_t *best) {
    bool found = false;
    // Initialize best snippet with zero score and match count.
    best->match_count = 0;
    best->score = 0.0;
    best->first_instance_weight = 0.0;
    best->distinct_match_count = 0;

	int highest_bit = macro_highest_bit_index(query_mask) + 1;

    snippet_position_t *p = positions;
    snippet_position_t *endPtr = positions + num_positions;

    while (p < endPtr) {
        if (p->position < range_start) {
            p++;
            continue;
        }
        if (p->position > range_end)
            break;

        // Per-query accumulators.
        double query_score[MAX_QUERIES] = {0};          // Accumulated score (first occurrence and bonus for second)
        double query_first_weight[MAX_QUERIES] = {0};     // Sum of weights for first occurrences
        size_t query_match_count[MAX_QUERIES] = {0};      // Total occurrence count (first and second)
        size_t query_distinct_count[MAX_QUERIES] = {0};   // Count of distinct matches (first occurrences)
        uint64_t first_masks[MAX_QUERIES] = {0};          // Bitmask to track first occurrences per query
        uint64_t second_masks[MAX_QUERIES] = {0};         // Bitmask to track second occurrences per query

        snippet_position_t *curr = p;
        while (curr < endPtr && curr->position <= range_end) {
            size_t snippet_length = curr->position - p->position + 1;
            if (snippet_length > max_snippet)
                break;  // Exceeds the allowed snippet length.

            uint64_t bit = (uint64_t)1 << curr->term_index;
            // Process each query for which this term is relevant.
            for (int q = 0; q < highest_bit; q++) {
                if (curr->query_mask & (1U << q)) {
                    if (!(first_masks[q] & bit)) {
                        // First occurrence for query q.
                        first_masks[q] |= bit;
                        query_score[q] += curr->weight;
                        query_first_weight[q] += curr->weight;
                        query_distinct_count[q]++;
                        query_match_count[q]++;
                    } else if (!(second_masks[q] & bit)) {
                        // Second occurrence for query q.
                        second_masks[q] |= bit;
                        query_score[q] += curr->weight * BONUS_FACTOR;
                        query_match_count[q]++;
                    }
                    // Further occurrences are ignored.
                }
            }

            // Normalize using the snippet length.
            double norm = log((double)snippet_length + 1.0);

            // Evaluate each query's score.
            for (int q = 0; q < highest_bit; q++) {
                if (query_match_count[q] > 0) {
                    double density = query_score[q] / norm;
                    double combined = (density + query_first_weight[q]) * query_distinct_count[q];
                    // Prefer higher match_count; if equal, use the combined score.
                    if (query_match_count[q] > best->match_count ||
                       (query_match_count[q] == best->match_count && combined > best->score)) {
                        best->match_count = query_match_count[q];
                        best->score = combined;
                        best->density = density;
                        best->first_instance_weight = query_first_weight[q];
                        best->distinct_match_count = query_distinct_count[q];
                        best->start = p->position;
                        best->end = curr->position;
                        best->mask = first_masks[q];  // Save mask for the winning query.
                        best->query_index = q;         // Record the query index.
                        found = true;
                    }
                }
            }
            curr++;
        }
        p++;
    }
    return found;
}

// ----------------------------------------------------------------------
// Function: segment_document
//
//   Recursively segments the region [region_start, region_end] by selecting
//   the best candidate snippet (using the multi-query scoring function) and
//   then recursively processing the left and right parts (if any).
//   If the entire region's token span is small enough (<= max_snippet),
//   it computes per-query metrics over that region and selects the best query.
// ----------------------------------------------------------------------
snippet_node_t *segment_document(snippet_position_t *positions, size_t num_positions,
								  uint32_t query_mask, size_t region_start, size_t region_end,
                                  size_t max_snippet) {
    // Base case: if the region's token span is small enough, return a single snippet.
    if ((region_end - region_start + 1) <= max_snippet) {
        // Per-query accumulators.
        double query_first_weight[MAX_QUERIES] = {0};
        size_t query_distinct_count[MAX_QUERIES] = {0};
        size_t query_match_count[MAX_QUERIES] = {0};
        uint64_t first_masks[MAX_QUERIES] = {0};

        double region_total_weight = 0.0;
		int highest_bit = macro_highest_bit_index(query_mask) + 1;

        // Process positions in the region.
        for (size_t i = 0; i < num_positions; i++) {
            if (positions[i].position >= region_start && positions[i].position <= region_end) {
                region_total_weight += positions[i].weight;
                uint64_t bit = (uint64_t)1 << positions[i].term_index;
                // For each query that this term belongs to:
                for (int q = 0; q < highest_bit; q++) {
                    if (positions[i].query_mask & (1U << q)) {
                        query_match_count[q]++;
                        if (!(first_masks[q] & bit)) {
                            first_masks[q] |= bit;
                            query_first_weight[q] += positions[i].weight;
                            query_distinct_count[q]++;
                        }
                    }
                }
            }
        }

        // Compute density for the region.
        double density = (region_end - region_start + 1 > 0) ?
                         region_total_weight / (region_end - region_start + 1) : 0;

        // Determine the best query for this region.
        double best_score = 0.0;
        int best_query = -1;
        for (int q = 0; q < highest_bit; q++) {
            if (query_match_count[q] > 0) {
                double combined = (density + query_first_weight[q]) * query_distinct_count[q];
                if (combined > best_score) {
                    best_score = combined;
                    best_query = q;
                }
            }
        }

        // Create the snippet node using the best query's metrics.
        snippet_node_t *node = malloc(sizeof(snippet_node_t));
        node->snippet.start = region_start;
        node->snippet.end = region_end;
        node->snippet.match_count = (best_query >= 0) ? query_match_count[best_query] : 0;
        node->snippet.distinct_match_count = (best_query >= 0) ? query_distinct_count[best_query] : 0;
        node->snippet.first_instance_weight = (best_query >= 0) ? query_first_weight[best_query] : 0;
        node->snippet.density = density;
        node->snippet.mask = (best_query >= 0) ? first_masks[best_query] : 0;
        node->snippet.score = best_score;
        node->snippet.query_index = best_query;
        node->next = NULL;
        node->next_in_cluster = false;
        return node;
    }

    snippet_t best;
    // Use the multi-query snippet finder here.
    if (!find_best_snippet_for_range_multi(positions, num_positions, query_mask,
                                             region_start, region_end,
                                             max_snippet, &best)) {
        return NULL; // No candidate found in this region.
    }

    snippet_node_t *node = malloc(sizeof(snippet_node_t));
    node->snippet = best;
    node->next = NULL;

    // Recursively segment the left portion.
    snippet_node_t *left_list = NULL;
    if (best.start > region_start) {
        left_list = segment_document(positions, num_positions, query_mask,
                                     region_start, best.start - 1, max_snippet);
    }

    // Recursively segment the right portion.
    snippet_node_t *right_list = NULL;
    if (best.end < region_end) {
        right_list = segment_document(positions, num_positions, query_mask,
                                      best.end + 1, region_end, max_snippet);
    }

    // Merge left_list, current node, and right_list.
    snippet_node_t *head = NULL;
    if (left_list) {
        head = left_list;
        snippet_node_t *p = left_list;
        while (p->next)
            p = p->next;
        p->next = node;
    } else {
        head = node;
    }
    node->next = right_list;
    return head;
}

// ----------------------------------------------------------------------
// Function: process_all_clusters
//
//   Pre-segments the sorted term positions array into clusters based on the
//   gap (greater than max_snippet) between consecutive term positions.
//   For each cluster, it calls segment_document to get a linked list of
//   snippet segments and then links all clusters into one global list.
//   Additionally, each node's next_in_cluster flag is set accordingly.
// ----------------------------------------------------------------------
snippet_node_t *process_all_clusters(snippet_position_t *positions, size_t num_positions,
                                       size_t max_snippet) {
    snippet_node_t *global_head = NULL;
    snippet_node_t *global_tail = NULL;

    snippet_position_t *p = positions;
    snippet_position_t *ep = positions + num_positions;

    while (p < ep) {
        // Set the start of the current cluster.
        snippet_position_t *cluster_start = p;

        // Advance p while the gap between successive positions is less than max_snippet.
		uint32_t query_mask = p->query_mask;
        p++;
        while (p < ep && (p->position - (p - 1)->position) < max_snippet) {
           	query_mask |= p->query_mask;
            p++;
        }

        // [cluster_start, p) forms a cluster.
        size_t cluster_length = p - cluster_start;
        if (cluster_length == 0)
            continue; // Safety check.

        size_t region_start = cluster_start->position;
        size_t region_end = (p - 1)->position;  // Last element of the cluster.

        // Process this cluster.
        snippet_node_t *cluster_list = segment_document(cluster_start, cluster_length, query_mask,
                                                         region_start, region_end,
                                                         max_snippet);
        if (cluster_list) {
            // Mark nodes in this cluster.
            snippet_node_t *node = cluster_list;
            while (node) {
                node->next_in_cluster = (node->next != NULL);
                node = node->next;
            }

            // Append this cluster's list to the global list.
            if (!global_head) {
                global_head = global_tail = cluster_list;
            } else {
                global_tail->next = cluster_list;
            }
            // Advance global_tail to the end of the newly appended cluster list.
            while (global_tail->next)
                global_tail = global_tail->next;
            // Mark the boundary between clusters.
            global_tail->next_in_cluster = false;
        }
        // Continue with the next cluster.
    }
    return global_head;
}

// ----------------------------------------------------------------------
// Utility: print_segments
//
//   Walks through the linked list of snippet nodes and prints each snippet,
//   indicating whether the following snippet is in the same cluster.
// ----------------------------------------------------------------------
void print_segments(snippet_node_t *head) {
    snippet_node_t *p = head;
    while (p) {
        printf("Snippet: tokens %zu-%zu, match_count = %zu/%zu, mask: %llu, density = %.3lf, first_instance_weight = %.3lf, score = %.3lf, query_index = %d\n",
               p->snippet.start, p->snippet.end, p->snippet.distinct_match_count,
               p->snippet.match_count, p->snippet.mask, p->snippet.density,
               p->snippet.first_instance_weight, p->snippet.score,
               p->snippet.query_index);
        if (p->next_in_cluster)
            printf("  [Next snippet is in the same cluster]\n");
        else
            printf("  [Cluster boundary]\n");
        p = p->next;
    }
}

// ----------------------------------------------------------------------
// Example usage
// ----------------------------------------------------------------------
int main() {
    // Example term positions (sorted by position) with varying query masks.
    // In this example:
    // - Query 0: bit 0 (1 << 0)
    // - Query 1: bit 1 (1 << 1)
    // - Query 2: bit 2 (1 << 2)
    snippet_position_t positions[] = {
        { 1, 2.0, 0, (1 << 0) },                     // token 1, term 0, query 0 only.
        { 3, 1.5, 1, (1 << 0) | (1 << 1) },            // token 3, term 1, queries 0 and 1.
        { 5, 1.0, 2, (1 << 1) },                       // token 5, term 2, query 1 only.
        { 7, 2.5, 0, (1 << 1) },                       // token 7, term 0, query 1 only.
        {10, 3.0, 3, (1 << 2) },                       // token 10, term 3, query 2 only.
        {12, 2.0, 1, (1 << 2) },                       // token 12, term 1, query 2 only.
        {14, 1.0, 0, (1 << 0) | (1 << 2) },            // token 14, term 0, queries 0 and 2.
        {18, 2.0, 4, (1 << 0) },                       // token 18, term 4, query 0 only.
        {20, 1.5, 2, (1 << 1) },                       // token 20, term 2, query 1 only.
        // End first cluster.
        {60, 1.0, 3, (1 << 2) },                       // token 60, term 3, query 2 only.
        {61, 1.0, 3, (1 << 2) },                       // token 61, term 3, query 2 only.
        {62, 1.0, 3, (1 << 2) }                        // token 62, term 3, query 2 only.
    };
    size_t num_positions = sizeof(positions) / sizeof(positions[0]);

    // Define snippet constraint: maximum snippet length in tokens.
    size_t max_snippet = 20;

    // Process all clusters and retrieve a global linked list of segments.
    snippet_node_t *global_segments = process_all_clusters(positions, num_positions, max_snippet);
    // Print the segments along with cluster boundary information.
    print_segments(global_segments);

    // (Memory freeing is omitted for brevity.)
    return 0;
}