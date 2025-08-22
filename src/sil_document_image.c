// SPDX-FileCopyrightText: 2023–2025 Andy Curtis <contactandyc@gmail.com>
// SPDX-FileCopyrightText: 2024–2025 Knode.ai — technical questions: contact Andy (above)
// SPDX-License-Identifier: Apache-2.0

#include "search-index-library/sil_document_image.h"
#include "the-macro-library/macro_sort.h"

void sil_document_image_init(sil_document_image_t *image, const char *document, uint32_t length) {
    image->document = document;
    image->length = length;
    image->header = *(sil_document_header_t *)document;
    image->data = (char *)(document + sizeof(sil_document_header_t));
    image->terms = (char *)image->data + image->header.data_length;
    image->content = (char *)image->terms + image->header.term_length;
    char *p = image->content + image->header.content_length;
    p = (char *)(((uintptr_t)p + 63) & ~63);
    image->embeddings = (int8_t *)(p);
}

char **sil_document_image_terms(aml_pool_t *pool, sil_document_image_t *img, uint32_t *num_terms) {
    *num_terms = img->header.num_terms;
    char *p = img->terms;
    char *ep = img->content;
    char **terms = (char **)aml_pool_alloc(pool, sizeof(char *) * img->header.num_terms);
    char **wp = terms;
    while(p < ep) {
        *wp = p;
        wp++;
        p += strlen(p) + 1;
        p = (char*)sil_skip_id((uint8_t*)p);
    }
    return terms;
}

static bool sil_document_image_advance_next(sil_term_ext_t *t)
{
    atl_cursor_empty(&t->pub.c);
    return false;
}

static bool sil_document_image_advance(sil_term_ext_t *t)
{
    t->pub.c.advance = (atl_cursor_advance_cb)sil_document_image_advance_next;
    return true;
}

// this is okay because we know there is only one id
static bool sil_document_image_advance_to(sil_term_ext_t *t, uint32_t id)
{
    return true;
}

static void fill_term(sil_document_image_t *img, aml_pool_t *pool,
                      sil_term_ext_t *r, char *termp) {
    char *p = termp;
    p = p + strlen(p) + 1;

    r->gid = 1;
    r->pub.c.id = 1;
    r->p = (uint8_t*)p;
    r->wp = NULL;

    advance_document_id(r);
    r->pub.max_term_size = r->p - r->wp;
    r->pub.document_frequency = 1;
    r->pub.term_positions = (uint32_t *)aml_pool_alloc(pool, sizeof(uint32_t) * (r->pub.max_term_size+1));

    r->pub.c.advance = (atl_cursor_advance_cb)sil_document_image_advance;
    r->pub.c.advance_to = (atl_cursor_advance_to_cb)sil_document_image_advance_to;

    r->pub.c.type = TERM_CURSOR;
}

static void set_fill_term(sil_term_ext_t *r, char *termp) {
    char *p = termp;
    p = p + strlen(p) + 1;

    r->p = (uint8_t*)p;
    r->wp = NULL;
    advance_document_id(r);
    sil_term_decode_positions(&r->pub);
}

// Structure to Hold Term + Position Before Sorting
typedef struct {
    char *term;
    size_t position;
} term_position_t;

// Compare Terms Alphabetically for Sorting
static inline
bool compare_term_positions(const term_position_t *a, const term_position_t *b) {
    int n = strcmp(a->term, b->term);
    if (n) return n < 0;
    return a->position < b->position;
}

macro_sort(sort_term_positions, term_position_t, compare_term_positions);

