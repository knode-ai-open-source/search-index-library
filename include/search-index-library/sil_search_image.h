// SPDX-FileCopyrightText: 2023–2025 Andy Curtis <contactandyc@gmail.com>
// SPDX-FileCopyrightText: 2024–2025 Knode.ai — technical questions: contact Andy (above)
// SPDX-License-Identifier: Apache-2.0

#ifndef _sil_search_image_h
#define _sil_search_image_h

#include <inttypes.h>
#include <stddef.h>
#include "a-memory-library/aml_pool.h"
#include "search-index-library/sil_term.h"
#include "a-tokenizer-library/atl_cursor.h"

struct sil_search_image_s;
typedef struct sil_search_image_s sil_search_image_t;

sil_search_image_t *sil_search_image_init(const char *filename);

const sil_global_header_t * sil_search_image_global(uint32_t *length,
                                                    sil_search_image_t *h,
                                                    uint32_t id);
const int8_t *sil_search_image_embeddings(sil_search_image_t *h,
                                          const sil_global_header_t *gh);
const char *sil_search_image_content(sil_search_image_t *h,
                                     const sil_global_header_t *gh);

uint32_t sil_search_image_max_id(sil_search_image_t *img);

sil_term_t *sil_search_image_term(sil_search_image_t *img, aml_pool_t *pool, const char *term);
sil_term_t *sil_search_image_termf(sil_search_image_t *img, aml_pool_t *pool, const char *term, ...);

// to support and, or, not, phrase, etc
atl_cursor_t *sil_search_image_custom_cb(aml_pool_t *pool, atl_token_t *token, void *arg);

void sil_search_image_destroy(sil_search_image_t *h);

#endif
