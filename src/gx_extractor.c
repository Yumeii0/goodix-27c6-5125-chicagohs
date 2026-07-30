#include "gx5125/extractor.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "gx5125/feature_aux_metadata.h"
#include "gx5125/feature_candidate.h"
#include "gx5125/feature_compact.h"
#include "gx5125/feature_descriptor_assembly.h"
#include "gx5125/feature_descriptor_lifecycle.h"
#include "gx5125/feature_detector.h"
#include "gx5125/feature_filter.h"
#include "gx5125/feature_global_postprocess.h"
#include "gx5125/feature_object.h"
#include "gx5125/feature_postprocess.h"
#include "gx5125/feature_pyramid.h"
#include "gx5125/feature_quality_post.h"
#include "gx5125/feature_record.h"
#include "gx5125/feature_response.h"
#include "gx5125/feature_root.h"

#define GX5125_PROFILE24_LEVELS 9U
#define GX5125_PROFILE24_TEMP_DESCRIPTORS 5U
#define GX5125_PROFILE24_DETECTOR_LIMIT 300U
#define GX5125_PROFILE24_RETRY_THRESHOLD 60U
#define GX5125_INLINE_COMPACT_BYTES 200U
#define GX5125_QUALITY_BUFFER_BYTES 192U

#define GX_OBJECT_RECORD_COUNT_OFFSET 0x0f0U
#define GX_OBJECT_RECORD_ARRAY_OFFSET 0x0f8U
#define GX_OBJECT_ZERO_CLASS_OFFSET 0x108U
#define GX_OBJECT_QUALITY_OFFSET 0x164U
#define GX_OBJECT_PACKED_MAP_OFFSET 0x130U
#define GX_OBJECT_AUX_MAP_OFFSET 0x148U
#define GX_OBJECT_METRIC_OFFSET 0x140U
#define GX_OBJECT_POSITIVE_PERCENT_OFFSET 0x158U
#define GX_OBJECT_MAP_STATE_OFFSET 0x15cU
#define GX_OBJECT_NONPOSITIVE_OFFSET 0x160U
#define GX_OBJECT_MASK_MAP_OFFSET 0x218U
#define GX_OBJECT_POST_MAP_OFFSET 0x220U
#define GX_OBJECT_GRAY_MAP_OFFSET 0x228U

struct gx5125_extractor {
    gx5125_extractor_config config;
    gx_feature_aux_metadata_state aux_state;
    uint8_t visited[GX5125_EXTRACTOR_PIXELS];
    int16_t rotation[128U * 128U];
    int32_t rank_pairs[GX5125_PROFILE24_DETECTOR_LIMIT * 2U];
    int32_t metadata[GX5125_PROFILE24_DETECTOR_LIMIT * 6U];
    uint8_t records[GX5125_PROFILE24_DETECTOR_LIMIT *
                    GX5125_EXTRACTOR_RECORD_BYTES];
};

struct gx5125_feature {
    void *object;
};

static const uint32_t profile_template_a[4] = {
    UINT32_C(0x0001adfe), UINT32_C(0x00000020),
    UINT32_C(0x00000004), UINT32_C(0x00000000)
};

static const uint32_t profile_template_b[4] = {
    UINT32_C(0x00000020), UINT32_C(0x00000001),
    UINT32_C(0x00000000), UINT32_C(0x00000078)
};

static uint32_t read_u32(const uint8_t *base, size_t offset) {
    uint32_t value = 0U;
    memcpy(&value, base + offset, sizeof(value));
    return value;
}

static void write_u32(uint8_t *base, size_t offset, uint32_t value) {
    memcpy(base + offset, &value, sizeof(value));
}

static void *read_pointer(const uint8_t *base, size_t offset) {
    void *value = NULL;
    memcpy(&value, base + offset, sizeof(value));
    return value;
}

static void write_pointer(uint8_t *base, size_t offset, void *value) {
    memcpy(base + offset, &value, sizeof(value));
}

static void *object_allocate(size_t bytes) {
    return bytes == 0U ? NULL : malloc(bytes);
}

static void object_deallocate(void *pointer) {
    free(pointer);
}

static uint32_t object_release_descriptor(
    gx_feature_pyramid_image **descriptor) {
    return gx_feature_descriptor_release(descriptor);
}

static const gx_feature_object_callbacks object_callbacks = {
    object_allocate, object_deallocate, object_release_descriptor
};

static int descriptor_release(gx_feature_pyramid_image **descriptor) {
    if (descriptor == NULL || *descriptor == NULL) return GX5125_OK;
    return gx_feature_descriptor_release(descriptor) == 0U ?
        GX5125_OK : GX5125_ERR_INTERNAL;
}

static int apply_filter(const gx_feature_pyramid_image *source,
                        gx_feature_pyramid_image *destination,
                        int32_t scale_code) {
    gx_feature_filter_kernel kernel;
    gx_feature_filter_stats stats;

    if (source == NULL || destination == NULL || source->pixels == NULL ||
        destination->pixels == NULL || source->width != destination->width ||
        source->height != destination->height || source->element_bytes != 2 ||
        destination->element_bytes != 2) {
        return GX5125_ERR_FILTER;
    }

    memset(&kernel, 0, sizeof(kernel));
    memset(&stats, 0, sizeof(stats));
    if (scale_code == 6) {
        if (gx_feature_filter_image_prepare_code6_u16_reflect101(
                source, destination, &stats) != 0) {
            return GX5125_ERR_FILTER;
        }
        return GX5125_OK;
    }
    if (scale_code == 0 || scale_code == 1 || scale_code == 7) {
        if (gx_feature_filter_auxiliary_kernel(scale_code, &kernel) != 0) {
            return GX5125_ERR_FILTER;
        }
    } else if (gx_feature_filter_profile24_kernel(scale_code, &kernel) != 0) {
        return GX5125_ERR_FILTER;
    }
    if (gx_feature_filter_u16_q16_reflect101(
            source, destination, kernel.coefficients,
            kernel.coefficient_count, &stats) != 0 ||
        stats.kernel_length != kernel.coefficient_count ||
        stats.kernel_sum != kernel.coefficient_sum) {
        return GX5125_ERR_FILTER;
    }
    return GX5125_OK;
}

