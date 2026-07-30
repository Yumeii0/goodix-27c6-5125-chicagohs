#include "gx5125/matcher.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "gx5125/feature_primitives.h"
#include "gx5125/feature_record.h"

#define GX_MATCH_ANGLE_PERIOD INT32_C(0x6488)
#define GX_MATCH_ANGLE_HALF INT32_C(0x3244)
#define GX_Q16_ONE UINT32_C(65536)
#define GX_MATCH_MAX_RECORDS GX5125_EXTRACTOR_MAX_RECORDS
#define GX_MATCH_MAX_SEEDS 256U
#define GX_MATCH_DESCRIPTOR_BITS (GX5125_MATCHER_DESCRIPTOR_BYTES * 8U)

typedef struct gx_match_record {
    int32_t x_q8;
    int32_t y_q8;
    int32_t angle;
    uint32_t class_value;
    const uint8_t *descriptor;
} gx_match_record;

typedef struct gx_match_seed {
    uint16_t template_index;
    uint16_t probe_index;
    uint16_t descriptor_hamming;
    uint16_t reserved;
} gx_match_seed;

typedef struct gx_match_hypothesis {
    uint32_t score_q16;
    uint32_t matched_pairs;
    uint32_t support_q16;
    uint32_t descriptor_similarity_q16;
    uint32_t spatial_similarity_q16;
    uint32_t angle_similarity_q16;
} gx_match_hypothesis;

static uint16_t load_u16(const uint8_t *pointer)
{
    return (uint16_t)((uint16_t)pointer[0] |
                      ((uint16_t)pointer[1] << 8U));
}

static uint32_t load_u32(const uint8_t *pointer)
{
    return (uint32_t)pointer[0] |
           ((uint32_t)pointer[1] << 8U) |
           ((uint32_t)pointer[2] << 16U) |
           ((uint32_t)pointer[3] << 24U);
}

static uint32_t popcount32(uint32_t value)
{
    value = value - ((value >> 1U) & UINT32_C(0x55555555));
    value = (value & UINT32_C(0x33333333)) +
            ((value >> 2U) & UINT32_C(0x33333333));
    value = (value + (value >> 4U)) & UINT32_C(0x0f0f0f0f);
    value += value >> 8U;
    value += value >> 16U;
    return value & UINT32_C(0x3f);
}

static uint32_t descriptor_hamming(const uint8_t *left,
                                   const uint8_t *right)
{
    uint32_t distance = 0U;
    size_t offset;

    for (offset = 0U; offset + 4U <= GX5125_MATCHER_DESCRIPTOR_BYTES;
         offset += 4U) {
        distance += popcount32(load_u32(left + offset) ^
                               load_u32(right + offset));
    }
    return distance;
}

static int32_t normalize_angle(int32_t angle)
{
    while (angle > GX_MATCH_ANGLE_HALF) {
        angle -= GX_MATCH_ANGLE_PERIOD;
    }
    while (angle < -GX_MATCH_ANGLE_HALF) {
        angle += GX_MATCH_ANGLE_PERIOD;
    }
    return angle;
}

static uint32_t abs_i32_u32(int32_t value)
{
    return value < 0 ? (uint32_t)(-(int64_t)value) : (uint32_t)value;
}

static int parse_records(const uint8_t *records,
                         size_t record_bytes,
                         uint32_t record_count,
                         gx_match_record parsed[GX_MATCH_MAX_RECORDS])
{
    uint32_t index;

    if (records == NULL || parsed == NULL || record_count == 0U ||
        record_count > GX_MATCH_MAX_RECORDS ||
        record_bytes != (size_t)record_count * GX_FEATURE_RECORD_BYTES) {
        return GX5125_MATCHER_ERR_FEATURE;
    }
    for (index = 0U; index < record_count; ++index) {
        const uint8_t *record = records +
            (size_t)index * GX_FEATURE_RECORD_BYTES;
        parsed[index].x_q8 = (int32_t)load_u16(
            record + GX_FEATURE_RECORD_X_OFFSET);
        parsed[index].y_q8 = (int32_t)load_u16(
            record + GX_FEATURE_RECORD_Y_OFFSET);
        parsed[index].angle = (int32_t)load_u16(
            record + GX_FEATURE_RECORD_ANGLE_OFFSET);
        parsed[index].class_value = load_u32(
            record + GX_FEATURE_RECORD_CLASS_OFFSET);
        parsed[index].descriptor = record + GX_FEATURE_RECORD_DESCRIPTOR_OFFSET;
    }
    return GX5125_MATCHER_OK;
}

