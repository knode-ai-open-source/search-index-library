// SPDX-FileCopyrightText: 2023-2025 Andy Curtis <contactandyc@gmail.com>
// SPDX-FileCopyrightText: 2024-2025 Knode.ai
// SPDX-License-Identifier: Apache-2.0
#include "search-index-library/sil_search_builder.h"
#include "search-index-library/impl/sil_constants.h"
#include "search-index-library/sil_term.h"
#include <inttypes.h>

#include "the-io-library/io_out.h"
#include "a-memory-library/aml_buffer.h"

static io_out_t *open_sorted(char *filename, io_compare_cb compare, size_t buffer_size) {
  io_out_options_t options;
  io_out_ext_options_t ext_options;
  io_out_options_init(&options);
  io_out_ext_options_init(&ext_options);

  io_out_options_format(&options, io_prefix());
  io_out_options_buffer_size(&options, buffer_size);
  io_out_ext_options_compare(&ext_options, compare, NULL);
  io_out_ext_options_reducer(&ext_options,
                             io_keep_first, NULL);
  io_out_ext_options_use_extra_thread(&ext_options);

  return io_out_ext_init(filename, &options, &ext_options);
}

struct sil_search_builder_s {
    char *filename;
    char *base_filename;
    size_t filename_len;
    size_t buffer_size;
    aml_buffer_t *bh;
    aml_pool_t *tmp_pool;
    aml_buffer_t *global_bh;
    io_out_t *term_data;
    io_out_t *global_data;
    uint32_t max_id;
    uint32_t current_id;
    uint32_t document_length;
    size_t total_terms;
    size_t total_documents;
};

struct term_data_s;
typedef struct term_data_s term_data_t;

struct term_data_s {
    uint32_t id;
    uint32_t position;
    uint32_t value;
};

static int compare_term_data(const io_record_t *r1, const io_record_t *r2, void *arg) {
    term_data_t *a = (term_data_t *)r1->record;
    term_data_t *b = (term_data_t *)r2->record;
    int n=strcmp((char *)(a+1), (char *)(b+1));
    if(n)
        return n;
    if(a->id != b->id)
        return (a->id < b->id) ? -1 : 1;
    if(a->position != b->position)
        return (a->position < b->position) ? -1 : 1;
    return 0;
}

static int compare_global_data(const io_record_t *r1, const io_record_t *r2, void *arg) {
    sil_global_header_t *ap = (sil_global_header_t *)r1->record;
    sil_global_header_t *bp = (sil_global_header_t *)r2->record;
    uint32_t *a = (uint32_t *)(ap+1);
    uint32_t *b = (uint32_t *)(bp+1);
    if(*a != *b)
        return (*a < *b) ? -1 : 1;
    return 0;
}

sil_search_builder_t *sil_search_builder_init(const char *filename, size_t buffer_size) {
    sil_search_builder_t *h = (sil_search_builder_t *)aml_zalloc(sizeof(*h) + (strlen(filename)*2) + 50);
    h->base_filename = (char *)(h+1);
    strcpy(h->base_filename, filename);
    h->filename_len = strlen(filename);
    h->filename = h->base_filename + h->filename_len + 1;

    snprintf(h->filename, h->filename_len+40, "%s_data", filename );
    h->term_data = open_sorted(h->filename, compare_term_data, buffer_size);
    snprintf(h->filename, h->filename_len+40, "%s_gbl", filename );
    h->global_data = open_sorted(h->filename, compare_global_data, buffer_size/10);
    h->global_bh = aml_buffer_init(256);
    h->buffer_size = buffer_size;
    h->bh = aml_buffer_init(256);
    h->tmp_pool = aml_pool_init(1024);
    return h;
}

static inline void _finish_document(sil_search_builder_t *h) {
    if(aml_buffer_length(h->global_bh)) {
        sil_global_header_t *gh = (sil_global_header_t *)aml_buffer_data(h->global_bh);
        gh->document_length = h->document_length;
        h->total_documents++;
        h->total_terms += h->document_length;
        io_out_write_record(h->global_data, aml_buffer_data(h->global_bh), aml_buffer_length(h->global_bh));
    }
}

