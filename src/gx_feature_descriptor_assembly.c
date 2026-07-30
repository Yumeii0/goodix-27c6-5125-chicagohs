#include "gx5125/feature_descriptor_assembly.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "gx5125/feature_descriptor_sample.h"
#include "gx5125/feature_primitives.h"

static int16_t wrap_add_i16(int16_t left, int16_t right) {
    return (int16_t)(uint16_t)((uint16_t)left + (uint16_t)right);
}

static int16_t wrap_mul_i16(int16_t left, int16_t right) {
    return (int16_t)(uint16_t)((uint16_t)left * (uint16_t)right);
}

static uint32_t load_u32(const uint8_t *pointer) {
    uint32_t value;
    memcpy(&value, pointer, sizeof(value));
    return value;
}

static void store_u32(uint8_t *pointer, uint32_t value) {
    memcpy(pointer, &value, sizeof(value));
}

void gx_feature_descriptor_project_hadamard(
    uint8_t record[GX_FEATURE_RECORD_BYTES],
    const int16_t compressed[GX_FEATURE_DESCRIPTOR_OUTPUT_VALUES],
    const int16_t rotation[GX_FEATURE_ROTATION_TABLE_SIDE *
                           GX_FEATURE_ROTATION_TABLE_SIDE]) {
    static const int32_t signs[4][4] = {
        { 1,  1,  1,  1},
        { 1, -1,  1, -1},
        { 1,  1, -1, -1},
        { 1, -1, -1,  1}
    };
    uint32_t words[4] = {0U, 0U, 0U, 0U};
    uint32_t bit_index;

    if (record == NULL || compressed == NULL || rotation == NULL) return;
    memset(record + 0x10U, 0, 0x18U);

    for (bit_index = 0U; bit_index < 32U; ++bit_index) {
        int16_t partial[4] = {0, 0, 0, 0};
        uint32_t group;
        uint32_t row;
        const int16_t *basis = rotation + (size_t)bit_index * 128U;

        for (group = 0U; group < 4U; ++group) {
            uint32_t element;
            int16_t sum = 0;
            for (element = 0U; element < 32U; ++element) {
                const size_t index = (size_t)group * 32U + element;
                sum = wrap_add_i16(
                    sum,
                    wrap_mul_i16(compressed[index], basis[index]));
            }
            partial[group] = sum;
        }

        for (row = 0U; row < 4U; ++row) {
            int32_t score = 0;
            uint32_t group_index;
            for (group_index = 0U; group_index < 4U; ++group_index) {
                score += signs[row][group_index] * (int32_t)partial[group_index];
            }
            if (score > 0) words[row] |= UINT32_C(1) << bit_index;
        }
    }

    store_u32(record + 0x10U, words[0]);
    store_u32(record + 0x14U, words[1]);
    store_u32(record + 0x18U, words[2]);
    store_u32(record + 0x1cU, words[3]);
}

static int compress_descriptor(const int32_t *values,
                               int32_t value_count,
                               int16_t compressed[128]) {
    uint64_t energy = 0U;
    uint32_t threshold;
    uint32_t cap;
    int32_t index;

    if (values == NULL || compressed == NULL || value_count <= 0 ||
        value_count > 128) {
        return -1;
    }

    for (index = 0; index < value_count; ++index) {
        const uint64_t value = (uint32_t)values[index];
        energy += value * value;
    }
    threshold = (uint32_t)(((uint64_t)gx_feature_isqrt_u64(energy) *
                            UINT64_C(0x3333)) >> 16U);
    cap = gx_feature_isqrt_u32(threshold);

    memset(compressed, 0, 128U * sizeof(compressed[0]));
    for (index = 0; index < value_count; ++index) {
        const uint32_t value = (uint32_t)values[index];
        compressed[index] = (int16_t)(uint16_t)
            ((value < threshold) ? gx_feature_isqrt_u32(value) : cap);
    }
    return 0;
}

