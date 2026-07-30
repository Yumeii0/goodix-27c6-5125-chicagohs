#include "gx5125/preprocess.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define GX_PREPROC_VALID_MIN 50u
#define GX_PREPROC_VALID_MAX 4050u
#define GX_PREPROC_SENSOR_MAX 4095u
#define GX_PREPROC_MIN_VALID_PERCENT 85u
#define GX_PREPROC_DIFF_SEED_THRESHOLD 120
#define GX_PREPROC_MIN_FOREGROUND_FOR_QUALITY 6u

struct gx_preproc {
    gx_preproc_params params;
    uint16_t base[GX_PREPROC_PIXELS];
    uint8_t mask[GX_PREPROC_PIXELS];
    float signal[GX_PREPROC_PIXELS];
    float smooth[GX_PREPROC_PIXELS];
    float background[GX_PREPROC_PIXELS];
    float detail[GX_PREPROC_PIXELS];
    float sorted[GX_PREPROC_PIXELS];
    double integral[(GX_PREPROC_ROWS + 1u) * (GX_PREPROC_COLUMNS + 1u)];
    int initialized;
};

static uint16_t clamp_sensor_u16(uint16_t value) {
    return value > GX_PREPROC_SENSOR_MAX ? GX_PREPROC_SENSOR_MAX : value;
}

static uint8_t clamp_u8_from_double(double value) {
    if (value <= 0.0) {
        return 0u;
    }
    if (value >= 255.0) {
        return 255u;
    }
    return (uint8_t)lround(value);
}

static uint8_t clamp_quality(double value) {
    if (value <= 0.0) {
        return 0u;
    }
    if (value >= 100.0) {
        return 100u;
    }
    return (uint8_t)lround(value);
}

static int float_compare(const void *left, const void *right) {
    const float a = *(const float *)left;
    const float b = *(const float *)right;
    if (a < b) {
        return -1;
    }
    if (a > b) {
        return 1;
    }
    return 0;
}

static size_t integral_index(unsigned int x, unsigned int y) {
    return (size_t)y * (GX_PREPROC_COLUMNS + 1u) + x;
}

static void box_blur(gx_preproc *ctx,
                     const float source[GX_PREPROC_PIXELS],
                     float destination[GX_PREPROC_PIXELS],
                     unsigned int radius) {
    unsigned int y;
    unsigned int x;

    if (radius == 0u) {
        memcpy(destination, source, sizeof(float) * GX_PREPROC_PIXELS);
        return;
    }

    memset(ctx->integral, 0, sizeof(ctx->integral));
    for (y = 0u; y < GX_PREPROC_ROWS; ++y) {
        double row_sum = 0.0;
        for (x = 0u; x < GX_PREPROC_COLUMNS; ++x) {
            row_sum += source[(size_t)y * GX_PREPROC_COLUMNS + x];
            ctx->integral[integral_index(x + 1u, y + 1u)] =
                ctx->integral[integral_index(x + 1u, y)] + row_sum;
        }
    }

    for (y = 0u; y < GX_PREPROC_ROWS; ++y) {
        const unsigned int y0 = y > radius ? y - radius : 0u;
        const unsigned int y1 = (y + radius + 1u) < GX_PREPROC_ROWS
                                    ? y + radius + 1u
                                    : GX_PREPROC_ROWS;
        for (x = 0u; x < GX_PREPROC_COLUMNS; ++x) {
            const unsigned int x0 = x > radius ? x - radius : 0u;
            const unsigned int x1 = (x + radius + 1u) < GX_PREPROC_COLUMNS
                                        ? x + radius + 1u
                                        : GX_PREPROC_COLUMNS;
            const double sum =
                ctx->integral[integral_index(x1, y1)] -
                ctx->integral[integral_index(x0, y1)] -
                ctx->integral[integral_index(x1, y0)] +
                ctx->integral[integral_index(x0, y0)];
            const unsigned int count = (x1 - x0) * (y1 - y0);
            destination[(size_t)y * GX_PREPROC_COLUMNS + x] =
                (float)(sum / (double)count);
        }
    }
}

static int validate_base(const uint16_t base[GX_PREPROC_PIXELS]) {
    uint32_t valid = 0u;
    uint32_t total = 0u;
    unsigned int y;
    unsigned int x;

    /* Mirrors the ChicagoHS core check: ignore a two-pixel border and require
     * at least 85% of the remaining samples to be within a sane 12-bit range.
     */
    for (y = 2u; y + 2u < GX_PREPROC_ROWS; ++y) {
        for (x = 2u; x + 2u < GX_PREPROC_COLUMNS; ++x) {
            const uint16_t value = clamp_sensor_u16(
                base[(size_t)y * GX_PREPROC_COLUMNS + x]);
            ++total;
            if (value > GX_PREPROC_VALID_MIN && value < GX_PREPROC_VALID_MAX) {
                ++valid;
            }
        }
    }
    if (total == 0u) {
        return 0;
    }
    return valid * 100u > GX_PREPROC_MIN_VALID_PERCENT * total;
}

