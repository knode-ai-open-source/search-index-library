// SPDX-FileCopyrightText: 2023–2025 Andy Curtis <contactandyc@gmail.com>
// SPDX-FileCopyrightText: 2024–2025 Knode.ai — technical questions: contact Andy (above)
// SPDX-License-Identifier: Apache-2.0

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>


#include "a-memory-library/aml_buffer.h"
#include "the-macro-library/macro_to.h"
#include "the-macro-library/macro_sort.h"
#include "search-index-library/snippets.h"

#define BONUS_FACTOR 0.15  // Second occurrence gets BONUS_FACTOR of full weight
#define MAX_QUERIES 32
#define SUMMARY_SNIPPET 250.0

static double position_ratio(size_t start) {
    // If we're already past SUMMARY_SNIPPET, just use max_snippet
    if (start >= SUMMARY_SNIPPET) {
        return 1.0;
    }
    double a = (double) start;
    double ratio = (SUMMARY_SNIPPET - a) / SUMMARY_SNIPPET;  // from 1.0 down to ~0
	return 1.0 + ratio;
}


static size_t adjusted_max_snippet(size_t start, size_t max_snippet) {
    // If we're already past SUMMARY_SNIPPET, just use max_snippet
    if (start >= SUMMARY_SNIPPET) {
        return max_snippet;
    }

	double snippet_factor = position_ratio(start);
	double b = (double) max_snippet;
	double snippet_size_d = snippet_factor * b;
    return (size_t) round(snippet_size_d);
}

