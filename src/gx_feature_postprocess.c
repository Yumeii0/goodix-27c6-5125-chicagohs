#include "gx5125/feature_postprocess.h"

#include <string.h>

static uint16_t gx_read_u16(const uint8_t *p) {
    uint16_t value;
    memcpy(&value, p, sizeof(value));
    return value;
}

size_t gx_feature_post_packed_bytes(int32_t width, int32_t height) {
    size_t stride;
    if (width <= 0 || height <= 0) return 0U;
    stride = ((size_t)width + 7U) / 8U;
    if (stride > SIZE_MAX / (size_t)height) return 0U;
    return stride * (size_t)height;
}

int gx_feature_post_partition_records(uint8_t *records,
                                      uint8_t *parallel_flags,
                                      uint32_t count,
                                      uint32_t *zero_class_count) {
    int32_t front = 0;
    int32_t last;
    uint8_t temporary[GX_FEATURE_POST_RECORD_BYTES];

    if (zero_class_count == NULL) return -1;
    *zero_class_count = 0U;
    if (count == 0U) return 0;
    if (records == NULL || parallel_flags == NULL ||
        count > GX_FEATURE_POST_MAX_RECORDS) return -1;

    last = (int32_t)count - 1;
    if (last >= 1) {
        for (;;) {
            while (front < last &&
                   (records[(size_t)front * GX_FEATURE_POST_RECORD_BYTES] & 3U) == 0U) {
                ++front;
            }
            if (last <= front) break;
            while (front < last &&
                   (records[(size_t)last * GX_FEATURE_POST_RECORD_BYTES] & 3U) == 1U) {
                --last;
            }
            if (last <= front) break;

            memcpy(temporary,
                   records + (size_t)front * GX_FEATURE_POST_RECORD_BYTES,
                   sizeof(temporary));
            memcpy(records + (size_t)front * GX_FEATURE_POST_RECORD_BYTES,
                   records + (size_t)last * GX_FEATURE_POST_RECORD_BYTES,
                   sizeof(temporary));
            memcpy(records + (size_t)last * GX_FEATURE_POST_RECORD_BYTES,
                   temporary,
                   sizeof(temporary));
            {
                const uint8_t value = parallel_flags[front];
                parallel_flags[front] = parallel_flags[last];
                parallel_flags[last] = value;
            }
        }
    }

    if (front == last &&
        (records[(size_t)front * GX_FEATURE_POST_RECORD_BYTES] & 3U) == 0U) {
        ++front;
    }
    *zero_class_count = (uint32_t)front;
    return 0;
}

int gx_feature_post_prune_mask(uint8_t *records,
                               uint32_t *count,
                               const gx_feature_post_map *mask) {
    uint32_t index = 0U;
    uint32_t current_count;

    if (count == NULL || mask == NULL || mask->pixels == NULL ||
        mask->width <= 0 || mask->height <= 0) return -1;
    current_count = *count;
    if (current_count > GX_FEATURE_POST_MAX_RECORDS) return -1;
    if (current_count != 0U && records == NULL) return -1;

    while (index < current_count) {
        uint8_t *record = records + (size_t)index * GX_FEATURE_POST_RECORD_BYTES;
        const int32_t x = (int32_t)((gx_read_u16(record + 2U) + 0x80U) >> 8);
        const int32_t y = (int32_t)((gx_read_u16(record + 4U) + 0x80U) >> 8);
        int remove = 0;

        if (x < mask->width && y < mask->height && x >= 0 && y >= 0) {
            remove = mask->pixels[(size_t)y * (size_t)mask->width + (size_t)x] == 0U;
        }
        if (remove) {
            const uint32_t last = current_count - 1U;
            --current_count;
            if (index != last) {
                memcpy(record,
                       records + (size_t)last * GX_FEATURE_POST_RECORD_BYTES,
                       GX_FEATURE_POST_RECORD_BYTES);
                memset(records + (size_t)last * GX_FEATURE_POST_RECORD_BYTES,
                       0,
                       GX_FEATURE_POST_RECORD_BYTES);
            }
            /* Re-check the record copied from the end. */
        } else {
            ++index;
        }
    }
    *count = current_count;
    return 0;
}

int gx_feature_post_pack_map(const gx_feature_post_map *map,
                             uint8_t *packed,
                             size_t packed_bytes) {
    size_t required;
    size_t y;
    size_t stride;

    if (map == NULL || map->pixels == NULL || packed == NULL) return -1;
    required = gx_feature_post_packed_bytes(map->width, map->height);
    if (required == 0U || packed_bytes < required) return -1;
    stride = ((size_t)map->width + 7U) / 8U;
    memset(packed, 0, required);

    for (y = 0U; y < (size_t)map->height; ++y) {
        size_t x;
        for (x = 0U; x < (size_t)map->width; ++x) {
            if (map->pixels[y * (size_t)map->width + x] != 0U) {
                packed[y * stride + x / 8U] |= (uint8_t)(1U << (x & 7U));
            }
        }
    }
    return 0;
}
