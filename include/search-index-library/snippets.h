// SPDX-FileCopyrightText: 2023–2025 Andy Curtis <contactandyc@gmail.com>
// SPDX-FileCopyrightText: 2024–2025 Knode.ai — technical questions: contact Andy (above)
// SPDX-License-Identifier: Apache-2.0

#ifndef _snippets_H
#define _snippets_H

#include "a-memory-library/aml_buffer.h"
#include <stdint.h>

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
    size_t index;                  // Index of the snippet in the list
    double density;                // Density score (score divided by normalization)
    double first_instance_weight;  // Sum of weights for the first occurrence of each term
    size_t match_count;            // Total number of matches (first and second occurrences)
    size_t distinct_match_count;   // Count of distinct (first-occurrence) matches
    uint64_t mask;                 // Bitmask of terms seen (for the winning query)
    double score;                  // Combined score computed as (density + first_instance_weight) * distinct_match_count
    int query_index;               // The query index for which this snippet is best
    bool next_in_cluster; 		   // Indicates if the following snippet belongs to the same cluster
} snippet_t;

// sorts the snippet positions by position and then merges where the position is equal updating the query mask
size_t snippet_position_sort(snippet_position_t *positions, size_t num_positions);

void snippets_sort(snippet_t *snippets, size_t num_snippets);

size_t snippets_top(snippet_t *snippets, size_t num_snippets);

void snippets_create(aml_buffer_t *bh, snippet_position_t *positions, size_t num_positions,
				     size_t max_snippet);

void snippets_print(snippet_t *snippets, size_t num_snippets);


#endif
