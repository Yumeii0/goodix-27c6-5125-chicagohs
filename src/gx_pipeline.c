#include "gx5125/pipeline.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define GX5125_PRESENCE_MIN_COVERAGE 6U

struct gx5125_pipeline {
    gx5125_pipeline_config config;
    gx5125_device *device;
    gx_preproc *preprocessor;
    gx5125_extractor *extractor;
    gx5125_pipeline_state state;
    bool base_ready;
};

static void secure_cleanse(void *memory, size_t size)
{
    volatile uint8_t *bytes = (volatile uint8_t *)memory;
    while (size > 0U) {
        *bytes++ = 0U;
        --size;
    }
}

static bool config_valid(const gx5125_pipeline_config *config)
{
    return config != NULL && config->minimum_quality <= 100U &&
           config->minimum_coverage <= 100U;
}

void gx5125_pipeline_default_config(gx5125_pipeline_config *config)
{
    if (config == NULL) {
        return;
    }
    memset(config, 0, sizeof(*config));
    gx5125_device_default_config(&config->device);
    gx_preproc_default_params(&config->preprocessor);
    gx5125_extractor_default_config(&config->extractor);
    config->minimum_quality = 20U;
    config->minimum_coverage = 20U;
}

static int create_processing_contexts(gx5125_pipeline *pipeline)
{
    pipeline->preprocessor = gx_preproc_create(&pipeline->config.preprocessor);
    if (pipeline->preprocessor == NULL) {
        return GX5125_PIPELINE_ERR_MEMORY;
    }
    pipeline->extractor = gx5125_extractor_create(&pipeline->config.extractor);
    if (pipeline->extractor == NULL) {
        gx_preproc_destroy(pipeline->preprocessor);
        pipeline->preprocessor = NULL;
        return GX5125_PIPELINE_ERR_MEMORY;
    }
    pipeline->base_ready = false;
    return GX5125_PIPELINE_OK;
}

static void destroy_processing_contexts(gx5125_pipeline *pipeline)
{
    gx5125_extractor_destroy(pipeline->extractor);
    gx_preproc_destroy(pipeline->preprocessor);
    pipeline->extractor = NULL;
    pipeline->preprocessor = NULL;
    pipeline->base_ready = false;
}

gx5125_pipeline *gx5125_pipeline_create(
    const gx5125_pipeline_config *config)
{
    gx5125_pipeline_config resolved;
    gx5125_pipeline *pipeline;

    if (config == NULL) {
        gx5125_pipeline_default_config(&resolved);
        config = &resolved;
    }
    if (!config_valid(config)) {
        return NULL;
    }
    pipeline = (gx5125_pipeline *)calloc(1U, sizeof(*pipeline));
    if (pipeline == NULL) {
        return NULL;
    }
    pipeline->config = *config;
    pipeline->device = gx5125_device_create(&pipeline->config.device);
    if (pipeline->device == NULL ||
        create_processing_contexts(pipeline) != GX5125_PIPELINE_OK) {
        gx5125_device_destroy(pipeline->device);
        destroy_processing_contexts(pipeline);
        secure_cleanse(pipeline, sizeof(*pipeline));
        free(pipeline);
        return NULL;
    }
    pipeline->state = GX5125_PIPELINE_CLOSED;
    return pipeline;
}

void gx5125_pipeline_destroy(gx5125_pipeline *pipeline)
{
    if (pipeline == NULL) {
        return;
    }
    gx5125_pipeline_close(pipeline);
    gx5125_device_destroy(pipeline->device);
    destroy_processing_contexts(pipeline);
    secure_cleanse(pipeline, sizeof(*pipeline));
    free(pipeline);
}

