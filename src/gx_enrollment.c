#include "gx5125/enrollment.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define GX5125_GRID_COLUMNS 10U
#define GX5125_GRID_ROWS 8U
#define GX5125_GRID_CELLS (GX5125_GRID_COLUMNS * GX5125_GRID_ROWS)
#define GX5125_Q16_ONE 65536U

typedef struct gx5125_enrollment_sample {
    uint8_t *records;
    size_t record_bytes;
    uint8_t *packed_map;
    size_t packed_map_bytes;
    uint32_t record_count;
    gx5125_enrollment_metrics metrics;
} gx5125_enrollment_sample;

struct gx5125_enrollment {
    gx5125_enrollment_config config;
    gx5125_enrollment_sample samples[GX5125_ENROLLMENT_MAX_SAMPLES];
    uint32_t sample_count;
    uint32_t total_records;
    uint32_t minimum_records;
    uint32_t maximum_records;
    uint32_t minimum_quality;
    uint32_t minimum_coverage;
    uint32_t maximum_pair_similarity_q16;
    uint32_t minimum_pair_novelty_q16;
};

static void secure_cleanse(void *memory, size_t size)
{
    volatile uint8_t *bytes = (volatile uint8_t *)memory;
    while (size > 0U) {
        *bytes++ = 0U;
        --size;
    }
}

static void sample_clear(gx5125_enrollment_sample *sample)
{
    if (sample == NULL) {
        return;
    }
    if (sample->records != NULL) {
        secure_cleanse(sample->records, sample->record_bytes);
        free(sample->records);
    }
    if (sample->packed_map != NULL) {
        secure_cleanse(sample->packed_map, sample->packed_map_bytes);
        free(sample->packed_map);
    }
    secure_cleanse(sample, sizeof(*sample));
}

static bool config_valid(const gx5125_enrollment_config *config)
{
    return config != NULL && config->target_samples > 0U &&
           config->target_samples <= GX5125_ENROLLMENT_MAX_SAMPLES &&
           config->minimum_quality <= 100U &&
           config->minimum_coverage <= 100U &&
           config->minimum_records > 0U &&
           config->minimum_records <= GX5125_EXTRACTOR_MAX_RECORDS;
}

void gx5125_enrollment_default_config(gx5125_enrollment_config *config)
{
    if (config == NULL) {
        return;
    }
    memset(config, 0, sizeof(*config));
    config->target_samples = 6U;
    config->minimum_quality = 60U;
    config->minimum_coverage = 70U;
    config->minimum_records = 45U;
}

gx5125_enrollment *gx5125_enrollment_create(
    const gx5125_enrollment_config *config)
{
    gx5125_enrollment_config resolved;
    gx5125_enrollment *enrollment;

    if (config == NULL) {
        gx5125_enrollment_default_config(&resolved);
        config = &resolved;
    }
    if (!config_valid(config)) {
        return NULL;
    }
    enrollment = (gx5125_enrollment *)calloc(1U, sizeof(*enrollment));
    if (enrollment == NULL) {
        return NULL;
    }
    enrollment->config = *config;
    enrollment->minimum_pair_novelty_q16 = GX5125_Q16_ONE;
    return enrollment;
}

void gx5125_enrollment_reset(gx5125_enrollment *enrollment)
{
    uint32_t index;

    if (enrollment == NULL) {
        return;
    }
    for (index = 0U; index < enrollment->sample_count; ++index) {
        sample_clear(&enrollment->samples[index]);
    }
    enrollment->sample_count = 0U;
    enrollment->total_records = 0U;
    enrollment->minimum_records = 0U;
    enrollment->maximum_records = 0U;
    enrollment->minimum_quality = 0U;
    enrollment->minimum_coverage = 0U;
    enrollment->maximum_pair_similarity_q16 = 0U;
    enrollment->minimum_pair_novelty_q16 = GX5125_Q16_ONE;
}

void gx5125_enrollment_destroy(gx5125_enrollment *enrollment)
{
    if (enrollment == NULL) {
        return;
    }
    gx5125_enrollment_reset(enrollment);
    secure_cleanse(enrollment, sizeof(*enrollment));
    free(enrollment);
}