static void seed_insert(gx_match_seed *seeds,
                        uint32_t *count,
                        uint32_t capacity,
                        uint16_t template_index,
                        uint16_t probe_index,
                        uint16_t hamming)
{
    uint32_t position;

    if (capacity == 0U) {
        return;
    }
    position = *count;
    if (position < capacity) {
        ++(*count);
    } else if (hamming >= seeds[capacity - 1U].descriptor_hamming) {
        return;
    } else {
        position = capacity - 1U;
    }
    while (position > 0U &&
           hamming < seeds[position - 1U].descriptor_hamming) {
        if (position < capacity) {
            seeds[position] = seeds[position - 1U];
        }
        --position;
    }
    seeds[position].template_index = template_index;
    seeds[position].probe_index = probe_index;
    seeds[position].descriptor_hamming = hamming;
    seeds[position].reserved = 0U;
}

static uint32_t q16_ratio(uint64_t numerator, uint64_t denominator)
{
    if (denominator == 0U) {
        return 0U;
    }
    if (numerator >= denominator) {
        return GX_Q16_ONE;
    }
    return (uint32_t)((numerator * GX_Q16_ONE) / denominator);
}

static void evaluate_hypothesis(
    const gx_match_record *template_records,
    uint32_t template_count,
    const gx_match_record *probe_records,
    uint32_t probe_count,
    const gx5125_matcher_config *config,
    const gx_match_seed *seed,
    gx_match_hypothesis *result)
{
    bool probe_used[GX_MATCH_MAX_RECORDS];
    const gx_match_record *template_seed;
    const gx_match_record *probe_seed;
    int32_t delta_angle;
    int32_t sine_q14 = 0;
    int32_t cosine_q14 = 0;
    uint64_t descriptor_total = 0U;
    uint64_t spatial_total = 0U;
    uint64_t angle_total = 0U;
    uint64_t radius_squared;
    uint32_t template_index;
    uint32_t matched = 0U;

    memset(result, 0, sizeof(*result));
    memset(probe_used, 0, sizeof(probe_used));
    template_seed = &template_records[seed->template_index];
    probe_seed = &probe_records[seed->probe_index];
    delta_angle = normalize_angle(probe_seed->angle - template_seed->angle);
    gx_feature_angle_sincos_q14(delta_angle, &sine_q14, &cosine_q14);
    radius_squared = (uint64_t)config->position_radius_q8 *
                     config->position_radius_q8;

    for (template_index = 0U; template_index < template_count;
         ++template_index) {
        const gx_match_record *template_record =
            &template_records[template_index];
        const int64_t relative_x =
            (int64_t)template_record->x_q8 - template_seed->x_q8;
        const int64_t relative_y =
            (int64_t)template_record->y_q8 - template_seed->y_q8;
        const int32_t transformed_x = probe_seed->x_q8 + (int32_t)(
            (relative_x * cosine_q14 - relative_y * sine_q14) >> 14U);
        const int32_t transformed_y = probe_seed->y_q8 + (int32_t)(
            (relative_x * sine_q14 + relative_y * cosine_q14) >> 14U);
        uint32_t best_probe = UINT32_MAX;
        uint32_t best_pair_score = 0U;
        uint32_t best_descriptor = 0U;
        uint32_t best_spatial = 0U;
        uint32_t best_angle = 0U;
        uint32_t probe_index;

        for (probe_index = 0U; probe_index < probe_count; ++probe_index) {
            const gx_match_record *probe_record = &probe_records[probe_index];
            const int32_t dx = probe_record->x_q8 - transformed_x;
            const int32_t dy = probe_record->y_q8 - transformed_y;
            const int64_t dx64 = dx;
            const int64_t dy64 = dy;
            const uint64_t distance_squared =
                (uint64_t)(dx64 * dx64 + dy64 * dy64);
            const uint32_t angle_error = abs_i32_u32(normalize_angle(
                (probe_record->angle - template_record->angle) - delta_angle));
            uint32_t hamming;
            uint32_t descriptor_similarity;
            uint32_t spatial_similarity;
            uint32_t angle_similarity;
            uint32_t pair_score;

            if (probe_used[probe_index] || distance_squared > radius_squared ||
                angle_error > config->angle_tolerance) {
                continue;
            }
            if ((template_record->class_value & UINT32_C(0xff)) !=
                (probe_record->class_value & UINT32_C(0xff)) &&
                template_record->class_value != 0U &&
                probe_record->class_value != 0U) {
                continue;
            }
            hamming = descriptor_hamming(template_record->descriptor,
                                         probe_record->descriptor);
            if (hamming > config->descriptor_max_hamming) {
                continue;
            }
            descriptor_similarity = q16_ratio(
                GX_MATCH_DESCRIPTOR_BITS - hamming,
                GX_MATCH_DESCRIPTOR_BITS);
            spatial_similarity = GX_Q16_ONE - q16_ratio(
                distance_squared, radius_squared);
            angle_similarity = GX_Q16_ONE - q16_ratio(
                angle_error, config->angle_tolerance);
            pair_score = (descriptor_similarity * 7U +
                          spatial_similarity * 2U +
                          angle_similarity) / 10U;
            if (pair_score > best_pair_score) {
                best_pair_score = pair_score;
                best_probe = probe_index;
                best_descriptor = descriptor_similarity;
                best_spatial = spatial_similarity;
                best_angle = angle_similarity;
            }
        }
        if (best_probe != UINT32_MAX) {
            probe_used[best_probe] = true;
            ++matched;
            descriptor_total += best_descriptor;
            spatial_total += best_spatial;
            angle_total += best_angle;
        }
    }

    result->matched_pairs = matched;
    if (matched < config->minimum_matched_pairs) {
        return;
    }
    result->support_q16 = q16_ratio(
        matched, template_count < probe_count ? template_count : probe_count);
    result->descriptor_similarity_q16 = (uint32_t)(descriptor_total / matched);
    result->spatial_similarity_q16 = (uint32_t)(spatial_total / matched);
    result->angle_similarity_q16 = (uint32_t)(angle_total / matched);
    result->score_q16 =
        (result->descriptor_similarity_q16 * 65U +
         result->spatial_similarity_q16 * 15U +
         result->angle_similarity_q16 * 10U +
         result->support_q16 * 10U) / 100U;
}