int gx5125_pipeline_open(gx5125_pipeline *pipeline)
{
    int rc;

    if (pipeline == NULL) {
        return GX5125_PIPELINE_ERR_ARGUMENT;
    }
    if (pipeline->state != GX5125_PIPELINE_CLOSED) {
        return GX5125_PIPELINE_ERR_STATE;
    }
    if (pipeline->preprocessor == NULL || pipeline->extractor == NULL) {
        rc = create_processing_contexts(pipeline);
        if (rc != GX5125_PIPELINE_OK) {
            pipeline->state = GX5125_PIPELINE_ERROR;
            return rc;
        }
    }
    rc = gx5125_device_open(pipeline->device);
    if (rc != GX5125_DEVICE_OK) {
        pipeline->state = GX5125_PIPELINE_CLOSED;
        return rc == GX5125_DEVICE_ERR_CANCELLED
                   ? GX5125_PIPELINE_ERR_CANCELLED
                   : GX5125_PIPELINE_ERR_DEVICE;
    }
    pipeline->state = GX5125_PIPELINE_READY;
    return GX5125_PIPELINE_OK;
}

void gx5125_pipeline_close(gx5125_pipeline *pipeline)
{
    if (pipeline == NULL) {
        return;
    }
    gx5125_device_close(pipeline->device);
    destroy_processing_contexts(pipeline);
    pipeline->state = GX5125_PIPELINE_CLOSED;
}

int gx5125_pipeline_reset_processing(gx5125_pipeline *pipeline)
{
    int rc;

    if (pipeline == NULL) {
        return GX5125_PIPELINE_ERR_ARGUMENT;
    }
    if (pipeline->state == GX5125_PIPELINE_CAPTURING) {
        return GX5125_PIPELINE_ERR_STATE;
    }
    destroy_processing_contexts(pipeline);
    rc = create_processing_contexts(pipeline);
    if (rc != GX5125_PIPELINE_OK) {
        pipeline->state = GX5125_PIPELINE_ERROR;
    }
    return rc;
}

int gx5125_pipeline_reset_extractor_state(gx5125_pipeline *pipeline)
{
    if (pipeline == NULL) {
        return GX5125_PIPELINE_ERR_ARGUMENT;
    }
    if (pipeline->state == GX5125_PIPELINE_CAPTURING ||
        pipeline->state == GX5125_PIPELINE_ERROR ||
        pipeline->extractor == NULL) {
        return GX5125_PIPELINE_ERR_STATE;
    }
    gx5125_extractor_reset_state(pipeline->extractor);
    return GX5125_PIPELINE_OK;
}

int gx5125_pipeline_set_base_raw16(
    gx5125_pipeline *pipeline,
    const uint16_t base[GX5125_DEVICE_IMAGE_PIXELS])
{
    int rc;

    if (pipeline == NULL || base == NULL) {
        return GX5125_PIPELINE_ERR_ARGUMENT;
    }
    if (pipeline->state == GX5125_PIPELINE_CAPTURING ||
        pipeline->state == GX5125_PIPELINE_ERROR ||
        pipeline->preprocessor == NULL || pipeline->extractor == NULL) {
        return GX5125_PIPELINE_ERR_STATE;
    }
    rc = gx_preproc_initialize(pipeline->preprocessor, base);
    if (rc != GX_PREPROC_OK) {
        pipeline->base_ready = false;
        return GX5125_PIPELINE_ERR_BASE_INVALID;
    }
    gx5125_extractor_reset_state(pipeline->extractor);
    pipeline->base_ready = true;
    return GX5125_PIPELINE_OK;
}

int gx5125_pipeline_capture_base(
    gx5125_pipeline *pipeline,
    gx5125_capture_metadata *metadata)
{
    uint16_t base[GX5125_DEVICE_IMAGE_PIXELS];
    gx5125_capture_metadata local_metadata;
    int rc;

    if (pipeline == NULL) {
        return GX5125_PIPELINE_ERR_ARGUMENT;
    }
    if (pipeline->state != GX5125_PIPELINE_READY) {
        return GX5125_PIPELINE_ERR_STATE;
    }
    memset(base, 0, sizeof(base));
    memset(&local_metadata, 0, sizeof(local_metadata));
    pipeline->state = GX5125_PIPELINE_CAPTURING;
    rc = gx5125_device_capture_raw16(pipeline->device, base, &local_metadata);
    pipeline->state = GX5125_PIPELINE_READY;
    if (rc != GX5125_DEVICE_OK) {
        secure_cleanse(base, sizeof(base));
        return rc == GX5125_DEVICE_ERR_CANCELLED
                   ? GX5125_PIPELINE_ERR_CANCELLED
                   : GX5125_PIPELINE_ERR_BASE_CAPTURE;
    }
    rc = gx5125_pipeline_set_base_raw16(pipeline, base);
    secure_cleanse(base, sizeof(base));
    if (rc != GX5125_PIPELINE_OK) {
        return rc;
    }
    if (metadata != NULL) {
        *metadata = local_metadata;
    }
    return GX5125_PIPELINE_OK;
}

