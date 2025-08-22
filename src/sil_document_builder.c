// SPDX-FileCopyrightText: 2023–2025 Andy Curtis <contactandyc@gmail.com>
// SPDX-FileCopyrightText: 2024–2025 Knode.ai — technical questions: contact Andy (above)
// SPDX-License-Identifier: Apache-2.0

#include "search-index-library/sil_document_builder.h"
#include "search-index-library/impl/sil_constants.h"
#include "search-index-library/sil_term.h"
#include "search-index-library/sil_document_image.h"

#include <inttypes.h>

#include "the-io-library/io_out.h"
#include "a-memory-library/aml_buffer.h"
#include "the-macro-library/macro_sort.h"

struct sil_document_builder_s {
    aml_pool_t *term_pool;
    aml_pool_t *tmp_pool;
    aml_buffer_t *term_bh;
    uint32_t document_length_for_bm25;

    aml_buffer_t *bh;
    aml_buffer_t *tmp_bh;
};

struct term_data_s;
typedef struct term_data_s term_data_t;

struct term_data_s {
    uint32_t position;
    uint32_t value;
};

static bool compare_term_data(const term_data_t **p1, const term_data_t **p2) {
    term_data_t *a = (term_data_t *)*p1;
    term_data_t *b = (term_data_t *)*p2;
    int n=strcmp((char *)(a+1), (char *)(b+1));
    if(n)
        return n < 0;
    return a->position < b->position;
}

static inline
macro_sort(sort_term_data, term_data_t *, compare_term_data);

void sil_document_builder_destroy(sil_document_builder_t *h) {
    aml_pool_destroy(h->term_pool);
    aml_pool_destroy(h->tmp_pool);
    aml_buffer_destroy(h->term_bh);
    aml_buffer_destroy(h->bh);
    aml_buffer_destroy(h->tmp_bh);
    aml_free(h);
}

sil_document_builder_t *sil_document_builder_init() {
    sil_document_builder_t *h = (sil_document_builder_t *)aml_zalloc(sizeof(*h));
    h->term_pool = aml_pool_init(1024*64);
    h->tmp_pool = aml_pool_init(1024);
    h->term_bh = aml_buffer_init(1024);
    h->bh = aml_buffer_init(1024);
    h->tmp_bh = aml_buffer_init(1024);
    h->document_length_for_bm25 = 0;
    return h;
}

static void encode_high_bit(aml_buffer_t *bh, uint32_t value) {
    do {
        uint8_t byte = value & 0x7F;  // Extract the lowest 7 bits
        value >>= 7;                 // Shift right by 7 bits
        if (value != 0) {
            byte |= 0x80;            // Set the high bit to indicate more bytes
        }
        aml_buffer_append(bh, &byte, sizeof(byte));  // Append the byte to the buffer
    } while (value != 0);
}

static void encode_single_value(aml_buffer_t *bh, uint8_t sid, uint32_t value) {
    if(value < SMALL_GROUP_1BYTE_VALUE) {
        sid |= value;
        aml_buffer_append(bh, &sid, sizeof(sid));
    } else if(value < 256) {
        sid |= SMALL_GROUP_1BYTE_VALUE;
        uint8_t v = value;
        aml_buffer_append(bh, &sid, sizeof(sid));
        aml_buffer_append(bh, &v, sizeof(v));
    } else if(value < 65536) {
        sid |= SMALL_GROUP_2BYTE_VALUE;
        uint16_t v = value;
        aml_buffer_append(bh, &sid, sizeof(sid));
        aml_buffer_append(bh, &v, sizeof(v));
    } else {
        sid |= SMALL_GROUP_4BYTE_VALUE;
        aml_buffer_append(bh, &sid, sizeof(sid));
        aml_buffer_append(bh, &value, sizeof(value));
    }
}


static uint32_t encode_position_value(uint8_t *p, uint32_t value) {
    uint8_t next_byte = 0;
    if(value < SMALL_GROUP_2BYTE_POS_VALUE) {
        next_byte = value;
        *p = next_byte;
        return 1;
    } else if(value < SMALL_GROUP_4BYTE_POS_VALUE) {
        next_byte = SMALL_GROUP_2BYTE_POS_VALUE;
        *p++ = next_byte;
        (*(uint16_t *)p) = value;
        return 3;
    } else {
        next_byte = SMALL_GROUP_4BYTE_POS_VALUE;
        *p++ = next_byte;
        (*(uint32_t *)p) = value;
        return 5;
    }
}

static uint32_t encode_term_positions(aml_buffer_t *bh, term_data_t **p, term_data_t **ep) {
    // extract bits 8-9 from p->position
    uint32_t first_base = (*p)->position & FIRST_POSITION_BASE;
    uint32_t last_pos = first_base;
    while(p < ep) {
        uint32_t pos = (*p)->position;
        uint32_t delta = pos - last_pos;
        last_pos = pos;
        encode_high_bit(bh, delta);
        p++;
    }
    return first_base;
}