void gx5125_matcher_default_config(gx5125_matcher_config *config)
{
    if (config == NULL) {
        return;
    }
    memset(config, 0, sizeof(*config));
    config->maximum_seed_pairs = 96U;
    config->position_radius_q8 = 7U * 256U;
    config->angle_tolerance = (uint32_t)(GX_MATCH_ANGLE_PERIOD / 8);
    config->descriptor_max_hamming = 56U;
    config->minimum_matched_pairs = 8U;
    config->consensus_sample_count = 3U;
    config->decision_threshold_q16 = 0U;
}

static bool config_valid(const gx5125_matcher_config *config)
{
    return config != NULL && config->maximum_seed_pairs > 0U &&
           config->maximum_seed_pairs <= GX_MATCH_MAX_SEEDS &&
           config->position_radius_q8 > 0U &&
           config->angle_tolerance > 0U &&
           config->angle_tolerance <= (uint32_t)GX_MATCH_ANGLE_HALF &&
           config->descriptor_max_hamming <= GX_MATCH_DESCRIPTOR_BITS &&
           config->minimum_matched_pairs > 0U &&
           config->minimum_matched_pairs <= GX_MATCH_MAX_RECORDS &&
           config->consensus_sample_count > 0U &&
           config->consensus_sample_count <= 3U &&
           config->decision_threshold_q16 <= GX_Q16_ONE;
}