int gx5125_pipeline_process_raw16(
    gx5125_pipeline *pipeline,
    const uint16_t current[GX5125_DEVICE_IMAGE_PIXELS],
    gx5125_extract_mode mode,
    const uint8_t auxiliary[GX5125_EXTRACTOR_AUX_BYTES],
    gx5125_feature **feature,
    gx5125_pipeline_result *result)
{
    uint8_t gray[GX_PREPROC_PIXELS];
    gx5125_pipeline_result local_result;
    gx5125_feature *local_feature = NULL;
    int rc;

    if (pipeline == NULL || current == NULL || feature == NULL) {
        return GX5125_PIPELINE_ERR_ARGUMENT;
    }
    *feature = NULL;
    if (pipeline->state == GX5125_PIPELINE_CAPTURING ||
        pipeline->state == GX5125_PIPELINE_ERROR ||
        !pipeline->base_ready || pipeline->preprocessor == NULL ||
        pipeline->extractor == NULL) {
        return GX5125_PIPELINE_ERR_STATE;
    }
    memset(gray, 0, sizeof(gray));
    memset(&local_result, 0, sizeof(local_result));
    rc = gx_preproc_process(pipeline->preprocessor, current, gray,
                            &local_result.preprocess);
    if (rc != GX_PREPROC_OK) {
        secure_cleanse(gray, sizeof(gray));
        return GX5125_PIPELINE_ERR_PREPROCESS;
    }
    if (local_result.preprocess.quality < pipeline->config.minimum_quality ||
        local_result.preprocess.coverage < pipeline->config.minimum_coverage) {
        if (result != NULL) {
            *result = local_result;
        }
        secure_cleanse(gray, sizeof(gray));
        return GX5125_PIPELINE_ERR_LOW_QUALITY;
    }
    rc = gx5125_extractor_extract_gray(
        pipeline->extractor, gray,
        local_result.preprocess.quality,
        local_result.preprocess.coverage,
        mode, auxiliary, &local_feature, &local_result.extract);
    secure_cleanse(gray, sizeof(gray));
    if (rc != GX5125_OK || local_feature == NULL) {
        gx5125_feature_destroy(local_feature);
        if (result != NULL) {
            *result = local_result;
        }
        return GX5125_PIPELINE_ERR_EXTRACT;
    }
    *feature = local_feature;
    if (result != NULL) {
        *result = local_result;
    }
    return GX5125_PIPELINE_OK;
}

int gx5125_pipeline_process_presence_raw16(
    gx5125_pipeline *pipeline,
    const uint16_t current[GX5125_DEVICE_IMAGE_PIXELS],
    gx5125_presence_result *result)
{
    uint8_t gray[GX_PREPROC_PIXELS];
    gx5125_presence_result local_result;
    int rc;

    if (pipeline == NULL || current == NULL || result == NULL) {
        return GX5125_PIPELINE_ERR_ARGUMENT;
    }
    if (pipeline->state == GX5125_PIPELINE_CAPTURING ||
        pipeline->state == GX5125_PIPELINE_ERROR ||
        !pipeline->base_ready || pipeline->preprocessor == NULL) {
        return GX5125_PIPELINE_ERR_STATE;
    }

    memset(gray, 0, sizeof(gray));
    memset(&local_result, 0, sizeof(local_result));
    rc = gx_preproc_process(pipeline->preprocessor, current, gray,
                            &local_result.preprocess);
    secure_cleanse(gray, sizeof(gray));
    if (rc != GX_PREPROC_OK && rc != GX_PREPROC_ERR_IMAGE_INVALID) {
        return GX5125_PIPELINE_ERR_PREPROCESS;
    }

    local_result.state =
        local_result.preprocess.coverage >= GX5125_PRESENCE_MIN_COVERAGE
            ? GX5125_FINGER_PRESENT
            : GX5125_FINGER_ABSENT;
    *result = local_result;
    return GX5125_PIPELINE_OK;
}

