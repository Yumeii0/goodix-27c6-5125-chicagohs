#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gx5125/enrollment.h"
#include "gx5125/pipeline.h"

static void make_frame(uint16_t frame[GX5125_DEVICE_IMAGE_PIXELS],
                       unsigned int variant)
{
    size_t index;
    for (index = 0U; index < GX5125_DEVICE_IMAGE_PIXELS; ++index) {
        const unsigned int x = (unsigned int)(index % GX_PREPROC_COLUMNS);
        const unsigned int y = (unsigned int)(index / GX_PREPROC_COLUMNS);
        frame[index] = (uint16_t)(
            980U + ((x * (17U + variant) + y * (29U + variant * 3U) +
                      ((x + variant * 5U) ^ (y + variant * 7U)) * 11U) %
                     760U));
    }
}

static int submit(gx5125_pipeline *pipeline,
                  gx5125_enrollment *enrollment,
                  const uint16_t frame[GX5125_DEVICE_IMAGE_PIXELS],
                  gx5125_enrollment_decision expected)
{
    gx5125_pipeline_result pipeline_result;
    gx5125_enrollment_metrics metrics;
    gx5125_enrollment_result result;
    gx5125_feature *feature = NULL;
    int rc;

    memset(&pipeline_result, 0, sizeof(pipeline_result));
    memset(&metrics, 0, sizeof(metrics));
    memset(&result, 0, sizeof(result));
    rc = gx5125_pipeline_process_raw16(
        pipeline, frame, GX5125_EXTRACT_ENROLL, NULL,
        &feature, &pipeline_result);
    if (rc != GX5125_PIPELINE_OK || feature == NULL) {
        gx5125_feature_destroy(feature);
        return -1;
    }
    metrics.quality = pipeline_result.preprocess.quality;
    metrics.coverage = pipeline_result.preprocess.coverage;
    metrics.mask_coverage_q16 = pipeline_result.extract.mask_coverage_q16;
    rc = gx5125_enrollment_submit(enrollment, feature, &metrics, &result);
    gx5125_feature_destroy(feature);
    return rc == GX5125_ENROLLMENT_OK && result.decision == expected ? 0 : -1;
}