static uint32_t popcount8(uint8_t value)
{
    uint32_t count = 0U;
    while (value != 0U) {
        value = (uint8_t)(value & (uint8_t)(value - 1U));
        ++count;
    }
    return count;
}

static uint32_t bitset_jaccard_q16(const uint8_t *left,
                                   const uint8_t *right,
                                   size_t bytes)
{
    uint64_t intersection = 0U;
    uint64_t union_count = 0U;
    size_t index;

    if (left == NULL || right == NULL) {
        return 0U;
    }
    for (index = 0U; index < bytes; ++index) {
        intersection += popcount8((uint8_t)(left[index] & right[index]));
        union_count += popcount8((uint8_t)(left[index] | right[index]));
    }
    if (union_count == 0U) {
        return GX5125_Q16_ONE;
    }
    return (uint32_t)((intersection * GX5125_Q16_ONE) / union_count);
}

static int16_t read_i16(const uint8_t *source)
{
    int16_t value = 0;
    memcpy(&value, source, sizeof(value));
    return value;
}

static void build_record_grid(const uint8_t *records,
                              uint32_t record_count,
                              uint8_t grid[GX5125_GRID_CELLS])
{
    uint32_t index;

    memset(grid, 0, GX5125_GRID_CELLS);
    if (records == NULL) {
        return;
    }
    for (index = 0U; index < record_count; ++index) {
        const uint8_t *record = records +
            (size_t)index * GX5125_EXTRACTOR_RECORD_BYTES;
        int32_t x = (int32_t)read_i16(record + 0x02U) / 256;
        int32_t y = (int32_t)read_i16(record + 0x04U) / 256;
        uint32_t column;
        uint32_t row;

        if (x < 0) x = 0;
        if (y < 0) y = 0;
        if (x >= (int32_t)GX5125_EXTRACTOR_COLUMNS) {
            x = (int32_t)GX5125_EXTRACTOR_COLUMNS - 1;
        }
        if (y >= (int32_t)GX5125_EXTRACTOR_ROWS) {
            y = (int32_t)GX5125_EXTRACTOR_ROWS - 1;
        }
        column = (uint32_t)x / 8U;
        row = (uint32_t)y / 8U;
        if (column >= GX5125_GRID_COLUMNS) {
            column = GX5125_GRID_COLUMNS - 1U;
        }
        if (row >= GX5125_GRID_ROWS) {
            row = GX5125_GRID_ROWS - 1U;
        }
        grid[row * GX5125_GRID_COLUMNS + column] = 1U;
    }
}

static uint32_t grid_jaccard_q16(const uint8_t *left_records,
                                 uint32_t left_count,
                                 const uint8_t *right_records,
                                 uint32_t right_count)
{
    uint8_t left[GX5125_GRID_CELLS];
    uint8_t right[GX5125_GRID_CELLS];
    uint32_t intersection = 0U;
    uint32_t union_count = 0U;
    uint32_t index;

    build_record_grid(left_records, left_count, left);
    build_record_grid(right_records, right_count, right);
    for (index = 0U; index < GX5125_GRID_CELLS; ++index) {
        if (left[index] != 0U && right[index] != 0U) {
            ++intersection;
        }
        if (left[index] != 0U || right[index] != 0U) {
            ++union_count;
        }
    }
    secure_cleanse(left, sizeof(left));
    secure_cleanse(right, sizeof(right));
    if (union_count == 0U) {
        return GX5125_Q16_ONE;
    }
    return (uint32_t)(((uint64_t)intersection * GX5125_Q16_ONE) /
                      union_count);
}

static uint32_t capture_similarity_q16(
    const gx5125_enrollment_sample *sample,
    const gx5125_feature_view *view)
{
    const uint32_t packed = bitset_jaccard_q16(
        sample->packed_map, view->packed_map, view->packed_map_bytes);
    const uint32_t grid = grid_jaccard_q16(
        sample->records, sample->record_count,
        view->records, view->record_count);

    /* This is a capture-diversity measurement, not an identity/matcher score. */
    return (uint32_t)(((uint64_t)grid * 3U + packed) / 4U);
}

