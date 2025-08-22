// SPDX-FileCopyrightText: 2023–2025 Andy Curtis <contactandyc@gmail.com>
// SPDX-FileCopyrightText: 2024–2025 Knode.ai — technical questions: contact Andy (above)
// SPDX-License-Identifier: Apache-2.0

#ifndef _sil_term_impl_h
#define _sil_term_impl_h

#include "search-index-library/impl/sil_constants.h"
#include <math.h>

/*
TODO: (Andy) Consider allowing value to be signed using least significant bit

Code sample is below:

// Encode a signed integer into an unsigned integer
uint32_t encode_signed(int32_t value) {
    if (value < 0) {
        return ((-value) << 1) | 1; // Store absolute value, set 0th bit to 1
    }
    return (value << 1); // Store value, set 0th bit to 0
}

// Decode an unsigned integer back into a signed integer
int32_t decode_signed(uint32_t encoded) {
    if (encoded & 1) { // Check 0th bit
        return -(int32_t)(encoded >> 1); // Negative value
    }
    return (int32_t)(encoded >> 1); // Positive value
}
*/

typedef struct {
    sil_term_t pub;

    uint32_t gid;

    uint32_t first_base;  // since in many cases there is only one word position, make first delta 10 bits
                          // by using two bits from 2 byte header
    uint8_t *wp;

    uint8_t *tp;  // for the whole term
    uint8_t *etp;

    uint8_t *p;  // for a particular sub-group
    uint8_t *ep; // end of sub-group

    char **term;
    char **eterm;
} sil_term_ext_t;

typedef struct {
    char *term;
    char *match;
    uint32_t freq;
    uint32_t query_term_freq;
    uint32_t max_term_size;
    double idf;
    double bm25;
    sil_term_ext_t term_data;
} sil_term_data_t;

typedef struct {
    sil_term_data_t *terms;
    sil_term_data_t **term_index;
    uint32_t num_terms;
    uint32_t num_term_index;
} sil_term_set_t;

static inline uint8_t *__decode_high_bit32(uint32_t *value, uint8_t *p) {
    uint32_t v = 0;
    v = *p++;
    if((v & 0x80) == 0) {
        *value = v;
        return p;
    }
    v -= 0x80;
    uint32_t bits = *p++;
    if((bits & 0x80) == 0) {
        *value = v | (bits << 7);
        return p;
    }
    bits -= 0x80;
    *value = v | (bits << 7);
    bits = *p++;
    if((bits & 0x80) == 0) {
        *value |= (bits << 14);
        return p;
    }
    bits -= 0x80;
    *value |= (bits << 14);
    bits = *p++;
    if((bits & 0x80) == 0) {
        *value |= (bits << 21);
        return p;
    }
    bits -= 0x80;
    *value |= (bits << 21);
    bits = *p++;
    // not possible to overflow here to due 32 bit limit
    *value = v | (bits << 28);
    return p;
}

// Expected distance thresholds (for the first 5 occurrences)
static const uint32_t sil_spread_distance_thresholds[] = {30, 70, 200, 400};

// Decay factors for each occurrence (first gets full weight, then decreases)
static const double sil_spread_decay_factors[] = {1.0, 0.7, 0.5, 0.3, 0.2};

// Maximum number of occurrences to consider for scoring
#define SIL_SPREAD_MAX_OCCURRENCES 5

// Compute weighted term spread score for the first 5 occurrences
static inline
double sil_term_spread_score(uint32_t *positions, uint32_t size) {
    if (size == 0) return 0.0;  // No occurrences
    if (size == 1) return sil_spread_decay_factors[0] / positions[0];  // Only one occurrence gets full weight

    double score = 0.0;
    uint32_t prev_position = positions[0];  // First occurrence
    score += sil_spread_decay_factors[0] / positions[0];  // First term full weight

    uint32_t count = (size > SIL_SPREAD_MAX_OCCURRENCES) ? SIL_SPREAD_MAX_OCCURRENCES : size; // Cap at 5 occurrences

    // Compute weights for additional occurrences (up to MAX_OCCURRENCES)
    for (uint32_t i = 1; i < count; i++) {
        uint32_t dist = positions[i] - prev_position;
        uint32_t threshold = sil_spread_distance_thresholds[i - 1];  // Use predefined spacing

        // Apply decay and distance normalization
        double weight = (sil_spread_decay_factors[i] * ((double) (dist < threshold ? dist : threshold) / threshold)) / positions[i];
        // printf("dist: %u, threshold: %u, weight: %f\n", dist, threshold, weight);
        score += weight;
        prev_position = positions[i];
    }

    return score / count;  // Normalize by occurrences (up to 5)
}

