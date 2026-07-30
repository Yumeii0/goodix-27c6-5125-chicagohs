#include "gx5125/feature_detector.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "gx5125/feature_refine.h"

#define GX_PROFILE_MULTI_LEVEL UINT32_C(0x07a20c80)
#define GX_PROFILE_REVERSE_LEVEL_SCAN UINT32_C(0x07220c00)
#define GX_PROFILE_NO_FALLBACK_STORE UINT32_C(0x00a00a80)
#define GX_PROFILE_SKIP_FALLBACK_PHASE UINT32_C(0x00a40a80)
#define GX_PROFILE_FALLBACK_METRICS UINT32_C(0x07020400)
#define GX_FALLBACK_SCALE_MULTIPLIER UINT32_C(0x00013333)

static int profile_in_set(uint32_t profile, uint32_t set) {
    return profile < 27U && ((set >> profile) & 1U) != 0U;
}

static int validate_descriptors(
    const gx_feature_map_descriptor *gray,
    const gx_feature_u16_map *levels,
    size_t level_count,
    uint8_t *records,
    int32_t *rank_pairs,
    int32_t *metadata,
    uint32_t *record_count,
    uint8_t *visited,
    const gx_feature_map_descriptor *magnitude,
    const gx_feature_map_descriptor *orientation,
    int32_t max_records) {
    size_t index;

    if (gray == NULL || gray->pixels == NULL || gray->width <= 0 ||
        gray->height <= 0 || levels == NULL || level_count < 6U ||
        records == NULL || rank_pairs == NULL || metadata == NULL ||
        record_count == NULL || visited == NULL || magnitude == NULL ||
        orientation == NULL || magnitude->pixels == NULL ||
        orientation->pixels == NULL || max_records <= 0 ||
        magnitude->width != gray->width || magnitude->height != gray->height ||
        orientation->width != gray->width ||
        orientation->height != gray->height) {
        return 0;
    }
    for (index = 0U; index < level_count; ++index) {
        if (levels[index].pixels == NULL || levels[index].width != gray->width ||
            levels[index].height != gray->height) {
            return 0;
        }
    }
    return 1;
}

static void append_candidate(
    const gx_feature_map_descriptor *gray,
    const gx_feature_candidate_state *candidate,
    uint8_t *records,
    int32_t *rank_pairs,
    int32_t *metadata,
    uint32_t *count,
    const gx_feature_map_descriptor *magnitude,
    const gx_feature_map_descriptor *orientation,
    int32_t max_records,
    uint32_t profile) {
    gx_feature_refined_candidate append_candidate_state;
    const size_t pixel_index =
        (size_t)candidate->y * (size_t)gray->width + (size_t)candidate->x;

    memcpy(&append_candidate_state, candidate, sizeof(append_candidate_state));
    gx_feature_append_orientations(
        (const uint8_t *)gray->pixels + pixel_index,
        &append_candidate_state, records, rank_pairs, metadata, count,
        magnitude, orientation, max_records, profile);
}