int gx5125_matcher_score_views(
    const gx5125_enrollment_sample_view *template_sample,
    const gx5125_feature_view *probe,
    const gx5125_matcher_config *config,
    gx5125_match_result *result)
{
    gx5125_matcher_config resolved;
    gx_match_record template_records[GX_MATCH_MAX_RECORDS];
    gx_match_record probe_records[GX_MATCH_MAX_RECORDS];
    gx_match_seed seeds[GX_MATCH_MAX_SEEDS];
    gx_match_hypothesis best;
    uint32_t seed_count = 0U;
    uint32_t template_index;
    uint32_t seed_index;
    int rc;

    if (template_sample == NULL || probe == NULL || result == NULL) {
        return GX5125_MATCHER_ERR_ARGUMENT;
    }
    if (config == NULL) {
        gx5125_matcher_default_config(&resolved);
        config = &resolved;
    }
    if (!config_valid(config)) {
        return GX5125_MATCHER_ERR_ARGUMENT;
    }
    memset(result, 0, sizeof(*result));
    memset(&best, 0, sizeof(best));
    rc = parse_records(template_sample->records,
                       template_sample->record_bytes,
                       template_sample->record_count,
                       template_records);
    if (rc != GX5125_MATCHER_OK) {
        return rc;
    }
    rc = parse_records(probe->records, probe->record_bytes,
                       probe->record_count, probe_records);
    if (rc != GX5125_MATCHER_OK) {
        return rc;
    }

    memset(seeds, 0, sizeof(seeds));
    for (template_index = 0U;
         template_index < template_sample->record_count;
         ++template_index) {
        uint32_t probe_index;
        for (probe_index = 0U; probe_index < probe->record_count;
             ++probe_index) {
            const uint32_t hamming = descriptor_hamming(
                template_records[template_index].descriptor,
                probe_records[probe_index].descriptor);
            if (hamming <= config->descriptor_max_hamming) {
                seed_insert(seeds, &seed_count,
                            config->maximum_seed_pairs,
                            (uint16_t)template_index,
                            (uint16_t)probe_index,
                            (uint16_t)hamming);
            }
        }
    }
    for (seed_index = 0U; seed_index < seed_count; ++seed_index) {
        gx_match_hypothesis candidate;
        evaluate_hypothesis(template_records,
                            template_sample->record_count,
                            probe_records,
                            probe->record_count,
                            config, &seeds[seed_index], &candidate);
        if (candidate.score_q16 > best.score_q16 ||
            (candidate.score_q16 == best.score_q16 &&
             candidate.matched_pairs > best.matched_pairs)) {
            best = candidate;
        }
    }

    result->score_q16 = best.score_q16;
    result->best_sample_score_q16 = best.score_q16;
    result->second_sample_score_q16 = 0U;
    result->third_sample_score_q16 = 0U;
    result->consensus_sample_count = best.score_q16 != 0U ? 1U : 0U;
    result->best_sample_index = 0U;
    result->matched_pairs = best.matched_pairs;
    result->template_records = template_sample->record_count;
    result->probe_records = probe->record_count;
    result->support_q16 = best.support_q16;
    result->descriptor_similarity_q16 = best.descriptor_similarity_q16;
    result->spatial_similarity_q16 = best.spatial_similarity_q16;
    result->angle_similarity_q16 = best.angle_similarity_q16;
    result->threshold_q16 = config->decision_threshold_q16;
    result->decision_valid = config->decision_threshold_q16 != 0U;
    result->matched = result->decision_valid != 0U &&
                      result->score_q16 >= config->decision_threshold_q16 &&
                      result->matched_pairs >= config->minimum_matched_pairs;
    return GX5125_MATCHER_OK;
}