int gx5125_pipeline_capture_presence(
    gx5125_pipeline *pipeline,
    gx5125_presence_result *result)
{
    uint16_t current[GX5125_DEVICE_IMAGE_PIXELS];
    gx5125_capture_metadata metadata;
    gx5125_presence_result local_result;
    int rc;

    if (pipeline == NULL || result == NULL) {
        return GX5125_PIPELINE_ERR_ARGUMENT;
    }
    if (pipeline->state != GX5125_PIPELINE_READY || !pipeline->base_ready) {
        return GX5125_PIPELINE_ERR_STATE;
    }

    memset(current, 0, sizeof(current));
    memset(&metadata, 0, sizeof(metadata));
    memset(&local_result, 0, sizeof(local_result));
    pipeline->state = GX5125_PIPELINE_CAPTURING;
    rc = gx5125_device_capture_raw16(pipeline->device, current, &metadata);
    pipeline->state = GX5125_PIPELINE_READY;
    if (rc != GX5125_DEVICE_OK) {
        secure_cleanse(current, sizeof(current));
        return rc == GX5125_DEVICE_ERR_CANCELLED
                   ? GX5125_PIPELINE_ERR_CANCELLED
                   : GX5125_PIPELINE_ERR_FINGER_CAPTURE;
    }

    rc = gx5125_pipeline_process_presence_raw16(pipeline, current,
                                                 &local_result);
    secure_cleanse(current, sizeof(current));
    local_result.capture = metadata;
    *result = local_result;
    return rc;
}

int gx5125_pipeline_capture_feature(
    gx5125_pipeline *pipeline,
    gx5125_extract_mode mode,
    const uint8_t auxiliary[GX5125_EXTRACTOR_AUX_BYTES],
    gx5125_feature **feature,
    gx5125_pipeline_result *result)
{
    uint16_t current[GX5125_DEVICE_IMAGE_PIXELS];
    gx5125_capture_metadata metadata;
    gx5125_pipeline_result local_result;
    int rc;

    if (pipeline == NULL || feature == NULL) {
        return GX5125_PIPELINE_ERR_ARGUMENT;
    }
    *feature = NULL;
    if (pipeline->state != GX5125_PIPELINE_READY || !pipeline->base_ready) {
        return GX5125_PIPELINE_ERR_STATE;
    }
    memset(current, 0, sizeof(current));
    memset(&metadata, 0, sizeof(metadata));
    memset(&local_result, 0, sizeof(local_result));
    pipeline->state = GX5125_PIPELINE_CAPTURING;
    rc = gx5125_device_capture_raw16(pipeline->device, current, &metadata);
    pipeline->state = GX5125_PIPELINE_READY;
    if (rc != GX5125_DEVICE_OK) {
        secure_cleanse(current, sizeof(current));
        return rc == GX5125_DEVICE_ERR_CANCELLED
                   ? GX5125_PIPELINE_ERR_CANCELLED
                   : GX5125_PIPELINE_ERR_FINGER_CAPTURE;
    }
    rc = gx5125_pipeline_process_raw16(
        pipeline, current, mode, auxiliary, feature, &local_result);
    secure_cleanse(current, sizeof(current));
    local_result.capture = metadata;
    if (result != NULL) {
        *result = local_result;
    }
    return rc;
}

void gx5125_pipeline_request_cancel(gx5125_pipeline *pipeline)
{
    if (pipeline != NULL) {
        gx5125_device_request_cancel(pipeline->device);
    }
}

void gx5125_pipeline_clear_cancel(gx5125_pipeline *pipeline)
{
    if (pipeline != NULL) {
        gx5125_device_clear_cancel(pipeline->device);
    }
}

gx5125_pipeline_state gx5125_pipeline_get_state(
    const gx5125_pipeline *pipeline)
{
    return pipeline == NULL ? GX5125_PIPELINE_ERROR : pipeline->state;
}

int gx5125_pipeline_has_base(const gx5125_pipeline *pipeline)
{
    return pipeline != NULL && pipeline->base_ready ? 1 : 0;
}