// Compute IDF weight for a term
// total_documents is the total number of documents in the collection
// documents_with_term is the number of documents containing the term (this does not count multiple
//   occurrences per document)
static inline
double sil_term_idf(double total_documents, double documents_with_term) {
    return log((total_documents + 1) / (documents_with_term + 0.5));
}

// Compute BM25+ qtf weight for query term frequency scaling
// query_term_freq is the frequency of the term in the query
// k3 is a constant that controls the query term frequency scaling (typically 8.0)
static inline
double sil_qtf_weight(double query_term_freq, double k3) {
    return (query_term_freq * (k3 + 1)) / (query_term_freq + k3);
}

// compute idf * qtf weight for bm25+ scoring
// total_documents is the total number of documents in the collection
// documents_with_term is the number of documents containing the term
// query_term_freq is the frequency of the term in the query
// k3 is a constant that controls the query term frequency scaling (typically 8.0)
static inline
double sil_idf_qtf(double total_documents, double documents_with_term, double query_term_freq, double k3) {
    double idf = sil_term_idf(total_documents, documents_with_term);
    double qtf_weight = sil_qtf_weight(query_term_freq, k3);
    return idf * qtf_weight;
}

// Compute document normalization factor
// k1 is a constant that controls the term frequency scaling (typically 1.2)
// b is a constant that controls the scaling of the document length (typically 0.75)
// aveD is the average document length
static inline
double sil_bm25_doc_norm(double doc_length, double aveD, double k1, double b) {
    return k1 * (1 - b + b * (doc_length / aveD));
}

// Compute term frequency (TF) using precomputed document normalization for BM25
// term_freq is the frequency of the term in the document
// k1 is a constant that controls the term frequency scaling (typically 1.2)
// bm25_doc_norm is the document normalization factor
static inline
double sil_bm25_tf(double term_freq, double k1, double bm25_doc_norm) {
    return (term_freq * (k1 + 1)) / (term_freq + bm25_doc_norm);
}

static inline
double sil_bm25_score(double idf, double bm25_tf) {  // Previously: sil_bm25()
    return idf * bm25_tf;
}

// Compute term frequency (TF) using precomputed document normalization for BM25+
// term_freq is the frequency of the term in the document
// delta is a small value to prevent division by zero (typically 1.0)
// k1 is a constant that controls the term frequency scaling (typically 1.2)
// bm25_doc_norm is the document normalization factor
static inline
double sil_bm25_plus_tf(double term_freq, double delta, double k1, double bm25_doc_norm) {
    return ((term_freq + delta) * (k1 + 1)) / (term_freq + bm25_doc_norm);
}

// The spread score is a value between 0 and 1 that represents the spread of term occurrences
// It favors terms that are spread out evenly throughout the document.  It also favors terms
// that appear early in the document.  By applying this score to the BM25+ score, we can make
// the score account for term distribution in addition to term frequency.
static inline
double sil_bm25_plus_tf_spread(double term_freq, double delta, double k1, double bm25_doc_norm, double spread_score) {
    double bm25_plus_tf = ((term_freq + delta) * (k1 + 1)) / (term_freq + bm25_doc_norm);
    return bm25_plus_tf * (1.0 + spread_score); // Boost by spread score
}

static inline
double sil_bm25_plus_score(double idf_qtf, double bm25_plus_tf) {
    return idf_qtf * bm25_plus_tf;
}