static uint16_t build_foreground_mask(gx_preproc *ctx,
                                      const uint16_t current[GX_PREPROC_PIXELS],
                                      uint32_t *foreground_pixels) {
    int64_t selected_sum = 0;
    uint32_t selected_count = 0u;
    uint32_t foreground = 0u;
    int threshold = GX_PREPROC_DIFF_SEED_THRESHOLD;
    size_t i;

    for (i = 0u; i < GX_PREPROC_PIXELS; ++i) {
        const int base_value = (int)ctx->base[i];
        const int current_value = (int)clamp_sensor_u16(current[i]);
        const int difference = base_value - current_value;
        if (difference > GX_PREPROC_DIFF_SEED_THRESHOLD) {
            selected_sum += difference;
            ++selected_count;
        }
    }

    if (selected_count > 0u) {
        const int average = (int)(selected_sum / (int64_t)selected_count);
        const int adaptive = average / 5;
        /* The vendor path uses the adaptive value directly when the changed
         * area is substantial, otherwise it retains a 120-count floor.
         */
        if (selected_count > GX_PREPROC_ROWS * 10u) {
            threshold = adaptive;
        } else if (adaptive > threshold) {
            threshold = adaptive;
        }
    }
    if (threshold < 1) {
        threshold = 1;
    }
    if (threshold > (int)GX_PREPROC_SENSOR_MAX) {
        threshold = (int)GX_PREPROC_SENSOR_MAX;
    }

    for (i = 0u; i < GX_PREPROC_PIXELS; ++i) {
        const uint16_t current_value = clamp_sensor_u16(current[i]);
        const int difference = (int)ctx->base[i] - (int)current_value;
        const bool valid = current_value != 0u &&
                           current_value != GX_PREPROC_SENSOR_MAX &&
                           difference >= threshold;
        ctx->mask[i] = valid ? 255u : 0u;
        if (valid) {
            ++foreground;
        }
    }

    *foreground_pixels = foreground;
    return (uint16_t)threshold;
}

static void calculate_gray_stats(const uint8_t gray[GX_PREPROC_PIXELS],
                                 double *mean,
                                 double *standard_deviation,
                                 uint32_t *zero_count,
                                 uint32_t *ff_count) {
    uint64_t sum = 0u;
    long double variance = 0.0L;
    size_t i;

    *zero_count = 0u;
    *ff_count = 0u;
    for (i = 0u; i < GX_PREPROC_PIXELS; ++i) {
        const uint8_t value = gray[i];
        sum += value;
        if (value == 0u) {
            ++*zero_count;
        }
        if (value == 255u) {
            ++*ff_count;
        }
    }
    *mean = (double)sum / (double)GX_PREPROC_PIXELS;
    for (i = 0u; i < GX_PREPROC_PIXELS; ++i) {
        const long double delta = (long double)gray[i] - (long double)*mean;
        variance += delta * delta;
    }
    *standard_deviation = sqrt((double)(variance / (long double)GX_PREPROC_PIXELS));
}