int gx5125_matcher_score_enrollment(
    const gx5125_enrollment *enrollment,
    const gx5125_feature *probe_feature,
    const gx5125_matcher_config *config,
    gx5125_match_result *result)
{
    gx5125_matcher_config resolved;
    gx5125_feature_view probe;
    gx5125_enrollment_summary summary;
    gx5125_match_result top[3];
    uint32_t top_count = 0U;
    uint32_t requested_consensus;
    uint32_t index;

    if (enrollment == NULL || probe_feature == NULL || result == NULL) {
        return GX5125_MATCHER_ERR_ARGUMENT;
    }
    if (config == NULL) {
        gx5125_matcher_default_config(&resolved);
        config = &resolved;
    }
    if (!config_valid(config)) {
        return GX5125_MATCHER_ERR_ARGUMENT;
    }
    if (gx5125_feature_get_view(probe_feature, &probe) != GX5125_OK ||
        gx5125_enrollment_get_summary(enrollment, &summary) !=
            GX5125_ENROLLMENT_OK || summary.accepted_samples == 0U) {
        return GX5125_MATCHER_ERR_STATE;
    }
    memset(top, 0, sizeof(top));
    for (index = 0U; index < summary.accepted_samples; ++index) {
        gx5125_enrollment_sample_view sample;
        gx5125_match_result candidate;
        uint32_t position;
        int rc;

        memset(&sample, 0, sizeof(sample));
        memset(&candidate, 0, sizeof(candidate));
        if (gx5125_enrollment_get_sample_view(enrollment, index, &sample) !=
            GX5125_ENROLLMENT_OK) {
            return GX5125_MATCHER_ERR_STATE;
        }
        rc = gx5125_matcher_score_views(&sample, &probe, config, &candidate);
        if (rc != GX5125_MATCHER_OK) {
            return rc;
        }
        candidate.best_sample_index = index;
        position = top_count < 3U ? top_count : 2U;
        if (top_count < 3U) {
            ++top_count;
        } else if (candidate.score_q16 < top[2].score_q16 ||
                   (candidate.score_q16 == top[2].score_q16 &&
                    candidate.matched_pairs <= top[2].matched_pairs)) {
            continue;
        }
        while (position > 0U &&
               (candidate.score_q16 > top[position - 1U].score_q16 ||
                (candidate.score_q16 == top[position - 1U].score_q16 &&
                 candidate.matched_pairs > top[position - 1U].matched_pairs))) {
            top[position] = top[position - 1U];
            --position;
        }
        top[position] = candidate;
    }
    if (top_count == 0U) {
        return GX5125_MATCHER_ERR_STATE;
    }

    requested_consensus = config->consensus_sample_count;
    if (requested_consensus > top_count) {
        requested_consensus = top_count;
    }
    *result = top[0];
    result->best_sample_score_q16 = top[0].score_q16;
    result->second_sample_score_q16 = top_count > 1U ? top[1].score_q16 : 0U;
    result->third_sample_score_q16 = top_count > 2U ? top[2].score_q16 : 0U;
    result->consensus_sample_count = requested_consensus;
    if (requested_consensus == 1U) {
        result->score_q16 = top[0].score_q16;
    } else if (requested_consensus == 2U) {
        result->score_q16 =
            (top[0].score_q16 * 5U + top[1].score_q16 * 3U) / 8U;
    } else {
        result->score_q16 =
            (top[0].score_q16 * 5U + top[1].score_q16 * 3U +
             top[2].score_q16 * 2U) / 10U;
    }
    result->threshold_q16 = config->decision_threshold_q16;
    result->decision_valid = config->decision_threshold_q16 != 0U &&
                             requested_consensus ==
                                 config->consensus_sample_count;
    result->matched = result->decision_valid != 0U &&
                      result->score_q16 >= config->decision_threshold_q16 &&
                      result->matched_pairs >= config->minimum_matched_pairs;
    return GX5125_MATCHER_OK;
}

const char *gx5125_matcher_status_string(int status)
{
    switch (status) {
    case GX5125_MATCHER_OK: return "ok";
    case GX5125_MATCHER_ERR_ARGUMENT: return "invalid argument";
    case GX5125_MATCHER_ERR_FEATURE: return "invalid feature records";
    case GX5125_MATCHER_ERR_MEMORY: return "memory allocation failed";
    case GX5125_MATCHER_ERR_STATE: return "invalid matcher state";
    default: return "unknown matcher error";
    }
}

static void store_u16(uint8_t *pointer, uint16_t value)
{
    pointer[0] = (uint8_t)value;
    pointer[1] = (uint8_t)(value >> 8U);
}

static void store_u32(uint8_t *pointer, uint32_t value)
{
    pointer[0] = (uint8_t)value;
    pointer[1] = (uint8_t)(value >> 8U);
    pointer[2] = (uint8_t)(value >> 16U);
    pointer[3] = (uint8_t)(value >> 24U);
}

static void make_synthetic_record(uint8_t record[GX_FEATURE_RECORD_BYTES],
                                  uint32_t index,
                                  int32_t x_q8,
                                  int32_t y_q8,
                                  int32_t angle,
                                  uint32_t salt)
{
    uint32_t word;
    size_t offset;
    memset(record, 0, GX_FEATURE_RECORD_BYTES);
    store_u16(record + GX_FEATURE_RECORD_X_OFFSET, (uint16_t)x_q8);
    store_u16(record + GX_FEATURE_RECORD_Y_OFFSET, (uint16_t)y_q8);
    store_u16(record + GX_FEATURE_RECORD_ANGLE_OFFSET, (uint16_t)angle);
    store_u32(record + GX_FEATURE_RECORD_CLASS_OFFSET, index & 1U);
    word = UINT32_C(0x9e3779b9) * (index + 1U) ^ salt;
    for (offset = 0U; offset < GX5125_MATCHER_DESCRIPTOR_BYTES; ++offset) {
        word ^= word << 13U;
        word ^= word >> 17U;
        word ^= word << 5U;
        record[GX_FEATURE_RECORD_DESCRIPTOR_OFFSET + offset] = (uint8_t)word;
    }
}