int gx5125_pipeline_get_device_info(const gx5125_pipeline *pipeline,
                                    gx5125_device_info *info)
{
    if (pipeline == NULL || info == NULL) {
        return GX5125_PIPELINE_ERR_ARGUMENT;
    }
    if (pipeline->state != GX5125_PIPELINE_READY) {
        return GX5125_PIPELINE_ERR_STATE;
    }
    return gx5125_device_get_info(pipeline->device, info) == GX5125_DEVICE_OK
               ? GX5125_PIPELINE_OK
               : GX5125_PIPELINE_ERR_DEVICE;
}

const char *gx5125_pipeline_state_string(gx5125_pipeline_state state)
{
    switch (state) {
    case GX5125_PIPELINE_CLOSED: return "closed";
    case GX5125_PIPELINE_READY: return "ready";
    case GX5125_PIPELINE_CAPTURING: return "capturing";
    case GX5125_PIPELINE_ERROR: return "error";
    default: return "unknown";
    }
}

const char *gx5125_pipeline_status_string(int status)
{
    switch (status) {
    case GX5125_PIPELINE_OK: return "ok";
    case GX5125_PIPELINE_ERR_ARGUMENT: return "invalid argument";
    case GX5125_PIPELINE_ERR_MEMORY: return "memory allocation failed";
    case GX5125_PIPELINE_ERR_STATE: return "invalid pipeline state";
    case GX5125_PIPELINE_ERR_DEVICE: return "device open failed";
    case GX5125_PIPELINE_ERR_BASE_CAPTURE: return "base capture failed";
    case GX5125_PIPELINE_ERR_BASE_INVALID: return "base frame rejected";
    case GX5125_PIPELINE_ERR_FINGER_CAPTURE: return "finger capture failed";
    case GX5125_PIPELINE_ERR_PREPROCESS: return "preprocessing failed";
    case GX5125_PIPELINE_ERR_LOW_QUALITY: return "capture quality too low";
    case GX5125_PIPELINE_ERR_EXTRACT: return "feature extraction failed";
    case GX5125_PIPELINE_ERR_CANCELLED: return "operation cancelled";
    default: return "unknown pipeline error";
    }
}

static bool feature_views_equal(const gx5125_feature *left,
                                const gx5125_feature *right)
{
    gx5125_feature_view a;
    gx5125_feature_view b;

    if (gx5125_feature_get_view(left, &a) != GX5125_OK ||
        gx5125_feature_get_view(right, &b) != GX5125_OK) {
        return false;
    }
    return a.width == b.width && a.height == b.height &&
           a.record_count == b.record_count &&
           a.record_bytes == b.record_bytes &&
           a.packed_map_bytes == b.packed_map_bytes &&
           a.auxiliary_map_bytes == b.auxiliary_map_bytes &&
           a.full_map_bytes == b.full_map_bytes &&
           memcmp(a.records, b.records, a.record_bytes) == 0 &&
           memcmp(a.packed_map, b.packed_map, a.packed_map_bytes) == 0 &&
           memcmp(a.auxiliary_map, b.auxiliary_map,
                  a.auxiliary_map_bytes) == 0 &&
           memcmp(a.mask_map, b.mask_map, a.full_map_bytes) == 0 &&
           memcmp(a.post_map, b.post_map, a.full_map_bytes) == 0 &&
           memcmp(a.gray_map, b.gray_map, a.full_map_bytes) == 0;
}