sil_term_set_t *sil_term_set_copy(aml_pool_t *pool, const sil_term_set_t *original) {
    if (!original || !pool) return NULL;

    // Step 1: Allocate memory for the new term set
    sil_term_set_t *copy = aml_pool_zalloc(pool, sizeof(sil_term_set_t));
    copy->num_terms = original->num_terms;
    copy->num_term_index = original->num_term_index;

    // Step 2: Allocate memory for terms
    copy->terms = aml_pool_zalloc(pool, copy->num_terms * sizeof(sil_term_data_t));

    // Step 3: Allocate memory for term_index
    copy->term_index = aml_pool_zalloc(pool, copy->num_term_index * sizeof(sil_term_data_t *));

    // Step 4: Copy each term data
    for (uint32_t i = 0; i < copy->num_terms; i++) {
        copy->terms[i] = original->terms[i];  // Struct copy
    }

    // Step 5: Update term_index to point to the correct copied terms
    for (uint32_t i = 0; i < copy->num_term_index; i++) {
        uint32_t term_offset = original->term_index[i] - original->terms;  // Compute index offset
        copy->term_index[i] = copy->terms+term_offset;
    }

    return copy;
}

// Function to Construct sil_term_set_t
sil_term_set_t *sil_construct_term_set(aml_pool_t *pool, const char *query, update_terms_cb cb, void *arg) {
    if (!query || !pool) return NULL;

    char *buffer = aml_pool_strdup(pool, query);
    char **split = (char **)aml_pool_alloc(pool, strlen(buffer) * sizeof(char *));
    char **wp = split;
    atl_token_t *tokens = atl_token_parse(pool, buffer);
    while(tokens) {
        char *term = tokens->token;
        while(*term) {
            if(*term >= 'A' && *term <= 'Z')
                *term = *term - 'A' + 'a';
            term++;
        }

        *wp = tokens->token;
        wp++;
        tokens = tokens->next;
    }

    size_t num_terms = wp - split;
    if (num_terms == 0) return NULL;

	if(pool)
		cb(pool, split, num_terms, arg);

    // Step 1: Create term/position array
    term_position_t *term_positions = aml_pool_alloc(pool, num_terms * sizeof(term_position_t));
    term_position_t *tp = term_positions;
    term_position_t *tp_end = term_positions + num_terms;
    char **s = split;
    size_t position = 0;

    while (tp < tp_end) {
        tp->term = *s;
        tp->position = position;
        position++;
        tp++;
        s++;
    }

    // Step 2: Sort term/position array
    sort_term_positions(term_positions, num_terms);

    // Step 3: Allocate memory for unique terms and term_index
    sil_term_data_t *terms = aml_pool_zalloc(pool, num_terms * sizeof(sil_term_data_t));
    sil_term_data_t **term_index = aml_pool_zalloc(pool, num_terms * sizeof(sil_term_data_t *));

    tp = term_positions;
    sil_term_data_t *t = terms;

    // Step 4: Process unique terms and assign positions
    while (tp < tp_end) {
        term_position_t *tp_curr = tp;
        tp++;
        while (tp < tp_end && strcmp(tp->term, tp_curr->term) == 0) {
            tp++;
        }
        t->term = aml_pool_strdup(pool, tp_curr->term);
        t->match = NULL;
        t->freq = 0;
        t->query_term_freq = tp-tp_curr;
        t->term_data = (sil_term_ext_t){0};
        while (tp_curr < tp) {
            term_index[tp_curr->position] = t;
            tp_curr++;
        }
        t++;
    }

    // Step 5: Construct the final term set
    sil_term_set_t *term_set = aml_pool_zalloc(pool, sizeof(sil_term_set_t));
    term_set->terms = terms;
    term_set->num_terms = t - terms;
    term_set->term_index = term_index;
    term_set->num_term_index = num_terms;

    return term_set;
}

void sil_document_image_add_set_freq(sil_term_set_t *dest, const sil_term_set_t *src) {
    for (uint32_t i = 0; i < src->num_terms; i++) {
        dest->terms[i].freq += src->terms[i].freq;
        if(dest->terms[i].max_term_size < src->terms[i].max_term_size)
            dest->terms[i].max_term_size = src->terms[i].max_term_size;
    }
}

