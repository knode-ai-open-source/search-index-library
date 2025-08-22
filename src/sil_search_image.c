// SPDX-FileCopyrightText: 2023–2025 Andy Curtis <contactandyc@gmail.com>
// SPDX-FileCopyrightText: 2024–2025 Knode.ai — technical questions: contact Andy (above)
// SPDX-License-Identifier: Apache-2.0

#include "search-index-library/sil_search_image.h"
#include "search-index-library/impl/sil_constants.h"
#include <inttypes.h>

#include "the-io-library/io.h"
#include "a-memory-library/aml_buffer.h"
#include "the-macro-library/macro_bsearch.h"

static uint8_t *extract_group_bytes(uint8_t **sp, uint8_t *p) {
    uint8_t control = *p++;
    if(control < GROUP_2BYTE_LENGTH) {
        *sp = p;
        p += control;
        return p;
    } else if(control == GROUP_2BYTE_LENGTH) {
        uint16_t len = (*(uint16_t *)p);
        p += 2;
        *sp = p;
        return p + len;
    } else {
        uint32_t len = (*(uint32_t *)p);
        p += 4;
        *sp = p;
        return p + len;
    }
}

struct sil_search_image_s {
    uint32_t total_terms;
    uint32_t total_documents;
    double average_document_length;

    void **gbls;
    uint32_t num_gbls;

    char *gbl_data;
    size_t gbl_data_len;

    int8_t *embedding_data;
    size_t embedding_data_len;

    char *content_data;
    size_t content_data_len;

    char *term_idx;
    size_t term_idx_len;
    char **terms;
    size_t num_terms;
    char *term_data;
    size_t term_data_len;
};

void sil_search_image_destroy(sil_search_image_t *h) {
    aml_free(h->gbls);
    aml_free(h->gbl_data);
    free(h->embedding_data);
    aml_free(h->content_data);
    aml_free(h->term_idx);
    aml_free(h->terms);
    aml_free(h->term_data);
    aml_free(h);
}

const sil_global_header_t * sil_search_image_global(uint32_t *length, sil_search_image_t *h, uint32_t id) {
    if(id >= h->num_gbls)
        return NULL;
    char *p = h->gbls[id];
    if(!p)
        return NULL;
    *length = (*(uint32_t *)p);
    return (sil_global_header_t*)(p + sizeof(uint32_t));
}

const int8_t *sil_search_image_embeddings(sil_search_image_t *h, const sil_global_header_t *gh) {
    uint64_t offset = gh->embeddings_offset;
    offset = offset * 512;
    return (int8_t *)(h->embedding_data + offset);
}

const char *sil_search_image_content(sil_search_image_t *h,
                                     const sil_global_header_t *gh) {
    return (char *)(h->content_data + gh->content_offset);
}


uint32_t sil_search_image_max_id(sil_search_image_t *img) {
    return img->num_gbls;
}

