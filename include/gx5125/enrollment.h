#ifndef GX5125_ENROLLMENT_H
#define GX5125_ENROLLMENT_H

#include <stddef.h>
#include <stdint.h>

#include "gx5125/extractor.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GX5125_ENROLLMENT_VERSION "0.5.0"
#define GX5125_ENROLLMENT_TEMPLATE_FORMAT_VERSION 1U
#define GX5125_ENROLLMENT_MAX_SAMPLES 12U

typedef enum gx5125_enrollment_status {
    GX5125_ENROLLMENT_OK = 0,
    GX5125_ENROLLMENT_ERR_ARGUMENT = -3000,
    GX5125_ENROLLMENT_ERR_MEMORY = -3001,
    GX5125_ENROLLMENT_ERR_STATE = -3002,
    GX5125_ENROLLMENT_ERR_FEATURE = -3003,
    GX5125_ENROLLMENT_ERR_FORMAT = -3004,
    GX5125_ENROLLMENT_ERR_INTEGRITY = -3005,
    GX5125_ENROLLMENT_ERR_CAPACITY = -3006
} gx5125_enrollment_status;

typedef enum gx5125_enrollment_decision {
    GX5125_ENROLLMENT_ACCEPTED = 0,
    GX5125_ENROLLMENT_REJECT_LOW_QUALITY,
    GX5125_ENROLLMENT_REJECT_LOW_COVERAGE,
    GX5125_ENROLLMENT_REJECT_LOW_RECORD_COUNT,
    GX5125_ENROLLMENT_REJECT_EXACT_DUPLICATE,
    GX5125_ENROLLMENT_REJECT_COMPLETE
} gx5125_enrollment_decision;

typedef struct gx5125_enrollment_config {
    uint32_t target_samples;
    uint32_t minimum_quality;
    uint32_t minimum_coverage;
    uint32_t minimum_records;
} gx5125_enrollment_config;

typedef struct gx5125_enrollment_metrics {
    uint32_t quality;
    uint32_t coverage;
    uint32_t mask_coverage_q16;
} gx5125_enrollment_metrics;

typedef struct gx5125_enrollment_result {
    gx5125_enrollment_decision decision;
    uint32_t accepted_samples;
    uint32_t target_samples;
    uint32_t record_count;
    uint32_t quality;
    uint32_t coverage;
    uint32_t exact_duplicate;
    uint32_t best_capture_similarity_q16;
    uint32_t novelty_q16;
    uint32_t complete;
} gx5125_enrollment_result;

typedef struct gx5125_enrollment_summary {
    uint32_t accepted_samples;
    uint32_t target_samples;
    uint32_t complete;
    uint32_t total_records;
    uint32_t minimum_records;
    uint32_t maximum_records;
    uint32_t minimum_quality;
    uint32_t minimum_coverage;
    uint32_t maximum_pair_similarity_q16;
    uint32_t minimum_pair_novelty_q16;
} gx5125_enrollment_summary;

typedef struct gx5125_enrollment_sample_view {
    uint32_t record_count;
    const uint8_t *records;
    size_t record_bytes;
    const uint8_t *packed_map;
    size_t packed_map_bytes;
    gx5125_enrollment_metrics metrics;
} gx5125_enrollment_sample_view;

typedef struct gx5125_enrollment gx5125_enrollment;

void gx5125_enrollment_default_config(gx5125_enrollment_config *config);

gx5125_enrollment *gx5125_enrollment_create(
    const gx5125_enrollment_config *config);
void gx5125_enrollment_destroy(gx5125_enrollment *enrollment);
void gx5125_enrollment_reset(gx5125_enrollment *enrollment);

int gx5125_enrollment_submit(
    gx5125_enrollment *enrollment,
    const gx5125_feature *feature,
    const gx5125_enrollment_metrics *metrics,
    gx5125_enrollment_result *result);

int gx5125_enrollment_get_summary(
    const gx5125_enrollment *enrollment,
    gx5125_enrollment_summary *summary);
int gx5125_enrollment_get_sample_view(
    const gx5125_enrollment *enrollment,
    uint32_t index,
    gx5125_enrollment_sample_view *view);
int gx5125_enrollment_is_complete(const gx5125_enrollment *enrollment);

size_t gx5125_enrollment_serialized_size(
    const gx5125_enrollment *enrollment);
int gx5125_enrollment_serialize(
    const gx5125_enrollment *enrollment,
    uint8_t *destination,
    size_t destination_size,
    size_t *written);
int gx5125_enrollment_deserialize(
    const uint8_t *source,
    size_t source_size,
    gx5125_enrollment **enrollment);

const char *gx5125_enrollment_status_string(int status);
const char *gx5125_enrollment_decision_string(
    gx5125_enrollment_decision decision);
int gx5125_enrollment_selftest(void);

#ifdef __cplusplus
}
#endif

#endif