static int root_filter_adapter(
    const gx_feature_pyramid_image *source,
    gx_feature_pyramid_image *destination,
    int32_t scale_code, void *context) {
    (void)context;
    return apply_filter(source, destination, scale_code);
}

static gx_feature_pyramid_image *prepare_optional_image_checked(
    const gx_feature_pyramid_image *source) {
    gx_feature_pyramid_image *destination;
    uint8_t workspace[GX_FEATURE_OPTIONAL_WORKSPACE_BYTES];
    if (source == NULL || source->pixels == NULL || source->width <= 0 ||
        source->height <= 0 || source->element_bytes != 1) {
        return NULL;
    }
    destination = gx_feature_descriptor_allocate(
        source->width, source->height, source->element_bytes);
    if (destination == NULL) return NULL;
    memset(workspace, 0, sizeof(workspace));
    if (gx_feature_optional_orientation_kernel(
            (const uint8_t *)source->pixels, workspace,
            source->width, source->height,
            GX_FEATURE_OPTIONAL_RADIUS) != 0 ||
        gx_feature_optional_reconstruct_kernel(
            workspace, (const uint8_t *)source->pixels,
            (uint8_t *)destination->pixels,
            source->width, source->height) != 0) {
        (void)gx_feature_descriptor_release(&destination);
        return NULL;
    }
    return destination;
}

typedef struct pyramid_filter_context {
    int status;
} pyramid_filter_context;

static uint64_t pyramid_parameters(int32_t first, int32_t second,
                                   void *context) {
    (void)context;
    return ((uint64_t)(uint32_t)second << 32U) | (uint64_t)(uint32_t)first;
}

static void pyramid_filter(gx_feature_pyramid_image *source,
                           gx_feature_pyramid_image *destination,
                           uint64_t parameters,
                           int32_t scale_code,
                           int32_t option_a,
                           int32_t option_b,
                           void *context) {
    pyramid_filter_context *filter_context =
        (pyramid_filter_context *)context;
    if (filter_context == NULL || filter_context->status != GX5125_OK) return;
    if (parameters != UINT64_C(0xffffffffffffffff) ||
        option_a != -1 || option_b != -1) {
        filter_context->status = GX5125_ERR_FILTER;
        return;
    }
    filter_context->status = apply_filter(source, destination, scale_code);
}

static int16_t round_angle_q8(int16_t angle) {
    int32_t value = (int32_t)angle;
    if (value < 0) {
        value = -(((-value) + 0x80) & ~0xff);
    } else {
        value = (value + 0x80) & ~0xff;
    }
    return (int16_t)value;
}

static void finalize_profile24_record(
    uint8_t record[GX5125_EXTRACTOR_RECORD_BYTES], int32_t image_height) {
    uint16_t x;
    uint16_t y;
    int16_t angle;
    uint32_t edge_class = 0U;

    memcpy(&x, record + 0x02U, sizeof(x));
    memcpy(&y, record + 0x04U, sizeof(y));
    memcpy(&angle, record + 0x06U, sizeof(angle));
    x = (uint16_t)((x + 8U) & UINT16_C(0xfff0));
    y = (uint16_t)((y + 8U) & UINT16_C(0xfff0));
    angle = round_angle_q8(angle);
    if (y < UINT16_C(0x1400)) {
        edge_class = 1U;
    } else if (y >= (uint16_t)((image_height << 8) + 0xec00)) {
        edge_class = 2U;
    }
    memcpy(record + 0x02U, &x, sizeof(x));
    memcpy(record + 0x04U, &y, sizeof(y));
    memcpy(record + 0x06U, &angle, sizeof(angle));
    memcpy(record + 0x0cU, &edge_class, sizeof(edge_class));
}