void sil_document_image_update_frequency(sil_document_image_t *img, sil_term_set_t *set) {
    char *p = img->terms;
    char *ep = img->content;
    sil_term_data_t *t = set->terms;
    sil_term_data_t *et = set->terms + set->num_terms;
    size_t matched = 0;
    while(p < ep && t < et) {
        int n=strcmp(t->term, p);
        if (n < 0) {
            t++;
            continue;
        } else if (n == 0) {
            p += strlen(p) + 1;
            t->term_data.p = (uint8_t*)p;
            t->term_data.wp = NULL;
            advance_document_id(&t->term_data);
            size_t max_term_size = t->term_data.p - t->term_data.wp;
            if(max_term_size > t->max_term_size)
                t->max_term_size = max_term_size;
            p = (char *)t->term_data.p;
            t->freq++;
            t++;
        } else {
            p += strlen(p) + 1;
            p = (char*)sil_skip_id((uint8_t*)p);
        }
    }
}

void sil_document_image_match_prepare_for_set(aml_pool_t *pool, sil_term_set_t *set) {
    for(size_t i=0; i<set->num_terms; i++) {
        set->terms[i].term_data.pub.term_positions =
            (uint32_t *)aml_pool_alloc(pool, sizeof(uint32_t) * (set->terms[i].max_term_size+1));
    }
}

size_t sil_document_image_match_set(sil_document_image_t *img, sil_term_set_t *set) {
    char *p = img->terms;
    char *ep = img->content;
    sil_term_data_t *t = set->terms;
    sil_term_data_t *et = set->terms + set->num_terms;
    size_t matched = 0;
    while(p < ep && t < et) {
        int n=strcmp(t->term, p);
        if (n < 0) {
            t->match = NULL;
            t++;
            continue;
        } else if (n == 0) {
            matched++;
            t->match = p;
            set_fill_term(&t->term_data, p);
            p = (char *)t->term_data.p;
            t++;
        } else {
            p += strlen(p) + 1;
            p = (char*)sil_skip_id((uint8_t*)p);
        }
    }
    while(t < et) {
        t->match = NULL;
        t++;
    }
    return matched;
}

char *sil_document_image_match_term(sil_document_image_t *img, const char *term) {
    char *p = img->terms;
    char *ep = img->content;
    while(p < ep) {
        int n=strcmp(term, p);
        if(n < 0)
            return NULL;
        else if(n == 0)
            return p;

        p += strlen(p) + 1;
        p = (char*)sil_skip_id((uint8_t*)p);
    }
    return NULL;
}

char *sil_document_image_match_termf(sil_document_image_t *img, aml_pool_t *pool, const char *term, ...) {
  va_list args;
  va_start(args, term);
  char *r = aml_pool_strdupvf(pool, term, args);
  va_end(args);
  return sil_document_image_match_term(img, r);
}


sil_term_t *sil_document_image_term(sil_document_image_t *img, aml_pool_t *pool, const char *term) {
    char *termp = sil_document_image_match_term(img, term);
    if(!termp) {
        if(term && term[0] && term[strlen(term)-1] == '*') {
            char *t = aml_pool_strdup(pool, term);
            t[strlen(t)-1] = 0;
            termp = sil_document_image_match_term(img, t);
            if(!termp)
                return NULL;
        }
        else
            return NULL;
    }
    sil_term_ext_t *r = (sil_term_ext_t *)aml_pool_zalloc(pool, sizeof(*r));
    fill_term(img, pool, r, termp);
    return (sil_term_t*)r;
}

sil_term_t *sil_document_image_termf(sil_document_image_t *img, aml_pool_t *pool, const char *term, ...) {
  va_list args;
  va_start(args, term);
  char *r = aml_pool_strdupvf(pool, term, args);
  va_end(args);
  return sil_document_image_term(img, pool, r);
}

atl_cursor_t *sil_document_image_custom_cb(aml_pool_t *pool, atl_token_t *token, void *arg) {
    sil_document_image_t *img = (sil_document_image_t *)arg;
    return (atl_cursor_t *)sil_document_image_term(img, pool, token->token);
}

void sil_document_image_terms_to_buffer(aml_buffer_t *bh, sil_document_image_t *img, uint32_t id) {
    char *p = img->terms;
    char *ep = img->content;
    while(p < ep) {
        sil_id_term_t it;
        it.id = id;
        it.term = p;
        aml_buffer_append(bh, &it, sizeof(id));
        p += strlen(p) + 1;
        p = (char*)sil_skip_id((uint8_t*)p);
    }
}