static void compress_term(term_data_t **cur, term_data_t **p,
                          aml_buffer_t *bh,
                          aml_buffer_t *tmp_bh) {
    const char *term = (char *)(*cur+1);
    aml_buffer_append(bh, term, strlen(term)+1); // zero will be added by sid
    uint8_t sid = 0;
    if(p-cur == 1 && (*cur)->position == 0) { // no term positions
        encode_single_value(bh, sid, (*cur)->value);
    } else {
        aml_buffer_clear(tmp_bh);
        term_data_t **p2 = cur;
        uint8_t value_data[8];
        uint32_t value_data_length = 0;

        if((*p2)->position == 0 && (*p2)->value != 0) {
            sid |= SMALL_GROUP_VALUE_PRESENT_MASK;
            value_data_length = encode_position_value(value_data, (*p2)->value);
            p2++;
        }

        // term positions should be delta encoded and then use high bit to indicate byte overflow
        uint32_t first_base = encode_term_positions(tmp_bh, p2, p);
        uint32_t len = aml_buffer_length(tmp_bh) - 1; // must always be at least one byte
        sid |= SMALL_GROUP_POS_MASK;
        sid |= (first_base >> 7);
        if(len < 0x3) {
            sid |= (len << 2);
            aml_buffer_append(bh, &sid, sizeof(sid));
            if(value_data_length)
                aml_buffer_append(bh, value_data, value_data_length);
        } else {
            sid |= SMALL_GROUP_EXTENDED_POS_LENGTH;
            aml_buffer_append(bh, &sid, sizeof(sid));
            if(value_data_length)
                aml_buffer_append(bh, value_data, value_data_length);
            encode_high_bit(bh, len);
        }
        aml_buffer_append(bh, aml_buffer_data(tmp_bh), aml_buffer_length(tmp_bh));
    }
}

void sil_document_builder_global(sil_document_builder_t *h,
                                 aml_buffer_t *document_bh,
                                 const int8_t *embeddings,
                                 uint32_t num_embeddings,
                                 const char *content,
                                 uint32_t content_length,
                                 const void *d, uint32_t len) {
    sort_term_data((term_data_t **)aml_buffer_data(h->term_bh), aml_buffer_length(h->term_bh)/sizeof(term_data_t *));
    term_data_t **p = (term_data_t **)aml_buffer_data(h->term_bh);
    term_data_t **ep = (term_data_t **)aml_buffer_end(h->term_bh);
    size_t num_terms = ep-p;
    while(p < ep) {
        term_data_t **cur = p;
        while(p < ep && !strcmp((char *)(*cur+1), (char *)(*p+1)))
            p++;

        compress_term(cur, p, h->bh, h->tmp_bh);
    }

    sil_document_header_t header;
    header.document_length_for_bm25 = h->document_length_for_bm25;
    header.term_length = aml_buffer_length(h->bh);
    header.data_length = len;
    header.content_length = content_length;
    header.num_embeddings = num_embeddings;
    header.num_terms = num_terms;

    uint32_t zero = 0;
    aml_buffer_set(document_bh, &zero, sizeof(zero));
    aml_buffer_append(document_bh, &header, sizeof(header));
    aml_buffer_append(document_bh, d, len);
    aml_buffer_append(document_bh, aml_buffer_data(h->bh), aml_buffer_length(h->bh)); // could go to an alternate place that allows cleanup every so often
    aml_buffer_append(document_bh, content, content_length); // content could go to disk and stay there
    uint32_t buffer_length = aml_buffer_length(document_bh);
    // append enough zeros to buffer to align at 64 bytes - this will make the whole buffer be aligned by 64 since 512 is multiple
    uint32_t padding = (64 - (buffer_length & 63)) & 63;
    if(padding)
        aml_buffer_appendn(document_bh, 0, padding);
    aml_buffer_append(document_bh, embeddings, num_embeddings*512); // embeddings could go to embedding table (never on disk)
    uint32_t *zero_ptr = (uint32_t *)aml_buffer_data(document_bh);
    *zero_ptr = aml_buffer_length(document_bh) - sizeof(uint32_t);

    aml_buffer_clear(h->bh);
    aml_buffer_clear(h->tmp_bh);
    aml_pool_clear(h->term_pool);
    aml_buffer_clear(h->term_bh);
    h->document_length_for_bm25 = 0;
}

static void _sil_document_builder_term(sil_document_builder_t *h, uint32_t value, uint32_t pos, const char *term ) {
    term_data_t *t = (term_data_t *)aml_pool_alloc(h->term_pool, sizeof(*t) + strlen(term)+1);
    t->position = pos;
    t->value = value;
    char *p = (char *)(t+1);
    while(*term) {
        if(*term >= 'A' && *term <= 'Z')
            *p++ = *term++ - 'A' + 'a';
        else
            *p++ = *term++;
    }
    *p = 0;
    aml_buffer_append(h->term_bh, &t, sizeof(t));
}