static int generate_profile24(
    gx5125_extractor *extractor,
    const gx_feature_pyramid_image *mask,
    const gx_feature_pyramid_image *gray,
    uint8_t *output_records,
    uint32_t *output_count,
    const uint32_t parameters[GX_FEATURE_PROFILE24_PARAMETER_WORDS]) {
    gx_feature_pyramid_image *levels[GX5125_PROFILE24_LEVELS] = {0};
    gx_feature_pyramid_image *smooth_a = NULL;
    gx_feature_pyramid_image *smooth_b = NULL;
    gx_feature_pyramid_image *laplacian = NULL;
    gx_feature_pyramid_image *magnitude = NULL;
    gx_feature_pyramid_image *orientation = NULL;
    gx_feature_u16_map level_maps[GX5125_PROFILE24_LEVELS];
    gx_feature_pyramid_callbacks pyramid_callbacks;
    gx_feature_pyramid_stats pyramid_stats;
    pyramid_filter_context filter_context;
    uint32_t detected_count = 0U;
    uint32_t output_capacity;
    size_t pixel_count;
    size_t index;
    int status = GX5125_ERR_GENERATOR;

    if (extractor == NULL || mask == NULL || gray == NULL ||
        output_records == NULL || output_count == NULL || parameters == NULL ||
        mask->pixels == NULL || gray->pixels == NULL ||
        mask->width != gray->width || mask->height != gray->height ||
        mask->element_bytes != 1 || gray->element_bytes != 1) {
        return GX5125_ERR_ARGUMENT;
    }
    *output_count = 0U;
    output_capacity = parameters[1];
    if (parameters[0] != GX_FEATURE_PROFILE24_ID ||
        output_capacity == 0U || output_capacity > GX5125_EXTRACTOR_MAX_RECORDS ||
        parameters[2] != GX5125_PROFILE24_DETECTOR_LIMIT ||
        parameters[3] != GX_FEATURE_PROFILE24_EDGE_THRESHOLD ||
        parameters[4] == 0U) {
        return GX5125_ERR_GENERATOR;
    }
    pixel_count = (size_t)gray->width * (size_t)gray->height;
    if (pixel_count != GX5125_EXTRACTOR_PIXELS) return GX5125_ERR_DIMENSIONS;

    for (index = 0U; index < GX5125_PROFILE24_LEVELS; ++index) {
        levels[index] = gx_feature_descriptor_allocate(
            gray->width, gray->height, 2);
        if (levels[index] == NULL) {
            status = GX5125_ERR_MEMORY;
            goto cleanup;
        }
    }
    smooth_a = gx_feature_descriptor_allocate(mask->width, mask->height, 2);
    smooth_b = gx_feature_descriptor_allocate(mask->width, mask->height, 2);
    laplacian = gx_feature_descriptor_allocate(mask->width, mask->height, 4);
    magnitude = gx_feature_descriptor_allocate(gray->width, gray->height, 4);
    orientation = gx_feature_descriptor_allocate(gray->width, gray->height, 2);
    if (smooth_a == NULL || smooth_b == NULL || laplacian == NULL ||
        magnitude == NULL || orientation == NULL) {
        status = GX5125_ERR_MEMORY;
        goto cleanup;
    }

    memset(&pyramid_callbacks, 0, sizeof(pyramid_callbacks));
    memset(&pyramid_stats, 0, sizeof(pyramid_stats));
    filter_context.status = GX5125_OK;
    pyramid_callbacks.make_parameters = pyramid_parameters;
    pyramid_callbacks.filter = pyramid_filter;
    pyramid_callbacks.context = &filter_context;
    if (gx_feature_response_pyramid_prepare(
            gray, levels, GX5125_PROFILE24_LEVELS, 0,
            GX_FEATURE_PROFILE24_ID, &pyramid_callbacks,
            &pyramid_stats) != 0 ||
        filter_context.status != GX5125_OK ||
        pyramid_stats.level_count != GX5125_PROFILE24_LEVELS) {
        status = filter_context.status != GX5125_OK ?
            filter_context.status : GX5125_ERR_GENERATOR;
        goto cleanup;
    }

    for (index = 0U; index < pixel_count; ++index) {
        ((uint16_t *)smooth_a->pixels)[index] =
            (uint16_t)((uint16_t)((const uint8_t *)mask->pixels)[index] << 8);
    }
    if (apply_filter(smooth_a, smooth_a, 0) != GX5125_OK ||
        apply_filter(smooth_a, smooth_b, 1) != GX5125_OK) {
        status = GX5125_ERR_FILTER;
        goto cleanup;
    }
    for (index = 0U; index < pixel_count; ++index) {
        ((int32_t *)laplacian->pixels)[index] =
            (int32_t)((const uint16_t *)smooth_a->pixels)[index] * 10 -
            (int32_t)((const uint16_t *)smooth_b->pixels)[index] * 9;
    }

    memset(extractor->visited, 0, sizeof(extractor->visited));
    memset(magnitude->pixels, 0, (size_t)magnitude->element_count);
    memset(orientation->pixels, 0, (size_t)orientation->element_count);
    if (gx_feature_build_response_map(
            (const int32_t *)laplacian->pixels,
            laplacian->width, laplacian->height,
            (int32_t *)magnitude->pixels,
            (int16_t *)orientation->pixels) != 0) {
        status = GX5125_ERR_GENERATOR;
        goto cleanup;
    }

    memset(level_maps, 0, sizeof(level_maps));
    for (index = 0U; index < GX5125_PROFILE24_LEVELS; ++index) {
        level_maps[index].width = levels[index]->width;
        level_maps[index].height = levels[index]->height;
        level_maps[index].pixels = (const uint16_t *)levels[index]->pixels;
    }
    memset(extractor->rank_pairs, 0, sizeof(extractor->rank_pairs));
    memset(extractor->metadata, 0, sizeof(extractor->metadata));
    memset(extractor->records, 0, sizeof(extractor->records));
    if (gx_feature_detect_candidates(
            (const gx_feature_map_descriptor *)(const void *)gray,
            extractor->records,
            extractor->rank_pairs,
            extractor->metadata,
            &detected_count,
            level_maps,
            GX5125_PROFILE24_LEVELS,
            extractor->visited,
            (const gx_feature_map_descriptor *)(const void *)magnitude,
            (const gx_feature_map_descriptor *)(const void *)orientation,
            (int32_t)parameters[3],
            (int32_t)parameters[2],
            parameters[0],
            (const gx_feature_detector_parameters *)(const void *)parameters) != 0) {
        status = GX5125_ERR_GENERATOR;
        goto cleanup;
    }
    if (detected_count <= GX5125_PROFILE24_RETRY_THRESHOLD) {
        status = GX5125_ERR_LOW_FEATURE_COUNT;
        goto cleanup;
    }
    if (detected_count > output_capacity ||
        detected_count > GX5125_PROFILE24_DETECTOR_LIMIT) {
        status = GX5125_ERR_GENERATOR;
        goto cleanup;
    }

    memcpy(output_records, extractor->records,
           (size_t)detected_count * GX5125_EXTRACTOR_RECORD_BYTES);
    for (index = 0U; index < detected_count; ++index) {
        const int32_t candidate_index = extractor->rank_pairs[index * 2U + 1U];
        uint8_t *record = output_records +
            index * GX5125_EXTRACTOR_RECORD_BYTES;
        if (candidate_index < 0 ||
            candidate_index >= (int32_t)GX5125_PROFILE24_DETECTOR_LIMIT) {
            status = GX5125_ERR_GENERATOR;
            goto cleanup;
        }
        if (gx_feature_record_descriptor_assemble(
                record,
                extractor->metadata[(size_t)candidate_index * 6U],
                extractor->metadata[(size_t)candidate_index * 6U + 1U],
                (int32_t)parameters[5],
                (const gx_feature_map_descriptor *)(const void *)orientation,
                (const gx_feature_map_descriptor *)(const void *)magnitude,
                extractor->rotation,
                (const int32_t *)(const void *)parameters) != 0) {
            status = GX5125_ERR_GENERATOR;
            goto cleanup;
        }
        finalize_profile24_record(record, gray->height);
    }
    *output_count = detected_count;
    status = GX5125_OK;

cleanup:
    for (index = 0U; index < GX5125_PROFILE24_LEVELS; ++index) {
        (void)descriptor_release(&levels[index]);
    }
    (void)descriptor_release(&smooth_a);
    (void)descriptor_release(&smooth_b);
    (void)descriptor_release(&laplacian);
    (void)descriptor_release(&magnitude);
    (void)descriptor_release(&orientation);
    if (status != GX5125_OK) *output_count = 0U;
    return status;
}

