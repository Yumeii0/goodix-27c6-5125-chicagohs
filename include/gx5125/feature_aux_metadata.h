#ifndef GX5125_FEATURE_AUX_METADATA_H
#define GX5125_FEATURE_AUX_METADATA_H

#include <stddef.h>
#include <stdint.h>

#include "gx5125/feature_pyramid.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GX_FEATURE_AUX_HISTORY_SLOTS 3U
#define GX_FEATURE_AUX_HISTORY_BYTES 19600U
#define GX_FEATURE_AUX_CONTEXT_WORDS 12U
#define GX_FEATURE_AUX_PROFILE24 UINT32_C(24)

typedef struct gx_feature_aux_metadata_state {
    uint32_t history_count;
    int32_t previous_coverage;
    uint8_t history[GX_FEATURE_AUX_HISTORY_SLOTS][GX_FEATURE_AUX_HISTORY_BYTES];
    int32_t stability_counter;
    int32_t previous_level;
} gx_feature_aux_metadata_state;

typedef struct gx_feature_aux_metadata_stats {
    uint32_t class_one_pixels;
    uint32_t class_two_pixels;
    uint32_t dark_hole_pixels;
    uint32_t anomaly_flag;
    int32_t resolved_primary_level;
    int32_t resolved_secondary_level;
    int32_t metric_state;
} gx_feature_aux_metadata_stats;

void gx_feature_aux_metadata_state_reset(gx_feature_aux_metadata_state *state);

int gx_feature_aux_metadata_state_import(
    gx_feature_aux_metadata_state *state,
    uint32_t history_count, int32_t previous_coverage,
    const uint8_t history[GX_FEATURE_AUX_HISTORY_SLOTS][GX_FEATURE_AUX_HISTORY_BYTES],
    int32_t stability_counter, int32_t previous_level);

/* Native equivalents of the small root auxiliary-input helpers. */
int gx_feature_root_aux_flags_parse(
    const uint8_t *input, int32_t rows, int32_t columns,
    int32_t *packed_state, uint8_t **temporary);
void gx_feature_root_packed_state_decode(
    uint32_t packed_state, uint32_t *first, uint32_t *second);
void gx_feature_root_aux_state_decode(
    const uint8_t input[6], uint32_t output[6]);

/* Exact active profile-24 implementation of FUN_1800135f0. The special
 * profile-7/profile-23 auxiliary-map morphology branch is intentionally not
 * accepted here; the active ChicagoHS profile is 24. The caller owns aux_map,
 * which must contain at least width*height bytes and is normally zeroed before
 * this call. Only aggregate statistics are returned. */
int gx_feature_root_aux_metadata_prepare_profile24(
    const gx_feature_pyramid_image *optional_gray,
    const gx_feature_pyramid_image *mask,
    const uint32_t context[GX_FEATURE_AUX_CONTEXT_WORDS],
    int32_t *metric_state, uint8_t *aux_map,
    gx_feature_aux_metadata_state *state,
    gx_feature_aux_metadata_stats *stats);

/* Non-biometric deterministic state and branch selftest. */
int gx_feature_aux_metadata_selftest(void);

#ifdef __cplusplus
}
#endif

#endif
