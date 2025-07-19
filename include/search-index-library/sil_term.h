// SPDX-FileCopyrightText: 2023-2025 Andy Curtis <contactandyc@gmail.com>
// SPDX-FileCopyrightText: 2024-2025 Knode.ai
// SPDX-License-Identifier: Apache-2.0
#ifndef _sil_term_H
#define _sil_term_H

/*
 * search_term.h
 *
 * This header defines the `sil_term_t` structure and its associated API, which
 * provides an efficient interface for iterating through and retrieving information
 * about indexed terms in a search system. The terms are represented as sorted lists
 * of IDs, each associated with optional positions and values.
 *
 * Features:
 * 1. **Term Representation**:
 *    - Each `sil_term_t` represents a term linked to a sorted list of document IDs.
 *    - For each ID, an optional value (e.g., score, weight, or flags) and positions
 *      (e.g., word positions within a document) can be associated.
 *    - The maximum number of positions per term is tracked by `max_term_size`.
 *
 * 2. **Iteration**:
 *    - Use the `advance` callback to iterate through the IDs for a term sequentially.
 *      - Each call to `advance` updates `id`, `value`, and resets position-related
 *        fields to reflect the next ID in the list.
 *      - Returns `true` if there are more IDs to iterate, `false` otherwise.
 *    - Use the `advance_to` callback to skip to the first ID greater than or equal
 *      to a given target ID, improving efficiency for sparse queries.
 *      - Returns `true` if such an ID exists, `false` otherwise.
 *
 * 3. **Position Decoding**:
 *    - To retrieve positional data for the current ID, call `sil_term_decode_positions()`.
 *      - Populates the `term_positions` and `term_positions_end` fields with pointers
 *        to the positions.
 *      - The number of positions can be calculated as:
 *        `(term_positions_end - term_positions)`.
 *
 * Example Usage:
 * ```c
 * sil_term_t *term = sil_search_image_term(search_image, "example"); // from sil_search_image.h
 * while (term->advance(term)) {
 *     printf("ID: %u, Value: %u, Positions: ", term->id, term->value);
 *     sil_term_decode_positions(term);
 *     for (uint32_t *p = term->term_positions; p < term->term_positions_end; ++p) {
 *         printf("%u ", *p);
 *     }
 *     printf("\n");
 * }
 * ```
 *
 * Notes:
 * - `sil_term_t` is designed to be lightweight and flexible, making it suitable for
 *   use in memory-efficient search systems.
 * - Ensure proper memory allocation and cleanup when working with position arrays or
 *   related resources.
 *
 * Dependencies:
 * - The implementation of this header (`sil_term_impl.h`) must be included for the
 *   callbacks (`advance`, `advance_to`) and decoding functionality to work.
 *
 */

#include <inttypes.h>
#include <stdint.h>
#include <stdbool.h>
#include "a-tokenizer-library/atl_token.h"
#include "a-tokenizer-library/atl_cursor.h"

struct sil_term_s;
typedef struct sil_term_s sil_term_t;

typedef struct {
    uint32_t max_positions;
    uint32_t document_frequency;
} sil_term_header_t;

typedef struct {
    uint32_t document_length;  // term count for BM25
    uint32_t num_embeddings;
    uint64_t content_offset : 36;
    uint64_t embeddings_offset : 28;
} sil_global_header_t;

struct sil_term_s {
    atl_cursor_t c;

    uint32_t value;

    // term positions (must be filled by sil_term_decode_positions after advance or advance_to call)
    uint32_t *term_positions;
    uint32_t *term_positions_end;

    // set at the term level, the maximum number of term positions
    uint32_t max_term_size;
    // the number of documents matching the term
    uint32_t document_frequency;
};

static inline void sil_term_decode_positions(sil_term_t *t);

void sil_term_dump(sil_term_t *t);

#include "impl/sil_term_impl.h"

#endif