static int compact_expand_and_pack(uint8_t *object,
                                   const gx_feature_pyramid_image *mask,
                                   uint32_t map_mode) {
    gx_feature_pyramid_image *expanded = NULL;
    gx_feature_pyramid_image *packed = NULL;
    gx_feature_post_map map;
    int32_t output_width = 0;
    int32_t output_height = 0;
    int32_t desired_width;
    int32_t desired_height;
    size_t compact_width;
    size_t compact_height;
    size_t compact_bytes;
    size_t expanded_bytes;
    size_t packed_bytes;
    int status = GX5125_ERR_POSTPROCESS;

    if (object == NULL || mask == NULL || mask->pixels == NULL) {
        return GX5125_ERR_ARGUMENT;
    }
    if (gx_feature_compact_build_quarter(
            (const uint8_t *)mask->pixels, mask->width, mask->height,
            mask->width, object + 0x028U) != 0) {
        return GX5125_ERR_POSTPROCESS;
    }
    desired_width = map_mode == 1U ? mask->width >> 1 : mask->width;
    desired_height = map_mode == 1U ? mask->height >> 1 : mask->height;
    if (desired_width <= 0 || desired_height <= 0) {
        return GX5125_ERR_DIMENSIONS;
    }
    expanded = gx_feature_descriptor_allocate(
        desired_width, desired_height, 1);
    if (expanded == NULL) return GX5125_ERR_MEMORY;
    compact_width = ((size_t)mask->width + 3U) / 4U;
    compact_height = ((size_t)mask->height + 3U) / 4U;
    compact_bytes = gx_feature_compact_linear_bytes(
        compact_width * compact_height);
    expanded_bytes = (size_t)desired_width * (size_t)desired_height;
    if (gx_feature_compact_expand(
            object + 0x028U, compact_bytes, (int32_t)map_mode,
            mask->width, mask->height,
            (uint8_t *)expanded->pixels, expanded_bytes,
            &output_width, &output_height) != 0 ||
        output_width != desired_width || output_height != desired_height) {
        goto cleanup;
    }
    packed_bytes = gx_feature_post_packed_bytes(
        expanded->width, expanded->height);
    packed = gx_feature_descriptor_allocate(
        (expanded->width + 7) / 8, expanded->height, 1);
    if (packed == NULL) {
        status = GX5125_ERR_MEMORY;
        goto cleanup;
    }
    map.width = expanded->width;
    map.height = expanded->height;
    map.pixels = (uint8_t *)expanded->pixels;
    if (gx_feature_post_pack_map(
            &map, (uint8_t *)packed->pixels, packed_bytes) != 0) {
        goto cleanup;
    }
    write_pointer(object, GX_OBJECT_PACKED_MAP_OFFSET, packed);
    packed = NULL;
    status = GX5125_OK;

cleanup:
    (void)descriptor_release(&packed);
    (void)descriptor_release(&expanded);
    return status;
}

void gx5125_extractor_default_config(gx5125_extractor_config *config) {
    if (config == NULL) return;
    config->map_mode = 0U;
    config->optional_image_enabled = 1U;
    config->enroll_capacity = GX5125_EXTRACTOR_MAX_RECORDS;
    config->identify_capacity = GX5125_EXTRACTOR_MAX_RECORDS;
}