void sil_document_builder_term(sil_document_builder_t *h, const char *term ) {
    _sil_document_builder_term(h, 0, 0, term);
}

void sil_document_builder_termf(sil_document_builder_t *h, const char *term, ... ) {
  va_list args;
  va_start(args, term);
  aml_pool_clear(h->tmp_pool);
  char *r = aml_pool_strdupvf(h->tmp_pool, term, args);
  va_end(args);
  sil_document_builder_term(h, r);
}

static inline bool valid_expansion_term(const char *term, size_t len) {
    return false;
    const char *p, *ep;
    if(len > 12)
        return false;
    p = term;
    ep = p+len;
    while(p < ep) {
        if(*p >= 'a' && *p <= 'z')
            return true;
        return false;
    }
    return false;
}

void sil_document_builder_wterm(sil_document_builder_t *h, size_t sp, const char *term ) {
    sil_document_builder_term(h, term);
    size_t term_len = strlen(term);
    if(!valid_expansion_term(term, term_len))
        return;
    char *r = aml_pool_alloc(h->tmp_pool, term_len+2);
    strcpy(r, term);
    for( size_t i=term_len+1; i>sp; i-- ) {
      size_t ix=i-1;
      r[i] = 0;
      r[i-1] = '*';
      sil_document_builder_term(h, r);
    }
}

void sil_document_builder_wtermf(sil_document_builder_t *h, size_t sp, const char *term, ... ) {
    va_list args;
    va_start(args, term);
    aml_pool_clear(h->tmp_pool);
    char *r = aml_pool_strdupvf(h->tmp_pool, term, args);
    va_end(args);
    sil_document_builder_wterm(h, sp, r);
}



void sil_document_builder_term_position(sil_document_builder_t *h, uint32_t pos, const char *term ) {
    h->document_length_for_bm25++;
    _sil_document_builder_term(h, 0, pos, term);
}

void sil_document_builder_termf_position(sil_document_builder_t *h, uint32_t pos, const char *term, ... ) {
  va_list args;
  va_start(args, term);
  aml_pool_clear(h->tmp_pool);
  char *r = aml_pool_strdupvf(h->tmp_pool, term, args);
  va_end(args);
  sil_document_builder_term_position(h, pos, r);
}

void sil_document_builder_wterm_position(sil_document_builder_t *h, uint32_t pos, size_t sp, const char *term ) {
    sil_document_builder_term_position(h, pos, term);
    size_t term_len = strlen(term);
    if(!valid_expansion_term(term, term_len))
        return;
    char *r = aml_pool_alloc(h->tmp_pool, term_len+2);
    strcpy(r, term);
    for( size_t i=term_len+1; i>sp; i-- ) {
      size_t ix=i-1;
      r[i] = 0;
      r[i-1] = '*';
      _sil_document_builder_term(h, 0, pos, r);
    }
}

void sil_document_builder_wtermf_position(sil_document_builder_t *h, uint32_t pos, size_t sp, const char *term, ... ) {
    va_list args;
    va_start(args, term);
    aml_pool_clear(h->tmp_pool);
    char *r = aml_pool_strdupvf(h->tmp_pool, term, args);
    va_end(args);
    sil_document_builder_wterm_position(h, pos, sp, r);
}

void sil_document_builder_term_value(sil_document_builder_t *h, uint32_t value, const char *term ) {
    _sil_document_builder_term(h, value, 0, term);
}

void sil_document_builder_termf_value(sil_document_builder_t *h, uint32_t value, const char *term, ... ) {
  va_list args;
  va_start(args, term);
  aml_pool_clear(h->tmp_pool);
  char *r = aml_pool_strdupvf(h->tmp_pool, term, args);
  va_end(args);
  sil_document_builder_term_value(h, value, r);
}

void sil_document_builder_wterm_value(sil_document_builder_t *h, uint32_t value, size_t sp, const char *term ) {
    sil_document_builder_term_value(h, value, term);
    size_t term_len = strlen(term);
    if(!valid_expansion_term(term, term_len))
        return;
    char *r = aml_pool_alloc(h->tmp_pool, term_len+2);
    strcpy(r, term);
    for( size_t i=term_len+1; i>sp; i-- ) {
      size_t ix=i-1;
      r[i] = 0;
      r[i-1] = '*';
      sil_document_builder_term_value(h, value, r);
    }
}

void sil_document_builder_wtermf_value(sil_document_builder_t *h, uint32_t value, size_t sp, const char *term, ... ) {
    va_list args;
    va_start(args, term);
    aml_pool_clear(h->tmp_pool);
    char *r = aml_pool_strdupvf(h->tmp_pool, term, args);
    va_end(args);
    sil_document_builder_wterm_value(h, value, sp, r);
}