static bool find_best_snippet_for_range_multi(snippet_position_t *positions, size_t num_positions, uint32_t query_mask,
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
            size_t adj_max_snippet = adjusted_max_snippet(p->position, max_snippet);
            if (snippet_length > adj_max_snippet)
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
						best->next_in_cluster = true;
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

static void segment_document(aml_buffer_t *bh,
			 				 snippet_position_t *positions, size_t num_positions,
							 uint32_t query_mask, size_t region_start, size_t region_end,
							 size_t max_snippet) {
    // Base case: if the region's token span is small enough, return a single snippet.
    if ((region_end - region_start + 1) <= adjusted_max_snippet(region_start, max_snippet)) {
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


		snippet_t snippet;
		snippet.start = region_start;
		snippet.end = region_end;
		snippet.match_count = (best_query >= 0) ? query_match_count[best_query] : 0;
		snippet.distinct_match_count = (best_query >= 0) ? query_distinct_count[best_query] : 0;
		snippet.first_instance_weight = (best_query >= 0) ? query_first_weight[best_query] : 0;
		snippet.density = density;
		snippet.mask = (best_query >= 0) ? first_masks[best_query] : 0;
		snippet.score = best_score;
		snippet.query_index = best_query;
		snippet.next_in_cluster = true;
		aml_buffer_append(bh, &snippet, sizeof(snippet));
		return;
    }

    snippet_t best;
    // Use the multi-query snippet finder here.
    if (!find_best_snippet_for_range_multi(positions, num_positions, query_mask,
                                             region_start, region_end,
                                             max_snippet, &best)) {
        return; // No candidate found in this region.
    }

    if (best.start > region_start)
		segment_document(bh, positions, num_positions, query_mask, region_start, best.start - 1, max_snippet);

	aml_buffer_append(bh, &best, sizeof(best));

	if (best.end < region_end)
		segment_document(bh, positions, num_positions, query_mask, best.end + 1, region_end, max_snippet);
}

void snippets_create(aml_buffer_t *bh, snippet_position_t *positions, size_t num_positions,
				     size_t max_snippet) {
    snippet_position_t *p = positions;
    snippet_position_t *ep = positions + num_positions;

    while (p < ep) {
        // Set the start of the current cluster.
        snippet_position_t *cluster_start = p;

        // Advance p while the gap between successive positions is less than max_snippet.
		uint32_t query_mask = p->query_mask;
        p++;
        while (p < ep && (p->position - (p - 1)->position) < adjusted_max_snippet((p - 1)->position, max_snippet)) {
           	query_mask |= p->query_mask;
            p++;
        }

        // [cluster_start, p) forms a cluster.
        size_t cluster_length = p - cluster_start;
        if (cluster_length == 0)
            continue; // Safety check.

        size_t region_start = cluster_start->position;
        size_t region_end = (p - 1)->position;  // Last element of the cluster.

		size_t current_length = aml_buffer_length(bh);

		segment_document(bh, cluster_start, cluster_length, query_mask, region_start, region_end, max_snippet);
	    size_t new_length = aml_buffer_length(bh);
	    if(new_length > current_length) {
		    snippet_t *last = (snippet_t *)aml_buffer_end(bh);
		    last--;
		    last->next_in_cluster = false;
	    }
	}
	snippet_t *snippets = (snippet_t *)aml_buffer_data(bh);
	size_t num_snippets = aml_buffer_length(bh) / sizeof(snippet_t);
	for(size_t i = 0; i < num_snippets; i++) {
		snippets[i].score *= position_ratio(snippets[i].start);
		snippets[i].index = i;
	}
}

void snippets_print(snippet_t *snippets, size_t num_snippets) {
	for (size_t i = 0; i < num_snippets; i++) {
		printf("Snippet[%zu-%zu]: tokens %zu-%zu, match_count = %zu/%zu, mask: %llu, density = %.3lf, first_instance_weight = %.3lf, score = %.3lf, query_index = %d, boundary: %s\n",
			   i, snippets[i].index, snippets[i].start, snippets[i].end, snippets[i].distinct_match_count,
			   snippets[i].match_count, snippets[i].mask, snippets[i].density,
			   snippets[i].first_instance_weight, snippets[i].score,
			   snippets[i].query_index, snippets[i].next_in_cluster ? "false" : "true");
	}
}

static inline
bool compare_snippet_position(const snippet_position_t *a, const snippet_position_t *b) {
	if(a->position != b->position)
		return a->position < b->position;
	return a->term_index < b->term_index;
}

static inline
macro_sort(sort_snippet_positions, snippet_position_t, compare_snippet_position);

size_t snippet_position_sort(snippet_position_t *positions, size_t num_positions) {
	sort_snippet_positions(positions, num_positions);
	snippet_position_t *p = positions;
	snippet_position_t *ep = positions + num_positions;
	snippet_position_t *wp = positions;
	while(p < ep) {
       // merge positions with the same position and term_index, updating the query mask
       snippet_position_t *q = p + 1;
       while(q < ep && q->position == p->position && q->term_index == p->term_index) {
		   p->query_mask |= q->query_mask;
		   q++;
	   }
	   *wp = *p;
	   wp++;
	   p = q;
	}
	return wp - positions;
}

static inline
bool compare_snippet(const snippet_t *a, const snippet_t *b) {
	if(a->score != b->score)
		return a->score > b->score;
	if(a->distinct_match_count != b->distinct_match_count)
		return a->distinct_match_count > b->distinct_match_count;
	if(a->match_count != b->match_count)
		return a->match_count > b->match_count;
	if(a->density != b->density)
		return a->density > b->density;
	if(a->first_instance_weight != b->first_instance_weight)
		return a->first_instance_weight > b->first_instance_weight;
	return a->start < b->start;
}

static inline
macro_sort(sort_snippets, snippet_t, compare_snippet);

void snippets_sort(snippet_t *snippets, size_t num_snippets) {
	sort_snippets(snippets, num_snippets);
}

static inline
bool compare_snippet_index(const snippet_t *a, const snippet_t *b) {
	return a->index < b->index;
}

static inline
macro_sort(sort_snippet_by_index, snippet_t, compare_snippet_index);


size_t _snippets_top(snippet_t *snippets, size_t num_snippets) {
	if(!num_snippets)
		return 0;

	sort_snippets(snippets, num_snippets);
	uint64_t mask = snippets[0].mask;
	if(!mask)
		return 0;
	for( size_t i=1; i<num_snippets; i++ ) {
		// compare the mask of the given snippet to the 0th snippet, reducing weight for each bit that is set
		if(snippets[i].query_index != snippets[0].query_index)
			continue;
		uint64_t common = snippets[i].mask & mask;
		if(common) {
			// rescore the snippet, counting the number of bits set in common
			int num_bits = macro_bit_count64(common);
			int current_bits = macro_bit_count64(snippets[i].mask);
			snippets[i].score *= (1.0 - (double)num_bits / (double)current_bits);
			// unset bits that are in mask on the current snippet
			snippets[i].mask &= ~mask;
		}
	}
	if(mask && num_snippets > 1)
		return _snippets_top(snippets+1, num_snippets-1) + 1;
	return 1;
}

size_t snippets_top(snippet_t *snippets, size_t num_snippets) {
	num_snippets = _snippets_top(snippets, num_snippets);
	sort_snippet_by_index(snippets, num_snippets);
	return num_snippets;
}
