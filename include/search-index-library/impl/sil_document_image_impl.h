// SPDX-FileCopyrightText: 2023–2025 Andy Curtis <contactandyc@gmail.com>
// SPDX-FileCopyrightText: 2024–2025 Knode.ai — technical questions: contact Andy (above)
// SPDX-License-Identifier: Apache-2.0

#ifndef _sil_document_image_impl_H
#define _sil_document_image_impl_H

#include "search-index-library/impl/sil_constants.h"
#include <inttypes.h>
#include <stddef.h>

static inline uint8_t *skip_single_value(uint32_t flags, uint8_t *p) {
    if(flags < SMALL_GROUP_1BYTE_VALUE) {
        return p;
    } else if(flags == SMALL_GROUP_1BYTE_VALUE) {
        return p+1;
    } else if(flags == SMALL_GROUP_2BYTE_VALUE) {
        return p+2;
    } else {
        return p+4;
    }
}

static inline uint8_t *skip_term_positions(uint32_t flags, uint8_t *p) {
    flags &= SMALL_GROUP_POS_LENGTH_MASK;
    flags >>= 2;
    if(flags < 0x3) {
        return p+flags+1;
    } else {
        p = __decode_high_bit32(&flags, p);
        return p+flags+1;
    }
}

static inline uint8_t *skip_position_value(uint8_t *p) {
    if(*p < SMALL_GROUP_2BYTE_POS_VALUE) {
        return p+1;
    } else if(*p < SMALL_GROUP_4BYTE_POS_VALUE) {
        p++;
        return p+2;
    } else {
        p++;
        return p+4;
    }
}


static inline uint8_t *sil_skip_id(uint8_t *p) {
    uint8_t control = (*(uint8_t *)p); // Read the 16-bit control word
    p += 1;

    uint32_t flags = control;

    if (flags & SMALL_GROUP_POS_MASK) {
        // Position data is present
        if (flags & SMALL_GROUP_VALUE_PRESENT_MASK) {
            // Value data is present
            p = skip_position_value(p);
        }
        return skip_term_positions(flags, p);
    } else
        return skip_single_value(flags, p);
}

#endif