static bool exact_duplicate(const gx5125_enrollment_sample *sample,
                            const gx5125_feature_view *view)
{
    return sample->record_count == view->record_count &&
           sample->record_bytes == view->record_bytes &&
           sample->packed_map_bytes == view->packed_map_bytes &&
           memcmp(sample->records, view->records, view->record_bytes) == 0 &&
           memcmp(sample->packed_map, view->packed_map,
                  view->packed_map_bytes) == 0;
}

static void result_initialize(gx5125_enrollment_result *result,
                              const gx5125_enrollment *enrollment,
                              const gx5125_feature_view *view,
                              const gx5125_enrollment_metrics *metrics)
{
    if (result == NULL) {
        return;
    }
    memset(result, 0, sizeof(*result));
    result->decision = GX5125_ENROLLMENT_REJECT_COMPLETE;
    result->accepted_samples = enrollment->sample_count;
    result->target_samples = enrollment->config.target_samples;
    result->record_count = view == NULL ? 0U : view->record_count;
    if (metrics != NULL) {
        result->quality = metrics->quality;
        result->coverage = metrics->coverage;
    }
    result->novelty_q16 = GX5125_Q16_ONE;
    result->complete = enrollment->sample_count >= enrollment->config.target_samples;
}

static int sample_copy(gx5125_enrollment_sample *destination,
                       const gx5125_feature_view *view,
                       const gx5125_enrollment_metrics *metrics)
{
    memset(destination, 0, sizeof(*destination));
    destination->records = (uint8_t *)malloc(view->record_bytes);
    destination->packed_map = (uint8_t *)malloc(view->packed_map_bytes);
    if (destination->records == NULL || destination->packed_map == NULL) {
        sample_clear(destination);
        return GX5125_ENROLLMENT_ERR_MEMORY;
    }
    memcpy(destination->records, view->records, view->record_bytes);
    memcpy(destination->packed_map, view->packed_map, view->packed_map_bytes);
    destination->record_bytes = view->record_bytes;
    destination->packed_map_bytes = view->packed_map_bytes;
    destination->record_count = view->record_count;
    destination->metrics = *metrics;
    return GX5125_ENROLLMENT_OK;
}

