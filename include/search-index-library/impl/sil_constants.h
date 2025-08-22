// SPDX-FileCopyrightText: 2023–2025 Andy Curtis <contactandyc@gmail.com>
// SPDX-FileCopyrightText: 2024–2025 Knode.ai — technical questions: contact Andy (above)
// SPDX-License-Identifier: Apache-2.0

#ifndef _SIL_SEARCH_CONSTANTS_H_
#define _SIL_SEARCH_CONSTANTS_H_

#define SMALL_GROUP_MASK 0x3FF        // mask to extract the id from the lower 10 bits
#define SMALL_GROUP_SHIFT 6           // id shift to allow for value and position below
#define SMALL_GROUP_FLAGS 0x3F        // lower 6 bit mask
#define SMALL_GROUP_HIGH_MASK 0xFFC0  // id mask to allow for value and position below
#define SMALL_GROUP_4BYTE_VALUE 0x1F  // 4 byte value
#define SMALL_GROUP_2BYTE_VALUE 0x1E  // 2 byte value
#define SMALL_GROUP_1BYTE_VALUE 0x1D  // 1 byte value
#define SMALL_GROUP_POS_MASK 0x20     // indicates the presence of term positions
#define SMALL_GROUP_VALUE_PRESENT_MASK 0x10  // indicates the presence of term value (used when term positions are present)
#define SMALL_GROUP_4BYTE_POS_VALUE 0xFF // 4 byte value (when term positions are present)
#define SMALL_GROUP_2BYTE_POS_VALUE 0xFE // 2 byte value (when term positions are present)

// two bits for length (0==1, 1==2, 2==3, 3==overflow)
// then high bit encoded length
// this allows for 2 bits extension to delta
#define FIRST_POSITION_BASE 0x180  // bits 8,9 of first position
#define SMALL_GROUP_POS_LENGTH_MASK 0xF // bits for position length
#define SMALL_GROUP_EXTENDED_POS_LENGTH 0xC

#define GROUP_4BYTE_LENGTH 0xFF
#define GROUP_2BYTE_LENGTH 0xFE

#endif