int gx5125_matcher_selftest(void)
{
    enum { COUNT = 24 };
    uint8_t template_records[COUNT * GX_FEATURE_RECORD_BYTES];
    uint8_t same_records[COUNT * GX_FEATURE_RECORD_BYTES];
    uint8_t different_records[COUNT * GX_FEATURE_RECORD_BYTES];
    gx5125_enrollment_sample_view sample;
    gx5125_feature_view same;
    gx5125_feature_view different;
    gx5125_matcher_config config;
    gx5125_match_result same_result;
    gx5125_match_result different_result;
    int32_t sine_q14 = 0;
    int32_t cosine_q14 = 0;
    const int32_t delta = INT32_C(0x0600);
    uint32_t index;

    memset(template_records, 0, sizeof(template_records));
    memset(same_records, 0, sizeof(same_records));
    memset(different_records, 0, sizeof(different_records));
    gx_feature_angle_sincos_q14(delta, &sine_q14, &cosine_q14);
    for (index = 0U; index < COUNT; ++index) {
        const int32_t x = (int32_t)((8U + (index % 6U) * 10U) * 256U);
        const int32_t y = (int32_t)((8U + (index / 6U) * 12U) * 256U);
        const int32_t center_x = 36 * 256;
        const int32_t center_y = 26 * 256;
        const int64_t dx = (int64_t)x - center_x;
        const int64_t dy = (int64_t)y - center_y;
        const int32_t rotated_x = center_x + 3 * 256 + (int32_t)(
            (dx * cosine_q14 - dy * sine_q14) >> 14U);
        const int32_t rotated_y = center_y - 2 * 256 + (int32_t)(
            (dx * sine_q14 + dy * cosine_q14) >> 14U);
        uint8_t *same_record;

        make_synthetic_record(template_records +
                              (size_t)index * GX_FEATURE_RECORD_BYTES,
                              index, x, y, (int32_t)(index * 500U), 0U);
        memcpy(same_records + (size_t)index * GX_FEATURE_RECORD_BYTES,
               template_records + (size_t)index * GX_FEATURE_RECORD_BYTES,
               GX_FEATURE_RECORD_BYTES);
        same_record = same_records +
            (size_t)index * GX_FEATURE_RECORD_BYTES;
        store_u16(same_record + GX_FEATURE_RECORD_X_OFFSET,
                  (uint16_t)rotated_x);
        store_u16(same_record + GX_FEATURE_RECORD_Y_OFFSET,
                  (uint16_t)rotated_y);
        store_u16(same_record + GX_FEATURE_RECORD_ANGLE_OFFSET,
                  (uint16_t)((int32_t)(index * 500U) + delta));
        same_record[GX_FEATURE_RECORD_DESCRIPTOR_OFFSET + (index %
            GX5125_MATCHER_DESCRIPTOR_BYTES)] ^= UINT8_C(0x03);
        make_synthetic_record(different_records +
                              (size_t)index * GX_FEATURE_RECORD_BYTES,
                              index, x + (int32_t)((index % 3U) * 256U),
                              y, (int32_t)(index * 1300U),
                              UINT32_C(0xa5a55a5a));
    }

    memset(&sample, 0, sizeof(sample));
    sample.record_count = COUNT;
    sample.records = template_records;
    sample.record_bytes = sizeof(template_records);
    memset(&same, 0, sizeof(same));
    same.record_count = COUNT;
    same.records = same_records;
    same.record_bytes = sizeof(same_records);
    memset(&different, 0, sizeof(different));
    different.record_count = COUNT;
    different.records = different_records;
    different.record_bytes = sizeof(different_records);
    gx5125_matcher_default_config(&config);
    memset(&same_result, 0, sizeof(same_result));
    memset(&different_result, 0, sizeof(different_result));
    if (gx5125_matcher_score_views(&sample, &same, &config,
                                   &same_result) != GX5125_MATCHER_OK ||
        gx5125_matcher_score_views(&sample, &different, &config,
                                   &different_result) != GX5125_MATCHER_OK ||
        same_result.matched_pairs < 20U ||
        same_result.score_q16 <= different_result.score_q16 ||
        same_result.score_q16 < 40000U ||
        different_result.score_q16 > 25000U) {
        return -1;
    }
    return 0;
}
