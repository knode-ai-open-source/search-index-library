// SPDX-FileCopyrightText: 2023-2025 Andy Curtis <contactandyc@gmail.com>
// SPDX-FileCopyrightText: 2024-2025 Knode.ai
// SPDX-License-Identifier: Apache-2.0
#ifndef _sil_document_image_H
#define _sil_document_image_H

#include "a-memory-library/aml_pool.h"
#include "a-memory-library/aml_buffer.h"
#include "search-index-library/sil_term.h"

struct sil_document_image_s;
typedef struct sil_document_image_s sil_document_image_t;

typedef struct {
    uint32_t document_length_for_bm25;
    uint32_t term_length;
    uint32_t data_length;
    uint32_t content_length;
    uint32_t num_embeddings;
    uint32_t num_terms;
} sil_document_header_t;


struct sil_document_image_s {
    const char *document;
    uint32_t length;

    sil_document_header_t header;
    char *terms;
    char *data;
    char *content;
    int8_t *embeddings;
};

typedef void (*update_terms_cb)(aml_pool_t *pool, char **terms, uint32_t num_terms, void *arg);

/* Initialize a search document image from a binary document built from sil_document_builder. */
void sil_document_image_init(sil_document_image_t *image, const char *document, uint32_t length);

/* Construct a term set from a query. */
sil_term_set_t *sil_construct_term_set(aml_pool_t *pool, const char *query, update_terms_cb cb, void *arg);

/* Copy a term set. */
sil_term_set_t *sil_term_set_copy(aml_pool_t *pool, const sil_term_set_t *original);

/* Used to combine global frequencies */
void sil_document_image_add_set_freq(sil_term_set_t *dest, const sil_term_set_t *src);

/* Match a term in a search document image and update frequency for matching terms. */
void sil_document_image_update_frequency(sil_document_image_t *img, sil_term_set_t *set);

/* Prepare a term set for matching in a search document image. */
void sil_document_image_match_prepare_for_set(aml_pool_t *pool, sil_term_set_t *set);

/* Match a set of terms in a search document image. */
size_t sil_document_image_match_set(sil_document_image_t *img, sil_term_set_t *set);

/* Get the terms from a search document image as an array. */
char **sil_document_image_terms(aml_pool_t *pool, sil_document_image_t *img, uint32_t *num_terms);

/* Match a term in a search document image. */
char *sil_document_image_match_term(sil_document_image_t *img, const char *term);
char *sil_document_image_match_termf(sil_document_image_t *img, aml_pool_t *pool, const char *term, ...);

/* To print out each term, use the following code:
 * #include "search-index-library/sil_document_image.h"
 * #include "search-index-library/sil_term.h"
 * #include "a-memory-library/aml_pool.h"
 *
 * aml_pool_t *pool = aml_pool_init(1024);
 * uint32_t num_terms;
 * char **terms = sil_document_image_terms(pool, img, &num_terms);
 * for(uint32_t i=0; i<num_terms; i++) {
 *    printf("%s\n", terms[i]);
 *    sil_term_t *t = sil_document_image_term(pool, img, terms[i]);
 *    if(!t) continue;
 *    sil_term_decode_positions(t);
 *    while(t->advance(t)) {
 *       sil_term_dump(t);
 *    }
 * }
 * aml_pool_destroy(pool);
 */

/* Get a term from a search document image which can be used for search. */
sil_term_t *sil_document_image_term(sil_document_image_t *img, aml_pool_t *pool, const char *term);
sil_term_t *sil_document_image_termf(sil_document_image_t *img, aml_pool_t *pool, const char *term, ...);

/* Custom callback for atl_cursor_cursor_open. */
atl_cursor_t *sil_document_image_custom_cb(aml_pool_t *pool, atl_token_t *token, void *arg);

typedef struct {
    uint32_t id;
    char *term;
} sil_id_term_t;

/* Write the terms to a buffer which can be used to build an inverted index. */
void sil_document_image_terms_to_buffer(aml_buffer_t *bh, sil_document_image_t *img, uint32_t id);

#include "search-index-library/impl/sil_document_image_impl.h"

#endif