sil_search_image_t *sil_search_image_init(const char *base) {
    char *p, *ep, **wp;
    size_t filename_len = strlen(base)+50;
    char *filename = (char *)aml_malloc(filename_len);
    sil_search_image_t *h = (sil_search_image_t *)aml_zalloc(sizeof(*h));

    snprintf(filename, filename_len, "%s_stats.txt", base );
    FILE *in = fopen(filename, "rb");
    if(fgets(filename, filename_len, in)  == NULL)
        return NULL;
    fclose(in);

    uint32_t num_terms, max_id;
    size_t total_documents, total_terms_in_documents;
    if(sscanf(filename, "%u %zu %zu %u", &num_terms, &total_documents, &total_terms_in_documents, &max_id) != 4)
        return NULL;

    h->total_terms = num_terms;
    h->total_documents = total_documents;
    h->average_document_length = total_documents > 0 ? (double)total_terms_in_documents / (double)total_documents : 0.0;

    h->gbls = (void **)aml_zalloc(sizeof(void *) * (max_id+1));
    h->num_gbls = max_id+1;

    snprintf(filename, filename_len, "%s_gbl", base );
    h->gbl_data = (char *)io_read_file(&h->gbl_data_len, filename);
    p = h->gbl_data;
    ep = p + h->gbl_data_len;
    while(p < ep) {
        uint32_t len = (*(uint32_t *)p);
        (*(uint32_t *)p) = len - sizeof(sil_global_header_t);
        char *np = p + sizeof(uint32_t) + sizeof(sil_global_header_t);
        uint32_t *id = (uint32_t *)np;
        h->gbls[*id] = p;
        p += sizeof(uint32_t) + len;
    }

    snprintf(filename, filename_len, "%s_embeddings", base );
    h->embedding_data = (int8_t *)io_read_file_aligned(&h->embedding_data_len, 64, filename);
    snprintf(filename, filename_len, "%s_content", base );
    h->content_data = (char *)io_read_file(&h->content_data_len, filename);

    snprintf(filename, filename_len, "%s_term_idx", base );
    h->term_idx = (char *)io_read_file(&h->term_idx_len, filename);
    h->num_terms = 0;
    p = h->term_idx;
    ep = p+h->term_idx_len;
    while(p < ep) {
        h->num_terms++;
        p += strlen(p) + 1;
        p += sizeof(size_t); // offset
    }

    h->terms = (char **)aml_zalloc(sizeof(char *) * h->num_terms);
    wp = h->terms;
    p = h->term_idx;
    while(p < ep) {
        *wp = p;
        wp++;
        p += strlen(p) + 1;
        p += sizeof(size_t);
    }

    snprintf(filename, filename_len, "%s_term_data", base );
    h->term_data = (char *)io_read_file(&h->term_data_len, filename);
    aml_free(filename);
    return h;
}

static inline int compare_strings(const char *key, const char **v) {
    return strcmp(key, *v);
}

static inline macro_bsearch_kv(search_strings, char, char *, compare_strings);

static inline bool advance_group(sil_term_ext_t *t);
static inline bool advance_group_to(sil_term_ext_t *t, uint32_t gid);


static bool sil_search_image_advance(sil_term_ext_t *t)
{
    if(t->p < t->ep) {
        advance_id(t);
        return true;
    }
    if(advance_group(t)) {
        advance_id(t);
        return true;
    }
    return false;
}

static bool sil_search_image_first_advance(sil_term_ext_t *t)
{
    t->pub.c.advance = (atl_cursor_advance_cb)sil_search_image_advance;
    return true;
}

static inline bool advance_group_to(sil_term_ext_t *t, uint32_t gid)
{
    uint32_t g = t->gid;
    if(g >= gid)
        return true;
    if((g & 0x3FC0000) == (gid & 0x3FC0000)) {
        uint32_t target = (gid & 0x3FC00) >> 10;
        while(t->ep < t->tp) {
            // advance to the next group within same high level group
            uint8_t control = t->ep[0];
            t->ep = extract_group_bytes(&t->p, t->ep+1);
            if(control >= target) {
                uint32_t g = control;
                g <<= 10;
                t->gid = (t->gid & 0x3FC0000) | g;
                return true;
            }
        }
        return advance_group(t);
    }
    uint32_t target = (gid & 0x3FC0000) >> 18;
    while(t->tp < t->etp) {
        // advance to the next high level group
        uint8_t control = t->tp[0];
        t->tp = extract_group_bytes(&t->ep, t->tp+1);
        if(control >= target) {
            uint32_t g = control;
            g <<= 18;
            t->gid = g;
            return advance_group_to(t, gid);
        }
    }
    return false;
}

static bool sil_search_image_advance_to(sil_term_ext_t *t, uint32_t id)
{
    if(id <= t->pub.c.id)
        return true;

    uint32_t gid = id & 0x3FFFC00; // mask off the lower 10 bits
    if(t->gid < gid) {
        if(!advance_group_to(t, gid))
            return false;
        advance_id(t);
        if(gid < t->gid) // if the gid is less than the target, we are done
            return true;
    }
    while(id > t->pub.c.id && t->p < t->ep)
        advance_id(t);
    if(id <= t->pub.c.id)
        return true;
    if(!advance_group(t))
        return false;
    advance_id(t);
    return true;
}