// the id is expected to be the first 4 bytes of d
void sil_search_builder_global(sil_search_builder_t *h,
                               const int8_t *embeddings,
                               uint32_t num_embeddings,
                               const char *content,
                               uint32_t content_length,
                               const void *d, uint32_t len) {
    _finish_document(h);
    sil_global_header_t gh;
    memset(&gh, 0, sizeof(gh));
    gh.content_offset = content_length + sizeof(uint32_t);  // reused for overall length of content
    gh.embeddings_offset = len + sizeof(sil_global_header_t);  // reused for overall length of global
    gh.num_embeddings = num_embeddings;
    aml_buffer_set(h->global_bh, &gh, sizeof(gh));
    aml_buffer_append(h->global_bh, d, len);
    aml_buffer_append(h->global_bh, embeddings, num_embeddings*512);
    aml_buffer_append(h->global_bh, &content_length, sizeof(content_length));
    aml_buffer_append(h->global_bh, content, content_length);

    uint32_t id = (*(uint32_t *)d);
    if(id > h->max_id)
        h->max_id = id;
    h->current_id = id;
    h->document_length = 0;
}

static void _sil_search_builder_term(sil_search_builder_t *h, uint32_t value, uint32_t pos, const char *term ) {
    term_data_t t;
    t.id = h->current_id;
    t.position = pos;
    t.value = value;
    aml_buffer_set(h->bh, &t, sizeof(t));
    aml_buffer_append(h->bh, term, strlen(term)+1);
    // printf( "%s %u %u %u bh len(%zu)\n", term, id, value, pos, aml_buffer_length(h->bh) );
    io_out_write_record(h->term_data, aml_buffer_data(h->bh), aml_buffer_length(h->bh));
}

void sil_search_builder_term(sil_search_builder_t *h, const char *term ) {
    _sil_search_builder_term(h, 0, 0, term);
}