int gx5125_enrollment_submit(
    gx5125_enrollment *enrollment,
    const gx5125_feature *feature,
    const gx5125_enrollment_metrics *metrics,
    gx5125_enrollment_result *result)
{
    gx5125_feature_view view;
    uint32_t best_similarity = 0U;
    uint32_t index;
    int rc;

    if (enrollment == NULL || feature == NULL || metrics == NULL) {
        return GX5125_ENROLLMENT_ERR_ARGUMENT;
    }
    memset(&view, 0, sizeof(view));
    if (gx5125_feature_get_view(feature, &view) != GX5125_OK ||
        view.records == NULL || view.record_bytes == 0U ||
        view.packed_map == NULL || view.packed_map_bytes == 0U) {
        return GX5125_ENROLLMENT_ERR_FEATURE;
    }
    result_initialize(result, enrollment, &view, metrics);
    if (enrollment->sample_count >= enrollment->config.target_samples) {
        return GX5125_ENROLLMENT_OK;
    }
    if (metrics->quality < enrollment->config.minimum_quality) {
        if (result != NULL) result->decision = GX5125_ENROLLMENT_REJECT_LOW_QUALITY;
        return GX5125_ENROLLMENT_OK;
    }
    if (metrics->coverage < enrollment->config.minimum_coverage) {
        if (result != NULL) result->decision = GX5125_ENROLLMENT_REJECT_LOW_COVERAGE;
        return GX5125_ENROLLMENT_OK;
    }
    if (view.record_count < enrollment->config.minimum_records) {
        if (result != NULL) result->decision = GX5125_ENROLLMENT_REJECT_LOW_RECORD_COUNT;
        return GX5125_ENROLLMENT_OK;
    }
    for (index = 0U; index < enrollment->sample_count; ++index) {
        uint32_t similarity;
        if (exact_duplicate(&enrollment->samples[index], &view)) {
            if (result != NULL) {
                result->decision = GX5125_ENROLLMENT_REJECT_EXACT_DUPLICATE;
                result->exact_duplicate = 1U;
                result->best_capture_similarity_q16 = GX5125_Q16_ONE;
                result->novelty_q16 = 0U;
            }
            return GX5125_ENROLLMENT_OK;
        }
        similarity = capture_similarity_q16(&enrollment->samples[index], &view);
        if (similarity > best_similarity) {
            best_similarity = similarity;
        }
    }
    rc = sample_copy(&enrollment->samples[enrollment->sample_count],
                     &view, metrics);
    if (rc != GX5125_ENROLLMENT_OK) {
        return rc;
    }
    ++enrollment->sample_count;
    enrollment->total_records += view.record_count;
    if (enrollment->sample_count == 1U) {
        enrollment->minimum_records = view.record_count;
        enrollment->maximum_records = view.record_count;
        enrollment->minimum_quality = metrics->quality;
        enrollment->minimum_coverage = metrics->coverage;
    } else {
        if (view.record_count < enrollment->minimum_records) {
            enrollment->minimum_records = view.record_count;
        }
        if (view.record_count > enrollment->maximum_records) {
            enrollment->maximum_records = view.record_count;
        }
        if (metrics->quality < enrollment->minimum_quality) {
            enrollment->minimum_quality = metrics->quality;
        }
        if (metrics->coverage < enrollment->minimum_coverage) {
            enrollment->minimum_coverage = metrics->coverage;
        }
        if (best_similarity > enrollment->maximum_pair_similarity_q16) {
            enrollment->maximum_pair_similarity_q16 = best_similarity;
        }
        if (GX5125_Q16_ONE - best_similarity <
            enrollment->minimum_pair_novelty_q16) {
            enrollment->minimum_pair_novelty_q16 =
                GX5125_Q16_ONE - best_similarity;
        }
    }
    if (result != NULL) {
        result->decision = GX5125_ENROLLMENT_ACCEPTED;
        result->accepted_samples = enrollment->sample_count;
        result->best_capture_similarity_q16 = best_similarity;
        result->novelty_q16 = GX5125_Q16_ONE - best_similarity;
        result->complete = enrollment->sample_count >=
            enrollment->config.target_samples;
    }
    return GX5125_ENROLLMENT_OK;
}

int gx5125_enrollment_get_summary(
    const gx5125_enrollment *enrollment,
    gx5125_enrollment_summary *summary)
{
    if (enrollment == NULL || summary == NULL) {
        return GX5125_ENROLLMENT_ERR_ARGUMENT;
    }
    memset(summary, 0, sizeof(*summary));
    summary->accepted_samples = enrollment->sample_count;
    summary->target_samples = enrollment->config.target_samples;
    summary->complete = enrollment->sample_count >=
        enrollment->config.target_samples;
    summary->total_records = enrollment->total_records;
    summary->minimum_records = enrollment->minimum_records;
    summary->maximum_records = enrollment->maximum_records;
    summary->minimum_quality = enrollment->minimum_quality;
    summary->minimum_coverage = enrollment->minimum_coverage;
    summary->maximum_pair_similarity_q16 =
        enrollment->maximum_pair_similarity_q16;
    summary->minimum_pair_novelty_q16 =
        enrollment->sample_count <= 1U ? GX5125_Q16_ONE :
        enrollment->minimum_pair_novelty_q16;
    return GX5125_ENROLLMENT_OK;
}

int gx5125_enrollment_get_sample_view(
    const gx5125_enrollment *enrollment,
    uint32_t index,
    gx5125_enrollment_sample_view *view)
{
    const gx5125_enrollment_sample *sample;
    if (enrollment == NULL || view == NULL) {
        return GX5125_ENROLLMENT_ERR_ARGUMENT;
    }
    if (index >= enrollment->sample_count) {
        return GX5125_ENROLLMENT_ERR_STATE;
    }
    sample = &enrollment->samples[index];
    memset(view, 0, sizeof(*view));
    view->record_count = sample->record_count;
    view->records = sample->records;
    view->record_bytes = sample->record_bytes;
    view->packed_map = sample->packed_map;
    view->packed_map_bytes = sample->packed_map_bytes;
    view->metrics = sample->metrics;
    return GX5125_ENROLLMENT_OK;
}