int main(void)
{
    gx5125_pipeline_config pipeline_config;
    gx5125_enrollment_config enrollment_config;
    gx5125_pipeline *pipeline = NULL;
    gx5125_enrollment *enrollment = NULL;
    gx5125_enrollment *corrupt_result = NULL;
    gx5125_enrollment_summary summary;
    gx5125_enrollment_sample_view view;
    uint16_t base[GX5125_DEVICE_IMAGE_PIXELS];
    uint16_t frame[GX5125_DEVICE_IMAGE_PIXELS];
    uint8_t *serialized = NULL;
    uint8_t *roundtrip = NULL;
    size_t serialized_size = 0U;
    size_t written = 0U;
    size_t rewritten = 0U;
    size_t index;
    int rc = 1;

    if (gx5125_enrollment_selftest() != 0) {
        return 1;
    }
    gx5125_pipeline_default_config(&pipeline_config);
    pipeline_config.minimum_quality = 0U;
    pipeline_config.minimum_coverage = 0U;
    gx5125_enrollment_default_config(&enrollment_config);
    enrollment_config.target_samples = 3U;
    enrollment_config.minimum_quality = 0U;
    enrollment_config.minimum_coverage = 0U;
    enrollment_config.minimum_records = 20U;

    pipeline = gx5125_pipeline_create(&pipeline_config);
    enrollment = gx5125_enrollment_create(&enrollment_config);
    if (pipeline == NULL || enrollment == NULL) {
        goto cleanup;
    }
    for (index = 0U; index < GX5125_DEVICE_IMAGE_PIXELS; ++index) {
        base[index] = 2200U;
    }
    if (gx5125_pipeline_set_base_raw16(pipeline, base) !=
        GX5125_PIPELINE_OK) {
        goto cleanup;
    }
    make_frame(frame, 0U);
    if (submit(pipeline, enrollment, frame, GX5125_ENROLLMENT_ACCEPTED) != 0 ||
        submit(pipeline, enrollment, frame,
               GX5125_ENROLLMENT_REJECT_EXACT_DUPLICATE) != 0) {
        goto cleanup;
    }
    make_frame(frame, 1U);
    if (submit(pipeline, enrollment, frame, GX5125_ENROLLMENT_ACCEPTED) != 0) {
        goto cleanup;
    }
    make_frame(frame, 2U);
    if (submit(pipeline, enrollment, frame, GX5125_ENROLLMENT_ACCEPTED) != 0 ||
        !gx5125_enrollment_is_complete(enrollment) ||
        gx5125_enrollment_get_summary(enrollment, &summary) !=
            GX5125_ENROLLMENT_OK ||
        summary.accepted_samples != 3U || summary.complete != 1U ||
        summary.total_records == 0U ||
        gx5125_enrollment_get_sample_view(enrollment, 2U, &view) !=
            GX5125_ENROLLMENT_OK ||
        view.record_count == 0U || view.records == NULL ||
        view.packed_map == NULL) {
        goto cleanup;
    }

    serialized_size = gx5125_enrollment_serialized_size(enrollment);
    serialized = (uint8_t *)malloc(serialized_size);
    roundtrip = (uint8_t *)malloc(serialized_size);
    if (serialized_size == 0U || serialized == NULL || roundtrip == NULL ||
        gx5125_enrollment_serialize(enrollment, serialized, serialized_size,
                                    &written) != GX5125_ENROLLMENT_OK ||
        written != serialized_size) {
        goto cleanup;
    }
    memcpy(roundtrip, serialized, serialized_size);
    roundtrip[serialized_size - 1U] ^= 0x01U;
    if (gx5125_enrollment_deserialize(roundtrip, serialized_size,
                                      &corrupt_result) !=
            GX5125_ENROLLMENT_ERR_INTEGRITY ||
        corrupt_result != NULL) {
        goto cleanup;
    }

    gx5125_enrollment_destroy(corrupt_result);
    gx5125_enrollment_destroy(enrollment);
    enrollment = NULL;
    if (gx5125_enrollment_deserialize(serialized, serialized_size,
                                      &enrollment) !=
            GX5125_ENROLLMENT_OK ||
        enrollment == NULL || !gx5125_enrollment_is_complete(enrollment) ||
        gx5125_enrollment_get_summary(enrollment, &summary) !=
            GX5125_ENROLLMENT_OK ||
        summary.accepted_samples != 3U || summary.complete != 1U ||
        gx5125_enrollment_serialize(enrollment, roundtrip, serialized_size,
                                    &rewritten) != GX5125_ENROLLMENT_OK ||
        rewritten != serialized_size ||
        memcmp(serialized, roundtrip, serialized_size) != 0 ||
        gx5125_enrollment_get_sample_view(enrollment, 2U, &view) !=
            GX5125_ENROLLMENT_OK ||
        view.record_count == 0U || view.records == NULL ||
        view.packed_map == NULL) {
        goto cleanup;
    }
    puts("GOODIX_BETA_ENROLLMENT_SELFTEST=PASS pure_linux:1 pipeline:1 "
         "accepted:3 duplicate_rejected:1 bundle_memory_only:0 "
         "matcher:0 serialized_template:1 roundtrip_exact:1 "
         "integrity_reject:1 biometric_input_used:0 "
         "biometric_output_saved:0");
    rc = 0;

cleanup:
    gx5125_enrollment_destroy(corrupt_result);
    gx5125_enrollment_destroy(enrollment);
    if (serialized != NULL) {
        memset(serialized, 0, serialized_size);
        free(serialized);
    }
    if (roundtrip != NULL) {
        memset(roundtrip, 0, serialized_size);
        free(roundtrip);
    }
    gx5125_pipeline_destroy(pipeline);
    memset(base, 0, sizeof(base));
    memset(frame, 0, sizeof(frame));
    memset(&summary, 0, sizeof(summary));
    memset(&view, 0, sizeof(view));
    return rc;
}