gx5125_extractor *gx5125_extractor_create(
    const gx5125_extractor_config *config) {
    gx5125_extractor_config resolved;
    gx5125_extractor *extractor;

    gx5125_extractor_default_config(&resolved);
    if (config != NULL) resolved = *config;
    if (resolved.map_mode > 1U || resolved.optional_image_enabled == 0U ||
        resolved.enroll_capacity == 0U ||
        resolved.enroll_capacity > GX5125_EXTRACTOR_MAX_RECORDS ||
        resolved.identify_capacity == 0U ||
        resolved.identify_capacity > GX5125_EXTRACTOR_MAX_RECORDS) {
        return NULL;
    }
    extractor = (gx5125_extractor *)calloc(1U, sizeof(*extractor));
    if (extractor == NULL) return NULL;
    extractor->config = resolved;
    gx_feature_aux_metadata_state_reset(&extractor->aux_state);
    gx_feature_descriptor_rotation_table_init(extractor->rotation);
    return extractor;
}

void gx5125_extractor_destroy(gx5125_extractor *extractor) {
    if (extractor == NULL) return;
    memset(extractor, 0, sizeof(*extractor));
    free(extractor);
}

void gx5125_extractor_reset_state(gx5125_extractor *extractor) {
    if (extractor == NULL) return;
    gx_feature_aux_metadata_state_reset(&extractor->aux_state);
}