int gx5125_enrollment_is_complete(const gx5125_enrollment *enrollment)
{
    return enrollment != NULL && enrollment->sample_count >=
        enrollment->config.target_samples;
}


#define GX5125_TEMPLATE_HEADER_BYTES 80U
#define GX5125_TEMPLATE_SAMPLE_HEADER_BYTES 24U
#define GX5125_TEMPLATE_CRC_OFFSET 72U
#define GX5125_TEMPLATE_MAX_BYTES (1024U * 1024U)

static const uint8_t gx5125_template_magic[8] = {
    'G', 'X', '5', '1', '2', '5', 'T', '1'
};

static void write_u32_le(uint8_t *destination, uint32_t value)
{
    destination[0] = (uint8_t)(value & 0xffU);
    destination[1] = (uint8_t)((value >> 8) & 0xffU);
    destination[2] = (uint8_t)((value >> 16) & 0xffU);
    destination[3] = (uint8_t)((value >> 24) & 0xffU);
}

static uint32_t read_u32_le(const uint8_t *source)
{
    return (uint32_t)source[0] |
           ((uint32_t)source[1] << 8) |
           ((uint32_t)source[2] << 16) |
           ((uint32_t)source[3] << 24);
}

static int add_size_checked(size_t *value, size_t addend)
{
    if (value == NULL || addend > SIZE_MAX - *value) {
        return -1;
    }
    *value += addend;
    return 0;
}

static uint32_t template_crc32(const uint8_t *bytes, size_t size)
{
    uint32_t crc = 0xffffffffU;
    size_t index;

    for (index = 0U; index < size; ++index) {
        uint8_t value = bytes[index];
        unsigned int bit;
        if (index >= GX5125_TEMPLATE_CRC_OFFSET &&
            index < GX5125_TEMPLATE_CRC_OFFSET + sizeof(uint32_t)) {
            value = 0U;
        }
        crc ^= value;
        for (bit = 0U; bit < 8U; ++bit) {
            crc = (crc >> 1) ^
                ((crc & 1U) != 0U ? 0xedb88320U : 0U);
        }
    }
    return ~crc;
}

size_t gx5125_enrollment_serialized_size(
    const gx5125_enrollment *enrollment)
{
    size_t total = GX5125_TEMPLATE_HEADER_BYTES;
    uint32_t index;

    if (enrollment == NULL ||
        enrollment->sample_count == 0U ||
        enrollment->sample_count != enrollment->config.target_samples ||
        enrollment->sample_count > GX5125_ENROLLMENT_MAX_SAMPLES) {
        return 0U;
    }
    for (index = 0U; index < enrollment->sample_count; ++index) {
        const gx5125_enrollment_sample *sample = &enrollment->samples[index];
        if (sample->records == NULL || sample->packed_map == NULL ||
            sample->record_count == 0U ||
            sample->record_count > GX5125_EXTRACTOR_MAX_RECORDS ||
            sample->record_bytes !=
                (size_t)sample->record_count * GX5125_EXTRACTOR_RECORD_BYTES ||
            sample->packed_map_bytes == 0U ||
            sample->packed_map_bytes > GX5125_EXTRACTOR_PIXELS ||
            add_size_checked(&total,
                             GX5125_TEMPLATE_SAMPLE_HEADER_BYTES) != 0 ||
            add_size_checked(&total, sample->record_bytes) != 0 ||
            add_size_checked(&total, sample->packed_map_bytes) != 0) {
            return 0U;
        }
    }
    if (total > GX5125_TEMPLATE_MAX_BYTES || total > UINT32_MAX) {
        return 0U;
    }
    return total;
}

