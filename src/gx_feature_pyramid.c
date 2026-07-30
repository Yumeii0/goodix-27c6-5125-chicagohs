#include "gx5125/feature_pyramid.h"

#include <limits.h>
#include <string.h>

_Static_assert(sizeof(gx_feature_pyramid_image) == 0x20U,
               "feature pyramid descriptor ABI mismatch");
_Static_assert(offsetof(gx_feature_pyramid_image, pixels) == 0x18U,
               "feature pyramid pixel pointer offset mismatch");

#define GX_PROFILE_NINE_LEVEL_MASK UINT32_C(0x07a60e80)
#define GX_PROFILE_SPECIAL_SCALE_MASK UINT32_C(0x07a20c80)

static int profile_bit_is_set(uint32_t profile, uint32_t mask) {
    return profile < 27U && ((mask >> profile) & 1U) != 0U;
}

size_t gx_feature_response_pyramid_level_count(uint32_t profile) {
    return profile_bit_is_set(profile, GX_PROFILE_NINE_LEVEL_MASK) ? 9U : 6U;
}

static int validate_descriptor(const gx_feature_pyramid_image *image,
                               int32_t minimum_elements,
                               int32_t expected_element_bytes) {
    if (image == NULL || image->pixels == NULL || image->width <= 0 ||
        image->height <= 0 || image->element_count < minimum_elements ||
        image->element_bytes != expected_element_bytes) {
        return -1;
    }
    return 0;
}

static void record_call(gx_feature_pyramid_stats *stats, int32_t scale_code) {
    if (stats == NULL) return;
    if (stats->filter_calls == 0U) stats->first_scale_code = scale_code;
    stats->last_scale_code = scale_code;
    ++stats->filter_calls;
}

static void invoke_filter(const gx_feature_pyramid_callbacks *callbacks,
                          gx_feature_pyramid_image *source,
                          gx_feature_pyramid_image *destination,
                          int32_t scale_code,
                          gx_feature_pyramid_stats *stats) {
    const uint64_t parameters =
        callbacks->make_parameters(-1, -1, callbacks->context);
    callbacks->filter(source, destination, parameters, scale_code,
                      -1, -1, callbacks->context);
    record_call(stats, scale_code);
}

int gx_feature_response_pyramid_prepare(
    const gx_feature_pyramid_image *source,
    gx_feature_pyramid_image *const *levels,
    size_t level_capacity,
    int32_t scale_start,
    uint32_t profile,
    const gx_feature_pyramid_callbacks *callbacks,
    gx_feature_pyramid_stats *stats) {
    const size_t level_count = gx_feature_response_pyramid_level_count(profile);
    const uint8_t *input;
    uint16_t *first_level;
    int32_t index;

    if (stats != NULL) memset(stats, 0, sizeof(*stats));
    if (source == NULL || levels == NULL || callbacks == NULL ||
        callbacks->make_parameters == NULL || callbacks->filter == NULL ||
        level_capacity < level_count || source->element_count <= 0 ||
        validate_descriptor(source, source->element_count, 1) != 0 ||
        levels[0] == NULL ||
        validate_descriptor(levels[0], source->element_count, 2) != 0) {
        return -1;
    }
    if ((size_t)source->element_count > SIZE_MAX / sizeof(uint16_t)) return -1;
    for (index = 1; index < (int32_t)level_count; ++index) {
        if (levels[index] == NULL || levels[index]->pixels == NULL ||
            levels[index]->element_bytes != 2) {
            return -1;
        }
    }

    input = (const uint8_t *)source->pixels;
    first_level = (uint16_t *)levels[0]->pixels;
    for (index = 0; index < source->element_count; ++index) {
        first_level[index] = (uint16_t)((uint16_t)input[index] << 8);
    }

    if (stats != NULL) stats->level_count = (uint32_t)level_count;

    if (profile == 9U || profile == 18U) {
        invoke_filter(callbacks, levels[0], levels[0], 200, stats);
        for (index = 1; index < 9; ++index) {
            invoke_filter(callbacks, levels[index - 1], levels[index],
                          200 + index, stats);
        }
    } else if (profile_bit_is_set(profile, GX_PROFILE_SPECIAL_SCALE_MASK)) {
        invoke_filter(callbacks, levels[0], levels[1], 301, stats);
        invoke_filter(callbacks, levels[0], levels[0], 300, stats);
        for (index = 2; index < 9; ++index) {
            invoke_filter(callbacks, levels[index - 1], levels[index],
                          300 + index, stats);
        }
    } else {
        invoke_filter(callbacks, levels[0], levels[0], scale_start, stats);
        for (index = 1; index < 6; ++index) {
            invoke_filter(callbacks, levels[index - 1], levels[index],
                          scale_start + index, stats);
        }
    }
    return 0;
}