int gx5125_extractor_extract_gray(
    gx5125_extractor *extractor,
    const uint8_t gray_pixels[GX5125_EXTRACTOR_PIXELS],
    uint32_t quality,
    uint32_t coverage,
    gx5125_extract_mode mode,
    const uint8_t auxiliary[GX5125_EXTRACTOR_AUX_BYTES],
    gx5125_feature **feature,
    gx5125_extract_stats *stats) {
    static const uint8_t zero_auxiliary[GX5125_EXTRACTOR_AUX_BYTES] = {0};
    const uint8_t *aux_input = auxiliary != NULL ? auxiliary : zero_auxiliary;
    gx_feature_aux_metadata_state state_backup;
    gx_feature_aux_metadata_stats aux_stats;
    gx_feature_pyramid_image *gray = NULL;
    gx_feature_pyramid_image *prepared = NULL;
    gx_feature_pyramid_image *filtered = NULL;
    gx_feature_pyramid_image *u16_input = NULL;
    gx_feature_pyramid_image *mask = NULL;
    gx_feature_pyramid_image *optional_gray = NULL;
    gx5125_feature *result_feature = NULL;
    uint8_t *object = NULL;
    void *aux_temporary = NULL;
    uint32_t parameters[GX_FEATURE_PROFILE24_PARAMETER_WORDS];
    uint32_t packed_first = 0U;
    uint32_t packed_second = 0U;
    uint32_t auxiliary_words[6] = {0};
    uint32_t context_words[GX_FEATURE_AUX_CONTEXT_WORDS] = {0};
    uint32_t mask_coverage_q16 = 0U;
    uint32_t capacity;
    uint32_t count;
    int32_t metric_state = 0;
    int aux_flags_result;
    int status = GX5125_ERR_INTERNAL;
    size_t index;
    uint8_t first_quality[GX5125_QUALITY_BUFFER_BYTES];
    int8_t second_quality[GX5125_QUALITY_BUFFER_BYTES];

    if (stats != NULL) memset(stats, 0, sizeof(*stats));
    if (extractor == NULL || gray_pixels == NULL || feature == NULL ||
        *feature != NULL || quality > 100U || coverage > 100U ||
        (mode != GX5125_EXTRACT_ENROLL &&
         mode != GX5125_EXTRACT_IDENTIFY)) {
        return GX5125_ERR_ARGUMENT;
    }
    state_backup = extractor->aux_state;
    memset(&aux_stats, 0, sizeof(aux_stats));

    capacity = mode == GX5125_EXTRACT_ENROLL ?
        extractor->config.enroll_capacity :
        extractor->config.identify_capacity;
    if (gx_feature_profile24_parameters_build(
            parameters, profile_template_a, profile_template_b,
            capacity, quality, coverage) != 0) {
        return GX5125_ERR_INTERNAL;
    }

    aux_flags_result = gx_feature_root_aux_flags_parse(
        gray_pixels, (int32_t)GX5125_EXTRACTOR_ROWS,
        (int32_t)GX5125_EXTRACTOR_COLUMNS,
        &metric_state, (uint8_t **)(void *)&aux_temporary);
    if (aux_flags_result < 0) {
        status = GX5125_ERR_AUX_METADATA;
        goto cleanup;
    }
    if (aux_flags_result == 1) {
        status = GX5125_ERR_UNSUPPORTED_AUX_OVERLAY;
        goto cleanup;
    }
    gx_feature_root_packed_state_decode(
        (uint32_t)metric_state, &packed_first, &packed_second);
    gx_feature_root_aux_state_decode(aux_input, auxiliary_words);
    if ((int32_t)auxiliary_words[3] > 0) {
        status = GX5125_ERR_UNSUPPORTED_AUX_OVERLAY;
        goto cleanup;
    }

    gray = gx_feature_descriptor_allocate(
        (int32_t)GX5125_EXTRACTOR_COLUMNS,
        (int32_t)GX5125_EXTRACTOR_ROWS, 1);
    if (gray == NULL) {
        status = GX5125_ERR_MEMORY;
        goto cleanup;
    }
    memcpy(gray->pixels, gray_pixels, GX5125_EXTRACTOR_PIXELS);

    result_feature = (gx5125_feature *)calloc(1U, sizeof(*result_feature));
    if (result_feature == NULL ||
        gx_feature_object_create(
            &result_feature->object,
            extractor->config.enroll_capacity > extractor->config.identify_capacity ?
                extractor->config.enroll_capacity :
                extractor->config.identify_capacity,
            &object_callbacks) != 0 ||
        result_feature->object == NULL) {
        status = GX5125_ERR_MEMORY;
        goto cleanup;
    }
    object = (uint8_t *)result_feature->object;
    write_u32(object, 0x000U, GX5125_EXTRACTOR_COLUMNS);
    write_u32(object, 0x004U, GX5125_EXTRACTOR_ROWS);
    write_u32(object, 0x10cU, quality);
    write_u32(object, 0x110U, coverage);

    prepared = gx_feature_descriptor_allocate(gray->width, gray->height, 1);
    filtered = gx_feature_descriptor_allocate(gray->width, gray->height, 2);
    u16_input = gx_feature_descriptor_allocate(gray->width, gray->height, 2);
    mask = gx_feature_descriptor_allocate(gray->width, gray->height, 1);
    if (prepared == NULL || filtered == NULL || u16_input == NULL ||
        mask == NULL) {
        status = GX5125_ERR_MEMORY;
        goto cleanup;
    }
    memset(mask->pixels, 0, (size_t)mask->element_count);
    if (gx_feature_root_mask_prepare(
            gray, mask, 1, UINT16_C(0x006e), UINT8_C(1),
            gx_feature_descriptor_allocate,
            gx_feature_descriptor_release,
            root_filter_adapter, NULL,
            &mask_coverage_q16) != 0) {
        status = GX5125_ERR_MASK;
        goto cleanup;
    }

    optional_gray = prepare_optional_image_checked(gray);
    if (optional_gray == NULL) {
        status = GX5125_ERR_OPTIONAL_IMAGE;
        goto cleanup;
    }

    for (index = 0U; index < GX5125_EXTRACTOR_PIXELS; ++index) {
        ((uint16_t *)u16_input->pixels)[index] =
            (uint16_t)((uint16_t)((const uint8_t *)gray->pixels)[index] << 8);
    }
    if (apply_filter(u16_input, filtered, 6) != GX5125_OK) {
        status = GX5125_ERR_FILTER;
        goto cleanup;
    }
    for (index = 0U; index < GX5125_EXTRACTOR_PIXELS; ++index) {
        ((uint8_t *)prepared->pixels)[index] =
            ((const uint8_t *)filtered->pixels)[index * 2U + 1U];
    }

    if (gx_feature_root_feature_maps_prepare(
            (int32_t)extractor->config.map_mode,
            prepared, optional_gray, mask, object,
            gx_feature_descriptor_allocate,
            gx_feature_descriptor_release) != 0) {
        status = GX5125_ERR_FEATURE_MAP;
        goto cleanup;
    }

    if (gx_feature_object_aux_map_ensure(
            object, GX5125_EXTRACTOR_PIXELS, &object_callbacks) != 0) {
        status = GX5125_ERR_MEMORY;
        goto cleanup;
    }
    context_words[0] = GX5125_EXTRACTOR_ROWS;
    context_words[1] = GX5125_EXTRACTOR_COLUMNS;
    context_words[2] = GX_FEATURE_PROFILE24_ID;
    context_words[3] = auxiliary_words[1];
    context_words[4] = packed_second;
    context_words[5] = packed_first;
    context_words[6] = auxiliary_words[0];
    context_words[7] = auxiliary_words[2];
    context_words[8] = coverage;
    context_words[9] = auxiliary_words[4];
    context_words[10] = extractor->config.map_mode;
    if (gx_feature_root_aux_metadata_prepare_profile24(
            optional_gray, mask, context_words, &metric_state,
            (uint8_t *)read_pointer(object, GX_OBJECT_AUX_MAP_OFFSET),
            &extractor->aux_state, &aux_stats) != 0) {
        status = GX5125_ERR_AUX_METADATA;
        goto cleanup;
    }

    status = generate_profile24(
        extractor, optional_gray, prepared,
        (uint8_t *)read_pointer(object, GX_OBJECT_RECORD_ARRAY_OFFSET),
        (uint32_t *)(void *)(object + GX_OBJECT_RECORD_COUNT_OFFSET),
        parameters);
    if (status != GX5125_OK) goto cleanup;

    if (gx_feature_object_output_maps_ensure(
            object, GX5125_EXTRACTOR_PIXELS, &object_callbacks) != 0) {
        status = GX5125_ERR_MEMORY;
        goto cleanup;
    }
    count = read_u32(object, GX_OBJECT_RECORD_COUNT_OFFSET);
    if (gx_feature_record_window_quality(
            (const uint8_t *)gray->pixels,
            (const uint8_t *)mask->pixels,
            gray->width, gray->height, gray->width, mask->width,
            (const uint8_t *)read_pointer(object, GX_OBJECT_RECORD_ARRAY_OFFSET),
            count, object + GX_OBJECT_QUALITY_OFFSET) != 0) {
        status = GX5125_ERR_POSTPROCESS;
        goto cleanup;
    }
    memcpy(read_pointer(object, GX_OBJECT_GRAY_MAP_OFFSET),
           gray->pixels, GX5125_EXTRACTOR_PIXELS);
    (void)gx_feature_global_postprocess(
        object, (int32_t)auxiliary_words[0], (int32_t)GX_FEATURE_PROFILE24_ID);

    {
        gx_feature_post_map post_mask;
        uint8_t *records =
            (uint8_t *)read_pointer(object, GX_OBJECT_RECORD_ARRAY_OFFSET);
        count = read_u32(object, GX_OBJECT_RECORD_COUNT_OFFSET);
        post_mask.width = mask->width;
        post_mask.height = mask->height;
        post_mask.pixels = (uint8_t *)mask->pixels;
        if (gx_feature_post_prune_mask(records, &count, &post_mask) != 0) {
            status = GX5125_ERR_POSTPROCESS;
            goto cleanup;
        }
        write_u32(object, GX_OBJECT_RECORD_COUNT_OFFSET, count);
    }

    status = compact_expand_and_pack(
        object, mask, extractor->config.map_mode);
    if (status != GX5125_OK) goto cleanup;
    write_u32(object, GX_OBJECT_METRIC_OFFSET, (uint32_t)metric_state);

    count = read_u32(object, GX_OBJECT_RECORD_COUNT_OFFSET);
    for (index = 0U; index < count; ++index) {
        gx_feature_record_apply_mode(
            (uint8_t *)read_pointer(object, GX_OBJECT_RECORD_ARRAY_OFFSET) +
                index * GX5125_EXTRACTOR_RECORD_BYTES,
            mode == GX5125_EXTRACT_IDENTIFY ? 1 : 0);
    }
    {
        uint32_t zero_count = 0U;
        if (gx_feature_post_partition_records(
                (uint8_t *)read_pointer(object, GX_OBJECT_RECORD_ARRAY_OFFSET),
                object + GX_OBJECT_QUALITY_OFFSET,
                count, &zero_count) != 0) {
            status = GX5125_ERR_POSTPROCESS;
            goto cleanup;
        }
        write_u32(object, GX_OBJECT_ZERO_CLASS_OFFSET, zero_count);
    }

    memset(first_quality, 0, sizeof(first_quality));
    memset(second_quality, 0, sizeof(second_quality));
    if (read_u32(object, GX_OBJECT_MAP_STATE_OFFSET) == 1U) {
        uint8_t *records =
            (uint8_t *)read_pointer(object, GX_OBJECT_RECORD_ARRAY_OFFSET);
        if (gx_feature_record_window_quality(
                (const uint8_t *)optional_gray->pixels,
                (const uint8_t *)mask->pixels,
                optional_gray->width, optional_gray->height,
                optional_gray->width, mask->width,
                records, count, first_quality) != 0 ||
            gx_feature_neighbor_quality(
                records, count, 1, object + GX_OBJECT_QUALITY_OFFSET,
                (const int8_t *)first_quality, second_quality) != 0) {
            status = GX5125_ERR_POSTPROCESS;
            goto cleanup;
        }
        for (index = 0U; index < count; ++index) {
            uint8_t *record = records +
                index * GX5125_EXTRACTOR_RECORD_BYTES;
            if (record[0x38U] != 0U &&
                object[GX_OBJECT_QUALITY_OFFSET + index] > 30U &&
                second_quality[index] > 65 && first_quality[index] > 65U &&
                record[0x39U] > 35U) {
                record[0x38U] = 0U;
            }
        }
    } else if (gx_feature_neighbor_quality(
                   (uint8_t *)read_pointer(object, GX_OBJECT_RECORD_ARRAY_OFFSET),
                   count, 0, object + GX_OBJECT_QUALITY_OFFSET,
                   (const int8_t *)first_quality, second_quality) != 0) {
        status = GX5125_ERR_POSTPROCESS;
        goto cleanup;
    }

    memcpy(read_pointer(object, GX_OBJECT_MASK_MAP_OFFSET),
           mask->pixels, GX5125_EXTRACTOR_PIXELS);
    write_u32(object, 0x13cU, 0U);
    write_u32(object, 0x100U, 0U);
    write_u32(object, 0x104U, 0U);
    write_u32(object, 0x114U, 0U);
    write_u32(object, 0x11cU, 0U);
    write_u32(object, 0x120U, 0U);
    write_u32(object, 0x124U, 0U);
    write_u32(object, 0x128U, 0U);

    if (stats != NULL) {
        stats->record_count = read_u32(object, GX_OBJECT_RECORD_COUNT_OFFSET);
        stats->quality = quality;
        stats->coverage = coverage;
        stats->mask_coverage_q16 = mask_coverage_q16;
        stats->positive_map_percent =
            read_u32(object, GX_OBJECT_POSITIVE_PERCENT_OFFSET);
        stats->map_state = read_u32(object, GX_OBJECT_MAP_STATE_OFFSET);
        stats->nonpositive_record_count =
            read_u32(object, GX_OBJECT_NONPOSITIVE_OFFSET);
        stats->auxiliary_metric = metric_state;
        stats->auxiliary_history_count = extractor->aux_state.history_count;
        stats->auxiliary_class_one_pixels = aux_stats.class_one_pixels;
        stats->auxiliary_class_two_pixels = aux_stats.class_two_pixels;
        stats->auxiliary_dark_hole_pixels = aux_stats.dark_hole_pixels;
        stats->auxiliary_anomaly_flag = aux_stats.anomaly_flag;
    }

    *feature = result_feature;
    result_feature = NULL;
    status = GX5125_OK;

cleanup:
    (void)descriptor_release(&gray);
    (void)descriptor_release(&prepared);
    (void)descriptor_release(&filtered);
    (void)descriptor_release(&u16_input);
    (void)descriptor_release(&mask);
    (void)descriptor_release(&optional_gray);
    free(aux_temporary);
    if (status != GX5125_OK) {
        extractor->aux_state = state_backup;
        if (result_feature != NULL) gx5125_feature_destroy(result_feature);
    }
    return status;
}