static double calculate_ridge_coherence(const uint8_t gray[GX_PREPROC_PIXELS],
                                        const uint8_t mask[GX_PREPROC_PIXELS]) {
    const unsigned int block_size = 8u;
    double weighted_sum = 0.0;
    double total_weight = 0.0;
    unsigned int by;
    unsigned int bx;

    for (by = 0u; by < GX_PREPROC_ROWS; by += block_size) {
        for (bx = 0u; bx < GX_PREPROC_COLUMNS; bx += block_size) {
            double gxx = 0.0;
            double gyy = 0.0;
            double gxy = 0.0;
            uint32_t count = 0u;
            unsigned int y;
            unsigned int x;
            const unsigned int y_end = (by + block_size) < GX_PREPROC_ROWS
                                           ? by + block_size
                                           : GX_PREPROC_ROWS;
            const unsigned int x_end = (bx + block_size) < GX_PREPROC_COLUMNS
                                           ? bx + block_size
                                           : GX_PREPROC_COLUMNS;

            for (y = by > 0u ? by : 1u; y < y_end && y + 1u < GX_PREPROC_ROWS; ++y) {
                for (x = bx > 0u ? bx : 1u; x < x_end && x + 1u < GX_PREPROC_COLUMNS; ++x) {
                    const size_t index = (size_t)y * GX_PREPROC_COLUMNS + x;
                    double gx;
                    double gy;
                    if (mask[index] == 0u) {
                        continue;
                    }
                    gx =
                        -(double)gray[index - GX_PREPROC_COLUMNS - 1u] +
                         (double)gray[index - GX_PREPROC_COLUMNS + 1u] -
                        2.0 * (double)gray[index - 1u] +
                        2.0 * (double)gray[index + 1u] -
                         (double)gray[index + GX_PREPROC_COLUMNS - 1u] +
                         (double)gray[index + GX_PREPROC_COLUMNS + 1u];
                    gy =
                        -(double)gray[index - GX_PREPROC_COLUMNS - 1u] -
                        2.0 * (double)gray[index - GX_PREPROC_COLUMNS] -
                         (double)gray[index - GX_PREPROC_COLUMNS + 1u] +
                         (double)gray[index + GX_PREPROC_COLUMNS - 1u] +
                        2.0 * (double)gray[index + GX_PREPROC_COLUMNS] +
                         (double)gray[index + GX_PREPROC_COLUMNS + 1u];
                    gxx += gx * gx;
                    gyy += gy * gy;
                    gxy += gx * gy;
                    ++count;
                }
            }

            if (count >= 12u && gxx + gyy > 1.0) {
                const double numerator = hypot(gxx - gyy, 2.0 * gxy);
                const double coherence = numerator / (gxx + gyy + 1e-12);
                const double weight = sqrt(gxx + gyy);
                weighted_sum += coherence * weight;
                total_weight += weight;
            }
        }
    }

    return total_weight > 0.0 ? weighted_sum / total_weight : 0.0;
}

void gx_preproc_default_params(gx_preproc_params *params) {
    if (params == NULL) {
        return;
    }
    params->signal_mode = GX_PREPROC_SIGNAL_RATIO;
    params->pre_blur_radius = 1u;
    params->background_radius = 3u;
    params->lower_clip_fraction = 0.110;
    params->upper_clip_fraction = 0.890;
    params->invert_output = 1;
}

static int validate_params(const gx_preproc_params *params) {
    if (params == NULL) {
        return 0;
    }
    if (params->signal_mode != GX_PREPROC_SIGNAL_DIFFERENCE &&
        params->signal_mode != GX_PREPROC_SIGNAL_RATIO) {
        return 0;
    }
    if (params->pre_blur_radius > 4u || params->background_radius < 2u ||
        params->background_radius > 12u) {
        return 0;
    }
    if (!(params->lower_clip_fraction >= 0.0 &&
          params->lower_clip_fraction < params->upper_clip_fraction &&
          params->upper_clip_fraction <= 1.0)) {
        return 0;
    }
    if (params->upper_clip_fraction - params->lower_clip_fraction < 0.25) {
        return 0;
    }
    return 1;
}

gx_preproc *gx_preproc_create(const gx_preproc_params *params) {
    gx_preproc_params selected;
    gx_preproc *ctx;

    if (params == NULL) {
        gx_preproc_default_params(&selected);
    } else {
        selected = *params;
    }
    if (!validate_params(&selected)) {
        return NULL;
    }

    ctx = (gx_preproc *)calloc(1u, sizeof(*ctx));
    if (ctx == NULL) {
        return NULL;
    }
    ctx->params = selected;
    return ctx;
}

void gx_preproc_destroy(gx_preproc *ctx) {
    if (ctx == NULL) {
        return;
    }
    memset(ctx, 0, sizeof(*ctx));
    free(ctx);
}

int gx_preproc_initialize(gx_preproc *ctx,
                          const uint16_t base[GX_PREPROC_PIXELS]) {
    size_t i;

    if (ctx == NULL || base == NULL) {
        return GX_PREPROC_ERR_ARGUMENT;
    }
    if (!validate_base(base)) {
        ctx->initialized = 0;
        return GX_PREPROC_ERR_BASE_INVALID;
    }
    for (i = 0u; i < GX_PREPROC_PIXELS; ++i) {
        ctx->base[i] = clamp_sensor_u16(base[i]);
    }
    ctx->initialized = 1;
    return GX_PREPROC_OK;
}

