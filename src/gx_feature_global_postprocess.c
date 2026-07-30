#include "gx5125/feature_global_postprocess.h"
#include "gx5125/feature_quality_map.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define GX_OFF_WIDTH 0x000U
#define GX_OFF_HEIGHT 0x004U
#define GX_OFF_RECORD_COUNT 0x0f0U
#define GX_OFF_RECORDS 0x0f8U
#define GX_OFF_POSITIVE_PERCENT 0x158U
#define GX_OFF_MAP_STATE 0x15cU
#define GX_OFF_NONPOSITIVE_COUNT 0x160U
#define GX_OFF_RECORD_QUALITY 0x164U
#define GX_OFF_OUTPUT_MAP 0x220U

static uint32_t gx_read_u32(const uint8_t *base, size_t offset) {
    uint32_t value;
    memcpy(&value, base + offset, sizeof(value));
    return value;
}

static void gx_write_u32(uint8_t *base, size_t offset, uint32_t value) {
    memcpy(base + offset, &value, sizeof(value));
}

static uint8_t *gx_read_pointer(const uint8_t *base, size_t offset) {
    uint8_t *value;
    memcpy(&value, base + offset, sizeof(value));
    return value;
}

static int8_t gx_signed_byte(uint8_t value) {
    return (int8_t)value;
}

uint64_t gx_feature_global_postprocess(uint8_t *feature_object,
                                       int32_t mode,
                                       int32_t internal_profile) {
    uint32_t width;
    uint32_t height;
    uint32_t count;
    uint8_t *records;
    uint8_t *output_map;
    int8_t *work_map;
    gx_feature_quality_stats stats;
    size_t pixels;
    uint32_t marked = 0U;
    uint32_t i;
    uint32_t nonpositive = 0U;
    uint32_t map_state = 0U;
    int32_t threshold;

    if (feature_object == NULL) return 0U;

    width = gx_read_u32(feature_object, GX_OFF_WIDTH);
    height = gx_read_u32(feature_object, GX_OFF_HEIGHT);
    count = gx_read_u32(feature_object, GX_OFF_RECORD_COUNT);
    records = gx_read_pointer(feature_object, GX_OFF_RECORDS);

    if (count > GX_FEATURE_MAX_RECORDS ||
        (count != 0U && records == NULL)) return 0U;

    for (i = 0U; i < count; ++i) {
        uint8_t *record = records + (size_t)i * GX_FEATURE_RECORD_BYTES;
        const int8_t flag = gx_signed_byte(record[0x38U]);
        const int8_t quality = gx_signed_byte(feature_object[GX_OFF_RECORD_QUALITY + i]);
        if (flag > 0 ||
            (mode > 2 && quality < 30) ||
            (mode > 1 && quality < 20)) {
            ++marked;
            record[0x38U] = 2U;
        }
    }

    if (marked == 0U) return 0U;
    if (width == 0U || height == 0U ||
        (size_t)height > SIZE_MAX / (size_t)width) return 0U;
    pixels = (size_t)width * (size_t)height;
    output_map = gx_read_pointer(feature_object, GX_OFF_OUTPUT_MAP);
    if (output_map == NULL) return 0U;

    work_map = (int8_t *)calloc(pixels, sizeof(*work_map));
    if (work_map == NULL) return 0U;

    for (i = 0U; i < count; ++i) {
        const uint8_t *record = records + (size_t)i * GX_FEATURE_RECORD_BYTES;
        const int32_t center_x = (int32_t)record[0x03U];
        const int32_t center_y = (int32_t)record[0x05U];
        const int8_t delta = gx_signed_byte(record[0x38U]) > 0 ? 1 : -1;
        int32_t dx;
        for (dx = -15; dx < 15; ++dx) {
            const int32_t x = center_x + dx;
            int32_t dy;
            if (x < 0 || x >= (int32_t)width) continue;
            for (dy = -15; dy < 15; ++dy) {
                const int32_t y = center_y + dy;
                size_t index;
                if (y < 0 || y >= (int32_t)height) continue;
                index = (size_t)y * (size_t)width + (size_t)x;
                work_map[index] = (int8_t)(work_map[index] + delta);
            }
        }
    }

    if (gx_feature_quality_classify_local(work_map,
                                          (int32_t)height,
                                          (int32_t)width,
                                          15,
                                          (uint8_t *)work_map,
                                          &stats) != 0) {
        free(work_map);
        return 0U;
    }

    memcpy(output_map, work_map, pixels);

    for (i = 0U; i < count; ++i) {
        uint8_t *record = records + (size_t)i * GX_FEATURE_RECORD_BYTES;
        const uint32_t x = record[0x03U];
        const uint32_t y = record[0x05U];
        const int8_t map_value = work_map[(size_t)y * (size_t)width + (size_t)x];
        const int8_t flag = gx_signed_byte(record[0x38U]);
        if (map_value == -1 && flag == 0) {
            record[0x38U] = 3U;
        } else if (map_value == 0 && flag > 0) {
            record[0x38U] = 0xffU;
        }
    }

    for (i = 0U; i < count; ++i) {
        const int8_t flag = gx_signed_byte(
            records[(size_t)i * GX_FEATURE_RECORD_BYTES + 0x38U]);
        if (flag < 1) ++nonpositive;
    }

    gx_write_u32(feature_object, GX_OFF_NONPOSITIVE_COUNT, nonpositive);
    gx_write_u32(feature_object, GX_OFF_POSITIVE_PERCENT,
                 (uint32_t)stats.positive_percent);

    threshold = 10;
    if (internal_profile == 7 || internal_profile == 23) threshold = 5;
    if (stats.positive_percent >= threshold)
        map_state = stats.positive_percent > 64 ? 2U : 1U;
    gx_write_u32(feature_object, GX_OFF_MAP_STATE, map_state);

    free(work_map);
    return 1U;
}