static inline
uint32_t sil_pair_proximity(sil_term_t *t1, sil_term_t *t2) {
    uint32_t *i = t1->term_positions, *j = t2->term_positions;
    uint32_t *ep1 = t1->term_positions_end, *ep2 = t2->term_positions_end;
    if (i == ep1 || j == ep2) return UINT32_MAX;  // No valid positions

    uint32_t min_distance = UINT32_MAX;

    while (true) {  // No need for (i < ep1 && j < ep2) since breaks handle it
        if (*i > *j) {
            uint32_t diff = (*i - *j) + 1;  // Apply out-of-order penalty
            if (diff < min_distance) min_distance = diff;
            if (++j == ep2) break;
        }
        else if (*i < *j) {
            uint32_t diff = *j - *i;
            if (diff < min_distance) min_distance = diff;
            if (++i == ep1) break;
        }
        else {  // Handles *i == *j without applying a proximity score
            // advance j to avoid out of order penalty
            if (++j == ep2) break;
        }
    }
    return min_distance;
}


static inline void sil_term_decode_positions(sil_term_t *t) {
    sil_term_ext_t *ext = (sil_term_ext_t *)t;
    uint8_t *p = ext->wp;
    uint8_t *ep = ext->p;
    uint32_t last_pos = ext->first_base;
    uint32_t *wp = t->term_positions;
    while(p < ep) {
        uint32_t delta;
        p = __decode_high_bit32(&delta, p);
        last_pos += delta;
        *wp++ = last_pos;
    }
    t->term_positions_end = wp;
}


static inline uint8_t *decode_position_value(uint32_t *value, uint8_t *p) {
    if(*p < SMALL_GROUP_2BYTE_POS_VALUE) {
        *value = *p;
        return p+1;
    } else if(*p < SMALL_GROUP_4BYTE_POS_VALUE) {
        p++;
        *value = (*(uint16_t *)p);
        return p+2;
    } else {
        p++;
        *value = (*(uint32_t *)p);
        return p+4;
    }
}

static inline uint8_t *decode_term_positions(uint8_t **sp, uint32_t *first_base, uint32_t flags, uint8_t *p) {
    flags &= SMALL_GROUP_POS_LENGTH_MASK;
    *first_base = (flags & 0x3) << 7;
    flags >>= 2;
    if(flags < 0x3) {
        *sp = p;
        return p+flags+1;
    } else {
        p = __decode_high_bit32(&flags, p);
        *sp = p;
        return p+flags+1;
    }
}

static inline uint8_t *decode_single_value(uint32_t *value, uint32_t flags, uint8_t *p) {
    if(flags < SMALL_GROUP_1BYTE_VALUE) {
        *value = flags;
        return p;
    } else if(flags == SMALL_GROUP_1BYTE_VALUE) {
        *value = (*(uint8_t *)p);
        return p+1;
    } else if(flags == SMALL_GROUP_2BYTE_VALUE) {
        *value = (*(uint16_t*)p);
        return p+2;
    } else {
        *value = (*(uint32_t*)p);
        return p+4;
    }
}

static inline void advance_id(sil_term_ext_t *t) {
    uint8_t *p = t->p;
    uint16_t control = (*(uint16_t *)p); // Read the 16-bit control word
    p += 2;

    // Extract the 10-bit ID from the high bits
    uint32_t id = control >> SMALL_GROUP_SHIFT;
    uint32_t flags = control & SMALL_GROUP_FLAGS;

    t->pub.c.id = id + t->gid; // Combine with group ID
    if (flags & SMALL_GROUP_POS_MASK) {
        // Position data is present
        if (flags & SMALL_GROUP_VALUE_PRESENT_MASK) {
            // Value data is present
            p = decode_position_value(&t->pub.value, p);
        }
        t->p = decode_term_positions(&t->wp, &t->first_base, flags, p);
    } else {
        t->p = decode_single_value(&t->pub.value, flags, p);
        t->wp = t->p;
    }
}

static inline void advance_document_id(sil_term_ext_t *t) {
    uint8_t *p = t->p;
    uint8_t control = (*(uint8_t *)p); // Read the 16-bit control word
    p += 1;

    // Extract the 10-bit ID from the high bits
    uint32_t flags = control;

    t->pub.c.id = 1;
    if (flags & SMALL_GROUP_POS_MASK) {
        // Position data is present
        if (flags & SMALL_GROUP_VALUE_PRESENT_MASK) {
            // Value data is present
            p = decode_position_value(&t->pub.value, p);
        }
        t->p = decode_term_positions(&t->wp, &t->first_base, flags, p);
    } else {
        t->p = decode_single_value(&t->pub.value, flags, p);
        t->wp = t->p;
    }
}


#endif
