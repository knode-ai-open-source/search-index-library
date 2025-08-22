// SPDX-FileCopyrightText: 2023–2025 Andy Curtis <contactandyc@gmail.com>
// SPDX-FileCopyrightText: 2024–2025 Knode.ai — technical questions: contact Andy (above)
// SPDX-License-Identifier: Apache-2.0

#ifndef _sil_search_builder_h
#define _sil_search_builder_h

#include <inttypes.h>
#include <stddef.h>
#include "a-memory-library/aml_pool.h"

struct sil_search_builder_s;
typedef struct sil_search_builder_s sil_search_builder_t;

sil_search_builder_t *sil_search_builder_init(const char *filename, size_t buffer_size);

// The first 4 bytes of d must be the local id
void sil_search_builder_global(sil_search_builder_t *h,
                               const int8_t *embeddings,
                               uint32_t num_embeddings,
                               const char *content,
                               uint32_t content_length,
                               const void *d, uint32_t len);

void sil_search_builder_term(sil_search_builder_t *h, const char *term );
void sil_search_builder_termf(sil_search_builder_t *h, const char *term, ... );

// adds the term and wildcard starting at sp position in string
void sil_search_builder_wterm(sil_search_builder_t *h, size_t sp, const char *term );
void sil_search_builder_wtermf(sil_search_builder_t *h, size_t sp, const char *term, ... );

void sil_search_builder_term_position(sil_search_builder_t *h, uint32_t pos, const char *term );
void sil_search_builder_termf_position(sil_search_builder_t *h, uint32_t pos, const char *term, ... );

// adds the term and wildcard starting at sp position in string
void sil_search_builder_wterm_position(sil_search_builder_t *h, uint32_t pos, size_t sp, const char *term );
void sil_search_builder_wtermf_position(sil_search_builder_t *h, uint32_t pos, size_t sp, const char *term, ... );

void sil_search_builder_term_value(sil_search_builder_t *h, uint32_t value, const char *term );
void sil_search_builder_termf_value(sil_search_builder_t *h, uint32_t value, const char *term, ... );

// adds the term and wildcard starting at sp position in string
void sil_search_builder_wterm_value(sil_search_builder_t *h, uint32_t value, size_t sp, const char *term );
void sil_search_builder_wtermf_value(sil_search_builder_t *h, uint32_t value, size_t sp, const char *term, ... );

void sil_search_builder_destroy(sil_search_builder_t *h);

#endif