int gx_feature_detect_candidates(
    const gx_feature_map_descriptor *gray,
    uint8_t *records,
    int32_t *rank_pairs,
    int32_t *metadata,
    uint32_t *record_count,
    const gx_feature_u16_map *levels,
    size_t level_count,
    uint8_t *visited,
    const gx_feature_map_descriptor *magnitude,
    const gx_feature_map_descriptor *orientation,
    int32_t edge_threshold,
    int32_t max_records,
    uint32_t profile,
    const gx_feature_detector_parameters *parameters) {
    gx_feature_candidate_state fallback[GX_FEATURE_DETECTOR_MAX_FALLBACK];
    uint32_t count = 0U;
    uint32_t fallback_count = 0U;
    int32_t scale_levels;
    int32_t outer_level;
    int32_t remaining_level;
    int stop = 0;

    if (!validate_descriptors(gray, levels, level_count, records, rank_pairs,
                              metadata, record_count, visited, magnitude,
                              orientation, max_records) ||
        edge_threshold < 0) {
        return -1;
    }

    scale_levels = (profile == 9U || profile == 18U ||
                    profile_in_set(profile, GX_PROFILE_MULTI_LEVEL))
                       ? 6
                       : 3;
    if ((size_t)(scale_levels + 2) >= level_count) {
        return -1;
    }

    outer_level = 1;
    remaining_level = scale_levels;
    while (outer_level <= scale_levels && !stop) {
        int32_t detector_level;
        int32_t y;

        if (profile > 26U ||
            !profile_in_set(profile, GX_PROFILE_REVERSE_LEVEL_SCAN)) {
            detector_level = outer_level;
        } else {
            detector_level = remaining_level;
        }

        for (y = 6; y < gray->height - 6 && !stop; ++y) {
            int32_t x;
            for (x = 6; x < gray->width - 6; ++x) {
                int32_t gate_response = 0;
                const int gate = gx_feature_candidate_extremum_gate(
                    levels, level_count, x, y, detector_level,
                    INT32_C(0x148), &gate_response);

                if (gate == 1) {
                    gx_feature_candidate_state candidate;
                    int32_t curvature = 0;
                    int refined;

                    memset(&candidate, 0, sizeof(candidate));
                    candidate.x = x;
                    candidate.y = y;
                    candidate.level = detector_level;
                    /* FUN_180015660 passes the extremum response inside the
                     * mutable candidate structure. FUN_180015060 overwrites
                     * it for ordinary profiles, but profile 9/18 deliberately
                     * preserve this gate response for ranking/record metric. */
                    candidate.response = gate_response;
                    refined = gx_feature_candidate_refine(
                        levels, level_count, &candidate, &curvature,
                        edge_threshold, profile);

                    if (refined == 0) {
                        if ((profile > 23U ||
                             !profile_in_set(profile,
                                             GX_PROFILE_NO_FALLBACK_STORE)) &&
                            fallback_count < GX_FEATURE_DETECTOR_MAX_FALLBACK) {
                            gx_feature_candidate_state *stored =
                                &fallback[fallback_count];
                            const uint32_t scale = gx_feature_scale_exp_q16(
                                (detector_level << 16) / scale_levels);

                            memset(stored, 0, sizeof(*stored));
                            stored->x = x;
                            stored->y = y;
                            stored->level = detector_level;
                            stored->response = gate_response;
                            stored->x_q8 = (int16_t)(x << 8);
                            stored->y_q8 = (int16_t)(y << 8);
                            stored->scale_q16 = (int32_t)(
                                ((uint64_t)scale *
                                 GX_FALLBACK_SCALE_MULTIPLIER) >> 16U);
                            ++fallback_count;
                        }
                    } else if (refined == 1) {
                        const size_t pixel_index =
                            (size_t)candidate.y * (size_t)gray->width +
                            (size_t)candidate.x;
                        if (visited[pixel_index] == 0U) {
                            append_candidate(gray, &candidate, records,
                                             rank_pairs, metadata, &count,
                                             magnitude, orientation,
                                             max_records, profile);
                            visited[pixel_index] = 1U;
                            if ((int32_t)count >= max_records) {
                                stop = 1;
                                break;
                            }
                        }
                    }
                }
            }
        }
        ++outer_level;
        --remaining_level;
    }

    if (!stop &&
        (profile > 23U ||
         !profile_in_set(profile, GX_PROFILE_SKIP_FALLBACK_PHASE))) {
        int geometry_and_capacity_ok = 1;

        if (gray->width * 2 <= gray->height * 5 || count > 0x77U) {
            geometry_and_capacity_ok = 0;
        }
        if (profile_in_set(profile, GX_PROFILE_FALLBACK_METRICS)) {
            const int32_t metric_34 = parameters != NULL
                                          ? parameters->metric_34
                                          : 0;
            const int32_t metric_38 = parameters != NULL
                                          ? parameters->metric_38
                                          : 0;
            if (count > 0x3bU || metric_34 < 0x51 || metric_38 < 0x51) {
                stop = 1;
            }
        } else if (!geometry_and_capacity_ok) {
            stop = 1;
        }

        if (!stop) {
            uint32_t index;
            for (index = 0U; index < fallback_count; ++index) {
                const gx_feature_candidate_state *candidate =
                    &fallback[index];
                const size_t pixel_index =
                    (size_t)candidate->y * (size_t)gray->width +
                    (size_t)candidate->x;

                if (count > 0x77U) {
                    break;
                }
                if (visited[pixel_index] == 0U) {
                    append_candidate(gray, candidate, records, rank_pairs,
                                     metadata, &count, magnitude, orientation,
                                     max_records, profile);
                    visited[pixel_index] = 1U;
                }
            }
        }
    }

    *record_count = count;
    return 0;
}