void sil_search_builder_termf(sil_search_builder_t *h, const char *term, ... ) {
  va_list args;
  va_start(args, term);
  aml_pool_clear(h->tmp_pool);
  char *r = aml_pool_strdupvf(h->tmp_pool, term, args);
  va_end(args);
  sil_search_builder_term(h, r);
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

void sil_search_builder_wterm(sil_search_builder_t *h, size_t sp, const char *term ) {
    sil_search_builder_term(h, term);
    size_t term_len = strlen(term);
    if(!valid_expansion_term(term, term_len))
        return;
    char *r = aml_pool_alloc(h->tmp_pool, term_len+2);
    strcpy(r, term);
    for( size_t i=term_len+1; i>sp; i-- ) {
      size_t ix=i-1;
      r[i] = 0;
      r[i-1] = '*';
      sil_search_builder_term(h, r);
    }
}

void sil_search_builder_wtermf(sil_search_builder_t *h, size_t sp, const char *term, ... ) {
    va_list args;
    va_start(args, term);
    aml_pool_clear(h->tmp_pool);
    char *r = aml_pool_strdupvf(h->tmp_pool, term, args);
    va_end(args);
    sil_search_builder_wterm(h, sp, r);
}



void sil_search_builder_term_position(sil_search_builder_t *h, uint32_t pos, const char *term ) {
    h->document_length++;
    _sil_search_builder_term(h, 0, pos, term);
}

void sil_search_builder_termf_position(sil_search_builder_t *h, uint32_t pos, const char *term, ... ) {
  va_list args;
  va_start(args, term);
  aml_pool_clear(h->tmp_pool);
  char *r = aml_pool_strdupvf(h->tmp_pool, term, args);
  va_end(args);
  sil_search_builder_term_position(h, pos, r);
}

void sil_search_builder_wterm_position(sil_search_builder_t *h, uint32_t pos, size_t sp, const char *term ) {
    sil_search_builder_term_position(h, pos, term);
    size_t term_len = strlen(term);
    if(!valid_expansion_term(term, term_len))
        return;
    char *r = aml_pool_alloc(h->tmp_pool, term_len+2);
    strcpy(r, term);
    for( size_t i=term_len+1; i>sp; i-- ) {
      size_t ix=i-1;
      r[i] = 0;
      r[i-1] = '*';
      _sil_search_builder_term(h, 0, pos, r);
    }
}

void sil_search_builder_wtermf_position(sil_search_builder_t *h, uint32_t pos, size_t sp, const char *term, ... ) {
    va_list args;
    va_start(args, term);
    aml_pool_clear(h->tmp_pool);
    char *r = aml_pool_strdupvf(h->tmp_pool, term, args);
    va_end(args);
    sil_search_builder_wterm_position(h, pos, sp, r);
}

void sil_search_builder_term_value(sil_search_builder_t *h, uint32_t value, const char *term ) {
    _sil_search_builder_term(h, value, 0, term);
}

void sil_search_builder_termf_value(sil_search_builder_t *h, uint32_t value, const char *term, ... ) {
  va_list args;
  va_start(args, term);
  aml_pool_clear(h->tmp_pool);
  char *r = aml_pool_strdupvf(h->tmp_pool, term, args);
  va_end(args);
  sil_search_builder_term_value(h, value, r);
}

void sil_search_builder_wterm_value(sil_search_builder_t *h, uint32_t value, size_t sp, const char *term ) {
    sil_search_builder_term_value(h, value, term);
    size_t term_len = strlen(term);
    if(!valid_expansion_term(term, term_len))
        return;
    char *r = aml_pool_alloc(h->tmp_pool, term_len+2);
    strcpy(r, term);
    for( size_t i=term_len+1; i>sp; i-- ) {
      size_t ix=i-1;
      r[i] = 0;
      r[i-1] = '*';
      sil_search_builder_term_value(h, value, r);
    }
}

void sil_search_builder_wtermf_value(sil_search_builder_t *h, uint32_t value, size_t sp, const char *term, ... ) {
    va_list args;
    va_start(args, term);
    aml_pool_clear(h->tmp_pool);
    char *r = aml_pool_strdupvf(h->tmp_pool, term, args);
    va_end(args);
    sil_search_builder_wterm_value(h, value, sp, r);
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

static void encode_single_value(aml_buffer_t *bh, uint16_t sid, uint32_t value) {
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

static uint32_t encode_term_positions(aml_buffer_t *bh, term_data_t *p, term_data_t *ep) {
    // extract bits 8-9 from p->position
    uint32_t first_base = p->position & FIRST_POSITION_BASE;
    uint32_t last_pos = first_base;
    while(p < ep) {
        uint32_t pos = p->position;
        uint32_t delta = pos - last_pos;
        last_pos = pos;
        size_t len = aml_buffer_length(bh);
        encode_high_bit(bh, delta);
        // printf( "encoded %u (%u) in %zu byte(s)\n", delta, pos, aml_buffer_length(bh) - len );
        p++;
    }
    return first_base;
}

static uint32_t compress_single_id(uint16_t sid,
                                   term_data_t *cur, term_data_t *p,
                                   aml_buffer_t *group_bh,
                                   aml_buffer_t *tmp_bh) {
    uint32_t num_positions = 0;
    if(p-cur == 1 && cur->position == 0) { // no term positions
        encode_single_value(group_bh, sid, cur->value);
    } else {
        aml_buffer_clear(tmp_bh);
        term_data_t *p2 = cur;
        uint8_t value_data[8];
        uint32_t value_data_length = 0;

        if(p2->position == 0 && p2->value != 0) {
            sid |= SMALL_GROUP_VALUE_PRESENT_MASK;
            value_data_length = encode_position_value(value_data, p2->value);
            p2++;
        }
        num_positions = p-p2;

        // term positions should be delta encoded and then use high bit to indicate byte overflow
        uint32_t first_base = encode_term_positions(tmp_bh, p2, p);
        uint32_t len = aml_buffer_length(tmp_bh) - 1; // must always be at least one byte
        sid |= SMALL_GROUP_POS_MASK;
        sid |= (first_base >> 7);
        if(len < 0x3) {
            sid |= (len << 2);
            aml_buffer_append(group_bh, &sid, sizeof(sid));
            if(value_data_length)
                aml_buffer_append(group_bh, value_data, value_data_length);
        } else {
            sid |= SMALL_GROUP_EXTENDED_POS_LENGTH;
            aml_buffer_append(group_bh, &sid, sizeof(sid));
            if(value_data_length)
                aml_buffer_append(group_bh, value_data, value_data_length);
            encode_high_bit(group_bh, len);
        }
        aml_buffer_append(group_bh, aml_buffer_data(tmp_bh), aml_buffer_length(tmp_bh));
    }
    return num_positions;
}

static uint32_t compress_small_group_data_into_group(uint32_t *document_frequency,
                                                     aml_buffer_t *group_bh,
                                                     aml_buffer_t *tmp_bh,
                                                     term_data_t *p, term_data_t *ep) {
    uint32_t id;
    uint32_t max_positions = 0;
    aml_buffer_clear(group_bh);
    while(p < ep) {
        term_data_t *cur = p;
        p++;
        id = cur->id & SMALL_GROUP_MASK;
        while(p < ep && id == (p->id & SMALL_GROUP_MASK))
            p++;
        uint16_t sid = id << SMALL_GROUP_SHIFT;
        *document_frequency += 1;
        uint32_t num_positions=compress_single_id(sid, cur, p, group_bh, tmp_bh);
        if(num_positions > max_positions)
            max_positions = num_positions;
    }
    return max_positions;
}

static void encode_group_to_buffer(aml_buffer_t *bh, uint8_t gid, aml_buffer_t *group_bh) {
    uint32_t len = aml_buffer_length(group_bh);
    aml_buffer_append(bh, &gid, sizeof(gid));
    if(len < GROUP_2BYTE_LENGTH) {
        uint8_t value = len;
        aml_buffer_append(bh, &value, sizeof(value));
    } else if(len < 65536) {
        uint8_t value = GROUP_2BYTE_LENGTH;
        aml_buffer_append(bh, &value, sizeof(value));
        uint16_t v = len;
        aml_buffer_append(bh, &v, sizeof(v));
    } else {
        uint8_t value = GROUP_4BYTE_LENGTH;
        aml_buffer_append(bh, &value, sizeof(value));
        aml_buffer_append(bh, &len, sizeof(len));
    }
    aml_buffer_append(bh, aml_buffer_data(group_bh), len);
}

uint32_t compress_groups(uint32_t *document_frequency, aml_buffer_t **bhs, term_data_t *p, term_data_t *ep) {
    uint32_t max_positions = 0;
    aml_buffer_clear(bhs[0]);
    while(p < ep) {
        // 26 bits, so bits 22-25
        term_data_t *cur = p;
        uint32_t id = cur->id & 0x3FC0000;
        p++;
        while(p < ep && id == (p->id & 0x3FC0000))
            p++;

        aml_buffer_clear(bhs[1]);
        term_data_t *p2 = cur;
        while(p2 < p) {
            term_data_t *cur2 = p2;
            id = cur2->id & 0x3FFFC00;
            p2++;
            while(p2 < p && id == (p2->id & 0x3FFFC00))
                p2++;
            aml_buffer_clear(bhs[2]);
            uint32_t max_positions_in_group = compress_small_group_data_into_group(document_frequency,
                                                                                   bhs[2], bhs[3], cur2, p2);
            if(max_positions_in_group > max_positions)
                max_positions = max_positions_in_group;
            uint32_t group_id = (cur2->id & 0x3FC00) >> 10;
            uint8_t gid = group_id;
            encode_group_to_buffer(bhs[1], gid, bhs[2]);
        }

        uint32_t group_id = (cur->id & 0x3FC0000) >> 18;
        uint8_t gid = group_id;
        encode_group_to_buffer(bhs[0], gid, bhs[1]);
    }
    return max_positions;
}

void sil_search_builder_destroy(sil_search_builder_t *h) {
    _finish_document(h); // finish the last document

    io_record_t *r;
    aml_buffer_t *bhs[4];
    bhs[0] = aml_buffer_init(1024*1024);
    bhs[1] = aml_buffer_init(1024*1024);
    bhs[2] = aml_buffer_init(1024*1024);
    bhs[3] = aml_buffer_init(1024*1024);

    aml_buffer_t *key = aml_buffer_init(128);
    aml_buffer_t *bh = aml_buffer_init(1024*1024);
    io_in_t *in;
    FILE *out_idx, *out_data, *out_stats, *out_gbl, *out_emb, *out_content;
    size_t offs;

    uint32_t total_embeddings = 0;
    uint64_t content_offset = 0;

    in = io_out_in(h->global_data);
    snprintf(h->filename, h->filename_len+40, "%s_gbl", h->base_filename);
    out_gbl = fopen(h->filename, "wb");
    snprintf(h->filename, h->filename_len+40, "%s_embeddings", h->base_filename);
    out_emb = fopen(h->filename, "wb");
    snprintf(h->filename, h->filename_len+40, "%s_content", h->base_filename);
    out_content = fopen(h->filename, "wb");

    while((r=io_in_advance(in)) != NULL) {
        sil_global_header_t *gh = ( sil_global_header_t *)r->record;
        char *main_global_data = r->record;
        uint32_t main_global_length = gh->embeddings_offset;
        uint32_t content_length = gh->content_offset;
        int8_t *embedding_data = (int8_t *)(main_global_data + main_global_length);
        char *content_data = (char *)(embedding_data + (gh->num_embeddings * 512));

        gh->content_offset = content_offset;
        gh->embeddings_offset = total_embeddings;

        uint32_t *id = (uint32_t *)(main_global_data + sizeof(sil_global_header_t));

        fwrite(&main_global_length, sizeof(main_global_length), 1, out_gbl);
        fwrite(main_global_data, main_global_length, 1, out_gbl);

        fwrite(embedding_data, gh->num_embeddings*512, 1, out_emb);
        fwrite(content_data, content_length, 1, out_content);

        total_embeddings += gh->num_embeddings;
        content_offset += content_length;
    }
    io_in_destroy(in);
    fclose(out_gbl);
    fclose(out_emb);
    fclose(out_content);
    snprintf(h->filename, h->filename_len+40, "%s_term_idx", h->base_filename);
    out_idx = fopen(h->filename, "wb");
    snprintf(h->filename, h->filename_len+40, "%s_term_data", h->base_filename);
    out_data = fopen(h->filename, "wb");

    uint32_t total_terms = 0;
    offs = 4;
    in = io_out_in(h->term_data);
    r=io_in_advance(in);
    while(r != NULL) {
        aml_buffer_set(key, r->record+sizeof(term_data_t), r->length-sizeof(term_data_t));
        aml_buffer_set(bh, r->record, sizeof(term_data_t));
        while((r=io_in_advance(in)) != NULL &&
              !strcmp(r->record+sizeof(term_data_t), aml_buffer_data(key))) {
            aml_buffer_append(bh, r->record, sizeof(term_data_t));
        }
        term_data_t *p = (term_data_t *)aml_buffer_data(bh);
        term_data_t *ep = (term_data_t *)aml_buffer_end(bh);
        uint32_t document_frequency = 0;
        uint32_t max_positions = compress_groups(&document_frequency, bhs, p, ep);
        fwrite(aml_buffer_data(key), aml_buffer_length(key), 1, out_idx);
        fwrite(&offs, sizeof(offs), 1, out_idx);

        uint32_t len = aml_buffer_length(bhs[0]) + sizeof(sil_term_header_t);
        offs += len + 4;
        fwrite(&len, sizeof(len), 1, out_data);
        sil_term_header_t header;
        header.max_positions = max_positions;
        header.document_frequency = document_frequency;
        fwrite(&header, sizeof(header), 1, out_data);
        fwrite(aml_buffer_data(bhs[0]), aml_buffer_length(bhs[0]), 1, out_data);
        total_terms++;
    }
    fclose(out_idx);
    fclose(out_data);
    io_in_destroy(in);

    aml_buffer_destroy(bhs[0]);
    aml_buffer_destroy(bhs[1]);
    aml_buffer_destroy(bhs[2]);
    aml_buffer_destroy(bhs[3]);

    aml_buffer_destroy(bh);
    aml_buffer_destroy(key);

    aml_buffer_destroy(h->bh);
    aml_pool_destroy(h->tmp_pool);

    snprintf(h->filename, h->filename_len+40, "%s_stats.txt", h->base_filename);
    out_stats = fopen(h->filename, "wb");
    fprintf(out_stats, "%u %zu %zu %u\n", total_terms, h->total_documents, h->total_terms, h->max_id );
    fprintf(out_stats, "total_terms: %u\n", total_terms );
    fprintf(out_stats, "max_id: %u\n", h->max_id );
    fprintf(out_stats, "total_documents: %zu\n", h->total_documents );
    fprintf(out_stats, "total_terms_in_documents: %zu\n", h->total_terms );
    fprintf(out_stats, "average document length: %f\n",
            h->total_documents > 0 ? (double)h->total_terms / (double)h->total_documents : 0.0);
    fclose(out_stats);
    aml_free(h);
}