static inline bool advance_group(sil_term_ext_t *t)
{
    if(t->ep < t->tp) {
        // advance to the next group within same high level group
        uint8_t control = t->ep[0];
        uint32_t g = control;
        g <<= 10;
        t->ep = extract_group_bytes(&t->p, t->ep+1);
        t->gid = (t->gid & 0x3FC0000) | g;
        return true;
    }
    if(t->tp < t->etp) {
        // advance to the next high level group
        uint8_t control = t->tp[0];
        uint32_t g = control;
        g <<= 18;
        t->tp = extract_group_bytes(&t->ep, t->tp+1);
        t->gid = g;
        return advance_group(t);
    }
    return false;
}

/*
    TODO: (Andy) Consider term widths
    term widths allow terms to be wider than 1 term position.  This is useful for terms which
    represent multiple tokens such as CEO vs (Chief Executive Officer) and for phrases.

    Near-term support is only for phrases and boolean logic.  If a term is a phrase, the term width is
    the number of tokens in the phrase.

    uint32_t *term_widths;
*/


// might be useful to be a public function
static void fill_term(sil_search_image_t *img, aml_pool_t *pool,
                      sil_term_ext_t *r, char **termp) {
    char *p = *termp;
    p = p + strlen(p) + 1;
    size_t offs = (*(size_t *)p);
    p += sizeof(offs);

    sil_term_header_t *header = (sil_term_header_t *)(img->term_data + offs);
    r->tp = (uint8_t *)(header);
    uint32_t len = (*(uint32_t *)(r->tp-4));
    r->tp += sizeof(sil_term_header_t);
    // printf( "%s (%u bytes)\n", *termp, len);
    r->etp = r->tp + len;

    uint8_t control = (*(uint8_t *)r->tp);
    r->tp = extract_group_bytes(&r->ep, r->tp+1); // top level group - bits 18-25
    uint32_t gid = control;
    gid <<= 18;

    control = (*(uint8_t *)r->ep);
    r->ep = extract_group_bytes(&r->p, r->ep+1); // second level group - bits 10-17
    uint32_t g = control;
    g <<= 10;
    gid |= g;
    r->gid = gid;
    r->wp = NULL;
    r->pub.value = 0;

    advance_id(r);

    r->pub.max_term_size = header->max_positions;
    r->pub.document_frequency = header->document_frequency;
    r->pub.term_positions = (uint32_t *)aml_pool_alloc(pool, sizeof(uint32_t) * (header->max_positions+1));

    r->term = termp;
    r->eterm = img->terms+img->num_terms;
    r->pub.c.advance = (atl_cursor_advance_cb)sil_search_image_first_advance;
    r->pub.c.advance_to = (atl_cursor_advance_to_cb)sil_search_image_advance_to;

    r->pub.c.type = TERM_CURSOR;
}

sil_term_t *sil_search_image_term(sil_search_image_t *img, aml_pool_t *pool, const char *term) {
    char **termp = search_strings(term, (const char **)img->terms, img->num_terms);
    if(!termp) {
        if(term && term[0] && term[strlen(term)-1] == '*') {
            char *t = aml_pool_strdup(pool, term);
            t[strlen(t)-1] = 0;
            termp = search_strings(t, (const char **)img->terms, img->num_terms);
            if(!termp)
                return NULL;
        }
        else
            return NULL;
    }
    sil_term_ext_t *r = (sil_term_ext_t *)aml_pool_zalloc(pool, sizeof(*r));
    fill_term(img, pool, r, termp);
    return (sil_term_t *)r;
}

sil_term_t *sil_search_image_termf(sil_search_image_t *img, aml_pool_t *pool, const char *term, ...) {
  va_list args;
  va_start(args, term);
  char *r = aml_pool_strdupvf(pool, term, args);
  va_end(args);
  return sil_search_image_term(img, pool, r);
}

atl_cursor_t *sil_search_image_custom_cb(aml_pool_t *pool, atl_token_t *token, void *arg) {
    sil_search_image_t *img = (sil_search_image_t *)arg;
    return (atl_cursor_t *)sil_search_image_term(img, pool, token->token);
}

void sil_term_dump(sil_term_t *t) {
    printf("Term ID: %u, Value: %u, Pos Length: %zu, Positions: ",
           t->c.id, t->value,
           t->term_positions_end-t->term_positions);
    for (uint32_t *p = t->term_positions; p < t->term_positions_end; p++) {
        printf("%u ", *p);
    }
    printf("\n");
}