int gx_preproc_process(gx_preproc *ctx,
                       const uint16_t current[GX_PREPROC_PIXELS],
                       uint8_t gray[GX_PREPROC_PIXELS],
                       gx_preproc_metrics *metrics) {
    uint32_t foreground_pixels = 0u;
    uint16_t threshold;
    size_t sample_count = 0u;
    size_t low_index;
    size_t high_index;
    float low_value;
    float high_value;
    double scale;
    size_t i;
    gx_preproc_metrics local_metrics;

    if (ctx == NULL || current == NULL || gray == NULL) {
        return GX_PREPROC_ERR_ARGUMENT;
    }
    if (!ctx->initialized) {
        return GX_PREPROC_ERR_NOT_INITIALIZED;
    }

    memset(&local_metrics, 0, sizeof(local_metrics));
    threshold = build_foreground_mask(ctx, current, &foreground_pixels);
    local_metrics.foreground_threshold = threshold;
    local_metrics.foreground_pixels = foreground_pixels;
    local_metrics.foreground_fraction =
        (double)foreground_pixels / (double)GX_PREPROC_PIXELS;
    local_metrics.coverage = (uint8_t)((foreground_pixels * 100u) /
                                       GX_PREPROC_PIXELS);

    if (local_metrics.coverage < GX_PREPROC_MIN_FOREGROUND_FOR_QUALITY) {
        memset(gray, 255, GX_PREPROC_PIXELS);
        local_metrics.quality = 0u;
        calculate_gray_stats(gray,
                             &local_metrics.gray_mean,
                             &local_metrics.gray_standard_deviation,
                             &local_metrics.gray_zero_count,
                             &local_metrics.gray_ff_count);
        if (metrics != NULL) {
            *metrics = local_metrics;
        }
        return GX_PREPROC_OK;
    }

    for (i = 0u; i < GX_PREPROC_PIXELS; ++i) {
        const double base_value = (double)ctx->base[i];
        const double current_value = (double)clamp_sensor_u16(current[i]);
        const double difference = base_value - current_value;
        if (ctx->params.signal_mode == GX_PREPROC_SIGNAL_RATIO) {
            const double denominator = base_value > 256.0 ? base_value : 256.0;
            ctx->signal[i] = (float)(difference * 4096.0 / denominator);
        } else {
            ctx->signal[i] = (float)difference;
        }
    }

    box_blur(ctx, ctx->signal, ctx->smooth, ctx->params.pre_blur_radius);
    box_blur(ctx, ctx->smooth, ctx->background, ctx->params.background_radius);
    for (i = 0u; i < GX_PREPROC_PIXELS; ++i) {
        ctx->detail[i] = ctx->smooth[i] - ctx->background[i];
        if (ctx->mask[i] != 0u) {
            ctx->sorted[sample_count++] = ctx->detail[i];
        }
    }

    if (sample_count < 32u) {
        memset(gray, 255, GX_PREPROC_PIXELS);
        local_metrics.coverage = 0u;
        local_metrics.quality = 0u;
        if (metrics != NULL) {
            *metrics = local_metrics;
        }
        return GX_PREPROC_ERR_IMAGE_INVALID;
    }

    qsort(ctx->sorted, sample_count, sizeof(ctx->sorted[0]), float_compare);
    low_index = (size_t)floor((double)(sample_count - 1u) *
                              ctx->params.lower_clip_fraction);
    high_index = (size_t)ceil((double)(sample_count - 1u) *
                              ctx->params.upper_clip_fraction);
    if (high_index >= sample_count) {
        high_index = sample_count - 1u;
    }
    if (high_index <= low_index) {
        high_index = low_index + 1u;
    }
    low_value = ctx->sorted[low_index];
    high_value = ctx->sorted[high_index];
    if ((double)high_value - (double)low_value < 1e-6) {
        high_value = low_value + 1.0f;
    }
    scale = 255.0 / ((double)high_value - (double)low_value);

    for (i = 0u; i < GX_PREPROC_PIXELS; ++i) {
        uint8_t mapped;
        if (ctx->mask[i] == 0u) {
            gray[i] = 255u;
            continue;
        }
        mapped = clamp_u8_from_double(((double)ctx->detail[i] - low_value) * scale);
        gray[i] = ctx->params.invert_output ? (uint8_t)(255u - mapped) : mapped;
    }

    calculate_gray_stats(gray,
                         &local_metrics.gray_mean,
                         &local_metrics.gray_standard_deviation,
                         &local_metrics.gray_zero_count,
                         &local_metrics.gray_ff_count);
    local_metrics.ridge_coherence = calculate_ridge_coherence(gray, ctx->mask);
    {
        const double saturation =
            (double)(local_metrics.gray_zero_count + local_metrics.gray_ff_count) /
            (double)GX_PREPROC_PIXELS;
        double quality = 68.0 +
                         20.0 * local_metrics.ridge_coherence +
                         0.08 * (local_metrics.gray_standard_deviation - 70.0) +
                         0.04 * (double)local_metrics.coverage;
        if (saturation > 0.25) {
            quality -= (saturation - 0.25) * 40.0;
        }
        local_metrics.quality = clamp_quality(quality);
    }

    if (metrics != NULL) {
        *metrics = local_metrics;
    }
    return GX_PREPROC_OK;
}

int gx_preproc_is_initialized(const gx_preproc *ctx) {
    return ctx != NULL && ctx->initialized != 0;
}