int gx5125_enrollment_serialize(
    const gx5125_enrollment *enrollment,
    uint8_t *destination,
    size_t destination_size,
    size_t *written)
{
    const size_t total = gx5125_enrollment_serialized_size(enrollment);
    size_t offset = GX5125_TEMPLATE_HEADER_BYTES;
    uint32_t index;

    if (written != NULL) {
        *written = 0U;
    }
    if (enrollment == NULL || destination == NULL || written == NULL) {
        return GX5125_ENROLLMENT_ERR_ARGUMENT;
    }
    if (total == 0U) {
        return GX5125_ENROLLMENT_ERR_STATE;
    }
    if (destination_size < total) {
        return GX5125_ENROLLMENT_ERR_CAPACITY;
    }

    memset(destination, 0, total);
    memcpy(destination, gx5125_template_magic, sizeof(gx5125_template_magic));
    write_u32_le(destination + 8U,
                 GX5125_ENROLLMENT_TEMPLATE_FORMAT_VERSION);
    write_u32_le(destination + 12U, GX5125_TEMPLATE_HEADER_BYTES);
    write_u32_le(destination + 16U, (uint32_t)total);
    write_u32_le(destination + 20U, enrollment->sample_count);
    write_u32_le(destination + 24U, enrollment->config.target_samples);
    write_u32_le(destination + 28U, enrollment->config.minimum_quality);
    write_u32_le(destination + 32U, enrollment->config.minimum_coverage);
    write_u32_le(destination + 36U, enrollment->config.minimum_records);
    write_u32_le(destination + 40U, enrollment->total_records);
    write_u32_le(destination + 44U, enrollment->minimum_records);
    write_u32_le(destination + 48U, enrollment->maximum_records);
    write_u32_le(destination + 52U, enrollment->minimum_quality);
    write_u32_le(destination + 56U, enrollment->minimum_coverage);
    write_u32_le(destination + 60U,
                 enrollment->maximum_pair_similarity_q16);
    write_u32_le(destination + 64U,
                 enrollment->minimum_pair_novelty_q16);
    write_u32_le(destination + 68U, 0U);
    write_u32_le(destination + 72U, 0U);
    write_u32_le(destination + 76U, 0U);

    for (index = 0U; index < enrollment->sample_count; ++index) {
        const gx5125_enrollment_sample *sample = &enrollment->samples[index];
        write_u32_le(destination + offset + 0U, sample->record_count);
        write_u32_le(destination + offset + 4U,
                     (uint32_t)sample->record_bytes);
        write_u32_le(destination + offset + 8U,
                     (uint32_t)sample->packed_map_bytes);
        write_u32_le(destination + offset + 12U, sample->metrics.quality);
        write_u32_le(destination + offset + 16U, sample->metrics.coverage);
        write_u32_le(destination + offset + 20U,
                     sample->metrics.mask_coverage_q16);
        offset += GX5125_TEMPLATE_SAMPLE_HEADER_BYTES;
        memcpy(destination + offset, sample->records, sample->record_bytes);
        offset += sample->record_bytes;
        memcpy(destination + offset, sample->packed_map,
               sample->packed_map_bytes);
        offset += sample->packed_map_bytes;
    }
    if (offset != total) {
        secure_cleanse(destination, total);
        return GX5125_ENROLLMENT_ERR_STATE;
    }
    write_u32_le(destination + GX5125_TEMPLATE_CRC_OFFSET,
                 template_crc32(destination, total));
    *written = total;
    return GX5125_ENROLLMENT_OK;
}

