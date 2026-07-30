#include "gx5125/feature_record.h"

#include <stddef.h>
#include <string.h>

uint32_t gx_feature_record_bitreverse32(uint32_t value) {
    value = ((value & UINT32_C(0x55555555)) << 1U) |
            ((value >> 1U) & UINT32_C(0x55555555));
    value = ((value & UINT32_C(0x33333333)) << 2U) |
            ((value >> 2U) & UINT32_C(0x33333333));
    value = ((value & UINT32_C(0x0f0f0f0f)) << 4U) |
            ((value >> 4U) & UINT32_C(0x0f0f0f0f));
    value = ((value & UINT32_C(0x00ff00ff)) << 8U) |
            ((value >> 8U) & UINT32_C(0x00ff00ff));
    return (value << 16U) | (value >> 16U);
}

void gx_feature_record_apply_mode(uint8_t record[GX_FEATURE_RECORD_BYTES],
                                  int identify_mode) {
    static const uint8_t pair_order[8] = {0U, 1U, 1U, 0U, 1U, 0U, 0U, 1U};
    uint8_t normalized[0x18];
    uint32_t descriptor_word;
    uint32_t reversed_word = 0U;
    size_t pair;

    if (record == NULL) {
        return;
    }

    memset(normalized, 0, sizeof(normalized));
    for (pair = 0U; pair < 8U; ++pair) {
        const uint8_t left = record[0x10U + pair * 2U];
        const uint8_t right = record[0x11U + pair * 2U];
        const uint8_t right_high_left_low =
            (uint8_t)((right & UINT8_C(0xf0)) | (left & UINT8_C(0x0f)));
        const uint8_t left_high_right_low =
            (uint8_t)((left & UINT8_C(0xf0)) | (right & UINT8_C(0x0f)));

        if (pair_order[pair] == 0U) {
            normalized[pair] = right_high_left_low;
            normalized[8U + pair] = left_high_right_low;
        } else {
            normalized[pair] = left_high_right_low;
            normalized[8U + pair] = right_high_left_low;
        }
    }

    memcpy(&descriptor_word, record + 0x20U, sizeof(descriptor_word));
    memcpy(normalized + 0x10U, &descriptor_word, sizeof(descriptor_word));
    if (identify_mode != 0) {
        reversed_word = gx_feature_record_bitreverse32(descriptor_word);
    }
    memcpy(normalized + 0x14U, &reversed_word, sizeof(reversed_word));
    memcpy(record + 0x10U, normalized, sizeof(normalized));

    if (identify_mode != 0) {
        record[0x30U] = record[0x28U];
        record[0x31U] = (uint8_t)~record[0x29U];
        record[0x32U] = (uint8_t)~record[0x2aU];
        record[0x33U] = record[0x2bU];
        record[0x34U] = record[0x2fU];
        record[0x35U] = record[0x2eU];
        record[0x36U] = record[0x2dU];
        record[0x37U] = record[0x2cU];
    }
}