int gx5125_pipeline_selftest(void)
{
    gx5125_pipeline_config config;
    gx5125_pipeline *a = NULL;
    gx5125_pipeline *b = NULL;
    uint16_t base[GX5125_DEVICE_IMAGE_PIXELS];
    uint16_t current[GX5125_DEVICE_IMAGE_PIXELS];
    gx5125_pipeline_result result_a;
    gx5125_pipeline_result result_b;
    gx5125_presence_result presence_absent;
    gx5125_presence_result presence_present;
    gx5125_feature *feature_a = NULL;
    gx5125_feature *feature_b = NULL;
    size_t index;
    int rc = -1;

    if (gx5125_device_selftest() != 0 || gx5125_extractor_selftest() != 0) {
        return -1;
    }
    gx5125_pipeline_default_config(&config);
    a = gx5125_pipeline_create(&config);
    b = gx5125_pipeline_create(&config);
    if (a == NULL || b == NULL ||
        gx5125_pipeline_capture_base(a, NULL) != GX5125_PIPELINE_ERR_STATE) {
        goto cleanup;
    }
    for (index = 0U; index < GX5125_DEVICE_IMAGE_PIXELS; ++index) {
        const unsigned int x = (unsigned int)(index % GX_PREPROC_COLUMNS);
        const unsigned int y = (unsigned int)(index / GX_PREPROC_COLUMNS);
        base[index] = 2200U;
        current[index] = (uint16_t)(1000U + ((x * 17U + y * 29U) % 700U));
    }
    if (gx5125_pipeline_set_base_raw16(a, base) != GX5125_PIPELINE_OK ||
        gx5125_pipeline_set_base_raw16(b, base) != GX5125_PIPELINE_OK) {
        goto cleanup;
    }
    memset(&presence_absent, 0, sizeof(presence_absent));
    memset(&presence_present, 0, sizeof(presence_present));
    if (gx5125_pipeline_process_presence_raw16(a, base,
                                               &presence_absent) != GX5125_PIPELINE_OK ||
        presence_absent.state != GX5125_FINGER_ABSENT ||
        presence_absent.preprocess.coverage != 0U ||
        gx5125_pipeline_process_presence_raw16(a, current,
                                               &presence_present) != GX5125_PIPELINE_OK ||
        presence_present.state != GX5125_FINGER_PRESENT ||
        presence_present.preprocess.coverage < 6U) {
        goto cleanup;
    }
    memset(&result_a, 0, sizeof(result_a));
    memset(&result_b, 0, sizeof(result_b));
    if (gx5125_pipeline_process_raw16(
            a, current, GX5125_EXTRACT_ENROLL, NULL,
            &feature_a, &result_a) != GX5125_PIPELINE_OK ||
        gx5125_pipeline_process_raw16(
            b, current, GX5125_EXTRACT_ENROLL, NULL,
            &feature_b, &result_b) != GX5125_PIPELINE_OK ||
        feature_a == NULL || feature_b == NULL ||
        result_a.extract.record_count != 90U ||
        memcmp(&result_a.preprocess, &result_b.preprocess,
               sizeof(result_a.preprocess)) != 0 ||
        memcmp(&result_a.extract, &result_b.extract,
               sizeof(result_a.extract)) != 0 ||
        !feature_views_equal(feature_a, feature_b)) {
        goto cleanup;
    }
    gx5125_feature_destroy(feature_a);
    gx5125_feature_destroy(feature_b);
    feature_a = NULL;
    feature_b = NULL;
    if (gx5125_pipeline_reset_extractor_state(a) != GX5125_PIPELINE_OK ||
        gx5125_pipeline_has_base(a) == 0 ||
        gx5125_pipeline_process_raw16(
            a, current, GX5125_EXTRACT_IDENTIFY, NULL,
            &feature_a, &result_a) != GX5125_PIPELINE_OK ||
        feature_a == NULL || result_a.extract.record_count != 90U) {
        goto cleanup;
    }
    gx5125_feature_destroy(feature_a);
    feature_a = NULL;
    if (gx5125_pipeline_process_raw16(
            a, base, GX5125_EXTRACT_ENROLL, NULL,
            &feature_a, &result_a) != GX5125_PIPELINE_ERR_LOW_QUALITY ||
        feature_a != NULL || result_a.preprocess.coverage != 0U ||
        gx5125_pipeline_reset_processing(a) != GX5125_PIPELINE_OK ||
        gx5125_pipeline_has_base(a) != 0) {
        goto cleanup;
    }
    rc = 0;

cleanup:
    gx5125_feature_destroy(feature_a);
    gx5125_feature_destroy(feature_b);
    gx5125_pipeline_destroy(a);
    gx5125_pipeline_destroy(b);
    secure_cleanse(base, sizeof(base));
    secure_cleanse(current, sizeof(current));
    secure_cleanse(&result_a, sizeof(result_a));
    secure_cleanse(&result_b, sizeof(result_b));
    secure_cleanse(&presence_absent, sizeof(presence_absent));
    secure_cleanse(&presence_present, sizeof(presence_present));
    return rc;
}