void gx5125_feature_destroy(gx5125_feature *feature) {
    if (feature == NULL) return;
    if (feature->object != NULL) {
        (void)gx_feature_object_release(
            &feature->object, &object_callbacks, NULL);
    }
    free(feature);
}

int gx5125_feature_get_view(const gx5125_feature *feature,
                            gx5125_feature_view *view) {
    const uint8_t *object;
    const gx_feature_pyramid_image *packed;
    if (feature == NULL || feature->object == NULL || view == NULL) {
        return GX5125_ERR_ARGUMENT;
    }
    object = (const uint8_t *)feature->object;
    packed = (const gx_feature_pyramid_image *)
        read_pointer(object, GX_OBJECT_PACKED_MAP_OFFSET);
    memset(view, 0, sizeof(*view));
    view->width = read_u32(object, 0x000U);
    view->height = read_u32(object, 0x004U);
    view->record_count = read_u32(object, GX_OBJECT_RECORD_COUNT_OFFSET);
    view->records =
        (const uint8_t *)read_pointer(object, GX_OBJECT_RECORD_ARRAY_OFFSET);
    view->record_bytes =
        (size_t)view->record_count * GX5125_EXTRACTOR_RECORD_BYTES;
    if (packed != NULL) {
        view->packed_map = (const uint8_t *)packed->pixels;
        view->packed_map_bytes = (size_t)packed->element_count;
    }
    view->auxiliary_map =
        (const uint8_t *)read_pointer(object, GX_OBJECT_AUX_MAP_OFFSET);
    view->auxiliary_map_bytes = GX5125_EXTRACTOR_PIXELS;
    view->mask_map =
        (const uint8_t *)read_pointer(object, GX_OBJECT_MASK_MAP_OFFSET);
    view->post_map =
        (const uint8_t *)read_pointer(object, GX_OBJECT_POST_MAP_OFFSET);
    view->gray_map =
        (const uint8_t *)read_pointer(object, GX_OBJECT_GRAY_MAP_OFFSET);
    view->full_map_bytes = GX5125_EXTRACTOR_PIXELS;
    if (view->records == NULL || view->packed_map == NULL ||
        view->auxiliary_map == NULL || view->mask_map == NULL ||
        view->post_map == NULL || view->gray_map == NULL) {
        memset(view, 0, sizeof(*view));
        return GX5125_ERR_INTERNAL;
    }
    return GX5125_OK;
}

