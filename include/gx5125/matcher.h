#ifndef GX5125_MATCHER_H
#define GX5125_MATCHER_H

#include <stddef.h>
#include <stdint.h>

#include "gx5125/enrollment.h"
#include "gx5125/extractor.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GX5125_MATCHER_VERSION "0.6.0"
#define GX5125_MATCHER_DESCRIPTOR_BYTES 20U

/* Part 6 keeps calibration transparent while adding descriptor-gated
 * per-sample scoring and top-three enrollment consensus. A production
 * decision threshold remains intentionally optional until multi-session
 * same-finger and different-finger distributions are measured. */
typedef enum gx5125_matcher_status {
    GX5125_MATCHER_OK = 0,
    GX5125_MATCHER_ERR_ARGUMENT = -4000,
    GX5125_MATCHER_ERR_FEATURE = -4001,
    GX5125_MATCHER_ERR_MEMORY = -4002,
    GX5125_MATCHER_ERR_STATE = -4003
} gx5125_matcher_status;

typedef struct gx5125_matcher_config {
    uint32_t maximum_seed_pairs;
    uint32_t position_radius_q8;
    uint32_t angle_tolerance;
    uint32_t descriptor_max_hamming;
    uint32_t minimum_matched_pairs;
    uint32_t consensus_sample_count;
    uint32_t decision_threshold_q16;
} gx5125_matcher_config;

typedef struct gx5125_match_result {
    uint32_t score_q16;
    uint32_t best_sample_score_q16;
    uint32_t second_sample_score_q16;
    uint32_t third_sample_score_q16;
    uint32_t consensus_sample_count;
    uint32_t best_sample_index;
    uint32_t matched_pairs;
    uint32_t template_records;
    uint32_t probe_records;
    uint32_t support_q16;
    uint32_t descriptor_similarity_q16;
    uint32_t spatial_similarity_q16;
    uint32_t angle_similarity_q16;
    uint32_t threshold_q16;
    uint32_t decision_valid;
    uint32_t matched;
} gx5125_match_result;

void gx5125_matcher_default_config(gx5125_matcher_config *config);

int gx5125_matcher_score_views(
    const gx5125_enrollment_sample_view *template_sample,
    const gx5125_feature_view *probe,
    const gx5125_matcher_config *config,
    gx5125_match_result *result);

int gx5125_matcher_score_enrollment(
    const gx5125_enrollment *enrollment,
    const gx5125_feature *probe,
    const gx5125_matcher_config *config,
    gx5125_match_result *result);

const char *gx5125_matcher_status_string(int status);
int gx5125_matcher_selftest(void);

#ifdef __cplusplus
}
#endif

#endif