int gx_feature_descriptor_finalize(
    uint8_t record[GX_FEATURE_RECORD_BYTES],
    const int16_t rotation[GX_FEATURE_ROTATION_TABLE_SIDE *
                           GX_FEATURE_ROTATION_TABLE_SIDE],
    const int32_t *values,
    int32_t value_count,
    int32_t mode,
    int32_t profile,
    int32_t tail_rows,
    int32_t tail_channels) {
    int16_t compressed[128];
    uint32_t tail[4];
    uint8_t *tail_destination;

    if (record == NULL || rotation == NULL || values == NULL ||
        tail_rows <= 0 || tail_channels <= 0 ||
        (int64_t)tail_rows * tail_channels > value_count) {
        return -1;
    }
    if (profile == 9 || profile == 18) return -2;
    if (compress_descriptor(values, value_count, compressed) != 0) return -1;

    if (mode == 0) {
        gx_feature_descriptor_project_hadamard(record, compressed, rotation);
        tail_destination = record + 0x20U;
    } else if (mode == 1) {
        gx_feature_build_correlation_word(record, compressed, rotation);
        tail_destination = record + 0x2cU;
    } else {
        return 0;
    }

    tail[0] = load_u32(tail_destination + 0U);
    tail[1] = load_u32(tail_destination + 4U);
    tail[2] = load_u32(tail_destination + 8U);
    tail[3] = load_u32(tail_destination + 12U);
    gx_feature_build_tail_bits(tail, compressed, (size_t)value_count,
                               tail_rows, tail_channels);
    store_u32(tail_destination + 0U, tail[0]);
    store_u32(tail_destination + 4U, tail[1]);
    store_u32(tail_destination + 8U, tail[2]);
    store_u32(tail_destination + 12U, tail[3]);
    return 0;
}

int gx_feature_record_descriptor_assemble(
    uint8_t record[GX_FEATURE_RECORD_BYTES],
    int32_t x,
    int32_t y,
    int32_t scale_q16,
    const gx_feature_map_descriptor *orientation,
    const gx_feature_map_descriptor *magnitude,
    const int16_t rotation[GX_FEATURE_ROTATION_TABLE_SIDE *
                           GX_FEATURE_ROTATION_TABLE_SIDE],
    const int32_t profile_words[GX_FEATURE_DESCRIPTOR_PROFILE_WORDS]) {
    int32_t descriptor[128];
    int32_t compact[32];
    int32_t use_mean_gate = 1;
    int32_t profile;
    size_t block;

    if (record == NULL || orientation == NULL || magnitude == NULL ||
        rotation == NULL || profile_words == NULL) {
        return -1;
    }
    profile = profile_words[0];
    if (profile == 9 || profile == 18) return -2;

    memset(descriptor, 0, sizeof(descriptor));
    gx_feature_descriptor_sample(x, y, scale_q16,
                                 (int16_t)(uint16_t)(record[6] |
                                     ((uint16_t)record[7] << 8U)),
                                 orientation, magnitude, descriptor);

    if (gx_feature_descriptor_finalize(
            record, rotation, descriptor, 128, 0, profile,
            profile_words[6], profile_words[7]) != 0) {
        return -1;
    }

    record[0x38U] = 0U;
    {
        const uint16_t x_q8 = (uint16_t)(record[2] |
                                        ((uint16_t)record[3] << 8U));
        const uint16_t y_q8 = (uint16_t)(record[4] |
                                        ((uint16_t)record[5] << 8U));
        const uint16_t width_limit = (uint16_t)(
            (uint32_t)(orientation->width + 0xf6) * UINT32_C(0x100));
        const uint16_t height_limit = (uint16_t)(
            (uint32_t)(orientation->height + 0xf6) * UINT32_C(0x100));
        if (x_q8 > UINT16_C(0x0a00) && x_q8 < width_limit &&
            y_q8 > UINT16_C(0x0a00) && y_q8 < height_limit) {
            use_mean_gate = 0;
        }
    }
    if (gx_feature_descriptor_quality_flag(descriptor, use_mean_gate,
                                           profile) != 0) {
        record[0x38U] = 2U;
    }

    memset(compact, 0, sizeof(compact));
    {
        static const size_t source_offsets[4] = {40U, 48U, 72U, 80U};
        for (block = 0U; block < 4U; ++block) {
            memcpy(compact + block * 8U,
                   descriptor + source_offsets[block],
                   8U * sizeof(compact[0]));
        }
    }
    if (gx_feature_descriptor_finalize(
            record, rotation, compact, 32, 1, profile,
            profile_words[9], profile_words[10]) != 0) {
        return -1;
    }
    return 0;
}