int gx5125_enrollment_deserialize(
    const uint8_t *source,
    size_t source_size,
    gx5125_enrollment **enrollment_out)
{
    gx5125_enrollment_config config;
    gx5125_enrollment *enrollment = NULL;
    uint32_t sample_count;
    uint32_t expected_total_records;
    uint32_t expected_minimum_records;
    uint32_t expected_maximum_records;
    uint32_t expected_minimum_quality;
    uint32_t expected_minimum_coverage;
    uint32_t index;
    size_t offset = GX5125_TEMPLATE_HEADER_BYTES;
    int status = GX5125_ENROLLMENT_ERR_FORMAT;

    if (enrollment_out != NULL) {
        *enrollment_out = NULL;
    }
    if (source == NULL || enrollment_out == NULL) {
        return GX5125_ENROLLMENT_ERR_ARGUMENT;
    }
    if (source_size < GX5125_TEMPLATE_HEADER_BYTES ||
        source_size > GX5125_TEMPLATE_MAX_BYTES ||
        memcmp(source, gx5125_template_magic,
               sizeof(gx5125_template_magic)) != 0 ||
        read_u32_le(source + 8U) !=
            GX5125_ENROLLMENT_TEMPLATE_FORMAT_VERSION ||
        read_u32_le(source + 12U) != GX5125_TEMPLATE_HEADER_BYTES ||
        read_u32_le(source + 16U) != source_size ||
        read_u32_le(source + 68U) != 0U ||
        read_u32_le(source + 76U) != 0U) {
        return GX5125_ENROLLMENT_ERR_FORMAT;
    }
    if (read_u32_le(source + GX5125_TEMPLATE_CRC_OFFSET) !=
        template_crc32(source, source_size)) {
        return GX5125_ENROLLMENT_ERR_INTEGRITY;
    }

    sample_count = read_u32_le(source + 20U);
    memset(&config, 0, sizeof(config));
    config.target_samples = read_u32_le(source + 24U);
    config.minimum_quality = read_u32_le(source + 28U);
    config.minimum_coverage = read_u32_le(source + 32U);
    config.minimum_records = read_u32_le(source + 36U);
    if (sample_count == 0U ||
        sample_count > GX5125_ENROLLMENT_MAX_SAMPLES ||
        sample_count != config.target_samples ||
        !config_valid(&config) ||
        read_u32_le(source + 60U) > GX5125_Q16_ONE ||
        read_u32_le(source + 64U) > GX5125_Q16_ONE) {
        return GX5125_ENROLLMENT_ERR_FORMAT;
    }

    expected_total_records = read_u32_le(source + 40U);
    expected_minimum_records = read_u32_le(source + 44U);
    expected_maximum_records = read_u32_le(source + 48U);
    expected_minimum_quality = read_u32_le(source + 52U);
    expected_minimum_coverage = read_u32_le(source + 56U);
    enrollment = gx5125_enrollment_create(&config);
    if (enrollment == NULL) {
        return GX5125_ENROLLMENT_ERR_MEMORY;
    }

    for (index = 0U; index < sample_count; ++index) {
        gx5125_enrollment_sample *sample;
        uint32_t record_count;
        uint32_t record_bytes;
        uint32_t packed_map_bytes;
        uint32_t quality;
        uint32_t coverage;
        uint32_t mask_coverage_q16;

        if (offset > source_size ||
            source_size - offset < GX5125_TEMPLATE_SAMPLE_HEADER_BYTES) {
            goto cleanup;
        }
        record_count = read_u32_le(source + offset + 0U);
        record_bytes = read_u32_le(source + offset + 4U);
        packed_map_bytes = read_u32_le(source + offset + 8U);
        quality = read_u32_le(source + offset + 12U);
        coverage = read_u32_le(source + offset + 16U);
        mask_coverage_q16 = read_u32_le(source + offset + 20U);
        offset += GX5125_TEMPLATE_SAMPLE_HEADER_BYTES;

        if (record_count == 0U ||
            record_count > GX5125_EXTRACTOR_MAX_RECORDS ||
            record_bytes !=
                record_count * GX5125_EXTRACTOR_RECORD_BYTES ||
            packed_map_bytes == 0U ||
            packed_map_bytes > GX5125_EXTRACTOR_PIXELS ||
            quality > 100U || coverage > 100U ||
            mask_coverage_q16 > GX5125_Q16_ONE ||
            offset > source_size ||
            (size_t)record_bytes > source_size - offset ||
            (size_t)packed_map_bytes >
                source_size - offset - (size_t)record_bytes) {
            goto cleanup;
        }

        sample = &enrollment->samples[index];
        sample->records = (uint8_t *)malloc(record_bytes);
        sample->packed_map = (uint8_t *)malloc(packed_map_bytes);
        if (sample->records == NULL || sample->packed_map == NULL) {
            status = GX5125_ENROLLMENT_ERR_MEMORY;
            goto cleanup;
        }
        memcpy(sample->records, source + offset, record_bytes);
        offset += record_bytes;
        memcpy(sample->packed_map, source + offset, packed_map_bytes);
        offset += packed_map_bytes;
        sample->record_count = record_count;
        sample->record_bytes = record_bytes;
        sample->packed_map_bytes = packed_map_bytes;
        sample->metrics.quality = quality;
        sample->metrics.coverage = coverage;
        sample->metrics.mask_coverage_q16 = mask_coverage_q16;
        ++enrollment->sample_count;
        enrollment->total_records += record_count;
        if (enrollment->sample_count == 1U) {
            enrollment->minimum_records = record_count;
            enrollment->maximum_records = record_count;
            enrollment->minimum_quality = quality;
            enrollment->minimum_coverage = coverage;
        } else {
            if (record_count < enrollment->minimum_records) {
                enrollment->minimum_records = record_count;
            }
            if (record_count > enrollment->maximum_records) {
                enrollment->maximum_records = record_count;
            }
            if (quality < enrollment->minimum_quality) {
                enrollment->minimum_quality = quality;
            }
            if (coverage < enrollment->minimum_coverage) {
                enrollment->minimum_coverage = coverage;
            }
        }
    }

    if (offset != source_size ||
        enrollment->total_records != expected_total_records ||
        enrollment->minimum_records != expected_minimum_records ||
        enrollment->maximum_records != expected_maximum_records ||
        enrollment->minimum_quality != expected_minimum_quality ||
        enrollment->minimum_coverage != expected_minimum_coverage) {
        goto cleanup;
    }
    enrollment->maximum_pair_similarity_q16 = read_u32_le(source + 60U);
    enrollment->minimum_pair_novelty_q16 = read_u32_le(source + 64U);
    *enrollment_out = enrollment;
    return GX5125_ENROLLMENT_OK;

cleanup:
    gx5125_enrollment_destroy(enrollment);
    return status;
}