const char *gx5125_status_string(int status) {
    switch (status) {
        case GX5125_OK: return "ok";
        case GX5125_ERR_ARGUMENT: return "invalid argument";
        case GX5125_ERR_MEMORY: return "memory allocation failed";
        case GX5125_ERR_DIMENSIONS: return "unsupported image dimensions";
        case GX5125_ERR_FILTER: return "filter stage failed";
        case GX5125_ERR_MASK: return "mask preparation failed";
        case GX5125_ERR_OPTIONAL_IMAGE: return "optional image preparation failed";
        case GX5125_ERR_FEATURE_MAP: return "feature map preparation failed";
        case GX5125_ERR_AUX_METADATA: return "auxiliary metadata failed";
        case GX5125_ERR_GENERATOR: return "feature generator failed";
        case GX5125_ERR_LOW_FEATURE_COUNT: return "too few feature records";
        case GX5125_ERR_POSTPROCESS: return "feature postprocess failed";
        case GX5125_ERR_UNSUPPORTED_AUX_OVERLAY:
            return "unsupported auxiliary overlay branch";
        case GX5125_ERR_INTERNAL: return "internal extractor error";
        default: return "unknown extractor error";
    }
}

int gx5125_extractor_selftest(void) {
    gx5125_extractor_config config;
    gx5125_extractor *extractor;
    gx5125_feature *feature = NULL;
    gx5125_feature_view view;
    gx5125_extract_stats stats;
    uint8_t gray[GX5125_EXTRACTOR_PIXELS];
    size_t index;
    int status;

    if (gx_feature_descriptor_lifecycle_selftest() != 0 ||
        gx_feature_profile24_root_selftest() != 0 ||
        gx_feature_aux_metadata_selftest() != 0 ||
        gx_feature_object_selftest() != 0 ||
        gx_feature_filter_profile24_validate() != 0 ||
        gx_feature_filter_auxiliary_validate() != 0 ||
        gx_feature_filter_image_prepare_code6_validate() != 0) {
        return -1;
    }
    gx5125_extractor_default_config(&config);
    extractor = gx5125_extractor_create(&config);
    if (extractor == NULL) return -1;
    for (index = 0U; index < GX5125_EXTRACTOR_PIXELS; ++index) {
        const uint32_t x = (uint32_t)(index % GX5125_EXTRACTOR_COLUMNS);
        const uint32_t y = (uint32_t)(index / GX5125_EXTRACTOR_COLUMNS);
        gray[index] = (uint8_t)(
            (x * 37U + y * 73U + ((x ^ y) * 29U) +
             ((x / 5U + y / 3U) & 1U) * 97U) & 0xffU);
    }
    memset(&stats, 0, sizeof(stats));
    status = gx5125_extractor_extract_gray(
        extractor, gray, 90U, 100U, GX5125_EXTRACT_ENROLL,
        NULL, &feature, &stats);
    if (status == GX5125_OK) {
        if (feature == NULL ||
            gx5125_feature_get_view(feature, &view) != GX5125_OK ||
            view.record_count != stats.record_count ||
            view.record_bytes != (size_t)view.record_count *
                GX5125_EXTRACTOR_RECORD_BYTES) {
            gx5125_feature_destroy(feature);
            gx5125_extractor_destroy(extractor);
            return -1;
        }
        gx5125_feature_destroy(feature);
    } else if (status != GX5125_ERR_LOW_FEATURE_COUNT) {
        gx5125_extractor_destroy(extractor);
        return -1;
    }
    gx5125_extractor_reset_state(extractor);
    gx5125_extractor_destroy(extractor);
    return 0;
}