const char *gx5125_enrollment_status_string(int status)
{
    switch (status) {
    case GX5125_ENROLLMENT_OK: return "ok";
    case GX5125_ENROLLMENT_ERR_ARGUMENT: return "invalid argument";
    case GX5125_ENROLLMENT_ERR_MEMORY: return "memory allocation failed";
    case GX5125_ENROLLMENT_ERR_STATE: return "invalid enrollment state";
    case GX5125_ENROLLMENT_ERR_FEATURE: return "invalid feature object";
    case GX5125_ENROLLMENT_ERR_FORMAT: return "invalid template format";
    case GX5125_ENROLLMENT_ERR_INTEGRITY: return "template integrity check failed";
    case GX5125_ENROLLMENT_ERR_CAPACITY: return "destination capacity too small";
    default: return "unknown enrollment error";
    }
}

const char *gx5125_enrollment_decision_string(
    gx5125_enrollment_decision decision)
{
    switch (decision) {
    case GX5125_ENROLLMENT_ACCEPTED: return "accepted";
    case GX5125_ENROLLMENT_REJECT_LOW_QUALITY: return "low-quality";
    case GX5125_ENROLLMENT_REJECT_LOW_COVERAGE: return "low-coverage";
    case GX5125_ENROLLMENT_REJECT_LOW_RECORD_COUNT: return "low-record-count";
    case GX5125_ENROLLMENT_REJECT_EXACT_DUPLICATE: return "exact-duplicate";
    case GX5125_ENROLLMENT_REJECT_COMPLETE: return "already-complete";
    default: return "unknown";
    }
}

int gx5125_enrollment_selftest(void)
{
    gx5125_enrollment_config config;
    gx5125_enrollment *enrollment;
    gx5125_enrollment_summary summary;

    gx5125_enrollment_default_config(&config);
    config.target_samples = 3U;
    enrollment = gx5125_enrollment_create(&config);
    if (enrollment == NULL || gx5125_enrollment_is_complete(enrollment) != 0 ||
        gx5125_enrollment_get_summary(enrollment, &summary) !=
            GX5125_ENROLLMENT_OK ||
        summary.target_samples != 3U || summary.accepted_samples != 0U) {
        gx5125_enrollment_destroy(enrollment);
        return -1;
    }
    gx5125_enrollment_reset(enrollment);
    gx5125_enrollment_destroy(enrollment);
    return 0;
}
