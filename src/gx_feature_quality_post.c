#include "gx5125/feature_quality_post.h"
#include "gx5125/feature_quality_map.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static int gx_valid_dims(int32_t width, int32_t height,
                         int32_t gray_stride, int32_t mask_stride) {
    return width >= 3 && height >= 3 && gray_stride >= width &&
           mask_stride >= width &&
           (size_t)height <= SIZE_MAX / (size_t)width;
}

static int32_t gx_min_i32(int32_t a, int32_t b) { return a < b ? a : b; }
static int32_t gx_max_i32(int32_t a, int32_t b) { return a > b ? a : b; }

int gx_feature_record_window_quality(const uint8_t *gray,
                                     const uint8_t *mask,
                                     int32_t width,
                                     int32_t height,
                                     int32_t gray_stride,
                                     int32_t mask_stride,
                                     const uint8_t *records,
                                     uint32_t record_count,
                                     uint8_t *quality_out) {
    size_t pixels;
    uint8_t *flat_gray = NULL;
    uint8_t *flat_mask = NULL;
    int32_t *gx = NULL;
    int32_t *gy = NULL;
    int32_t *magnitude = NULL;
    uint32_t *integral_mask = NULL;
    int32_t *integral_xx = NULL;
    int32_t *integral_yy = NULL;
    int32_t *integral_xy = NULL;
    uint32_t i;
    int rc = -1;

    if (gray == NULL || mask == NULL || quality_out == NULL ||
        (record_count != 0U && records == NULL) ||
        record_count > GX_FEATURE_MAX_RECORDS ||
        !gx_valid_dims(width, height, gray_stride, mask_stride)) return -1;

    pixels = (size_t)width * (size_t)height;
    flat_gray = (uint8_t *)malloc(pixels);
    flat_mask = (uint8_t *)malloc(pixels);
    gx = (int32_t *)malloc(pixels * sizeof(*gx));
    gy = (int32_t *)malloc(pixels * sizeof(*gy));
    magnitude = (int32_t *)malloc(pixels * sizeof(*magnitude));
    integral_mask = (uint32_t *)malloc(pixels * sizeof(*integral_mask));
    integral_xx = (int32_t *)malloc(pixels * sizeof(*integral_xx));
    integral_yy = (int32_t *)malloc(pixels * sizeof(*integral_yy));
    integral_xy = (int32_t *)malloc(pixels * sizeof(*integral_xy));
    if (flat_gray == NULL || flat_mask == NULL || gx == NULL || gy == NULL ||
        magnitude == NULL || integral_mask == NULL || integral_xx == NULL ||
        integral_yy == NULL || integral_xy == NULL) goto done;

    for (i = 0U; i < (uint32_t)height; ++i) {
        memcpy(flat_gray + (size_t)i * (size_t)width,
               gray + (size_t)i * (size_t)gray_stride, (size_t)width);
        memcpy(flat_mask + (size_t)i * (size_t)width,
               mask + (size_t)i * (size_t)mask_stride, (size_t)width);
    }

    if (gx_feature_quality_gradient(flat_gray, flat_mask, width, height,
                                    width, width, gx, gy, magnitude) != 0)
        goto done;
    if (gx_feature_quality_integrals(flat_mask, gx, gy, magnitude,
                                     width, height, integral_mask,
                                     integral_xx, integral_yy,
                                     integral_xy) != 0)
        goto done;

    for (i = 0U; i < record_count; ++i) {
        const uint8_t *record = records + (size_t)i * GX_FEATURE_RECORD_BYTES;
        const int32_t x = (int32_t)record[0x03];
        const int32_t y = (int32_t)record[0x05];
        const int32_t left = gx_max_i32(x - 16, 1);
        const int32_t top = gx_max_i32(y - 16, 1);
        const int32_t right = gx_min_i32(x + 16, width - 2);
        const int32_t bottom = gx_min_i32(y + 16, height - 2);
        const int32_t score = gx_feature_quality_window_score(
            integral_mask, integral_xx, integral_yy, integral_xy,
            width, height, left, top, right, bottom);
        const int32_t scaled = (int32_t)((int64_t)score * 100) >> 16;
        quality_out[i] = (uint8_t)scaled;
    }
    rc = 0;

done:
    free(flat_gray);
    free(flat_mask);
    free(gx);
    free(gy);
    free(magnitude);
    free(integral_mask);
    free(integral_xx);
    free(integral_yy);
    free(integral_xy);
    return rc;
}

/* Stack-table order reconstructed from the pinned Milan_v_3.02.00.15 code.
 * FUN_180016e80 indexes this table as table[255 - squared_distance]. */
static const uint8_t gx_neighbor_weight_table[256] = {
    1,1,1,1,1,1,1,1,1,1,1,1,2,2,2,2,
    2,2,2,2,2,2,2,2,2,2,2,2,2,3,3,3,
    3,3,3,3,3,3,3,3,3,3,4,4,4,4,4,4,
    4,4,4,4,4,5,5,5,5,5,5,5,5,5,6,6,
    6,6,6,6,6,6,7,7,7,7,7,7,7,8,8,8,
    8,8,8,8,9,9,9,9,9,9,10,10,10,10,10,10,
    11,11,11,11,11,12,12,12,12,12,12,13,13,13,13,13,
    14,14,14,14,14,15,15,15,15,16,16,16,16,16,17,17,
    17,17,18,18,18,18,18,19,19,19,19,20,20,20,20,21,
    21,21,21,22,22,22,22,22,23,23,23,23,24,24,24,24,
    25,25,25,25,26,26,26,26,27,27,27,27,28,28,28,28,
    28,29,29,29,29,30,30,30,30,31,31,31,31,31,32,32,
    32,32,32,33,33,33,33,33,34,34,34,34,34,35,35,35,
    35,35,35,36,36,36,36,36,36,36,37,37,37,37,37,37,
    37,38,38,38,38,38,38,38,38,38,39,39,39,39,39,39,
    39,39,39,39,39,39,39,39,39,39,39,39,39,39,39,39
};

int gx_feature_neighbor_quality(uint8_t *records,
                                uint32_t record_count,
                                int enabled,
                                const uint8_t *feature_quality,
                                const int8_t *input_scores,
                                int8_t *output_scores) {
    uint32_t i;

    if ((record_count != 0U && records == NULL) ||
        record_count > GX_FEATURE_MAX_RECORDS) return -1;

    if (!enabled) {
        for (i = 0U; i < record_count; ++i)
            records[(size_t)i * GX_FEATURE_RECORD_BYTES + 0x39U] = 0U;
        return 0;
    }

    if (feature_quality == NULL || input_scores == NULL ||
        output_scores == NULL) return -1;

    for (i = 0U; i < record_count; ++i) {
        const uint8_t *current = records + (size_t)i * GX_FEATURE_RECORD_BYTES;
        const int32_t current_x = (int32_t)current[0x03];
        const int32_t current_y = (int32_t)current[0x05];
        int32_t weight_sum = 0;
        int32_t score_sum = 0;
        int32_t divisor = 1;
        uint32_t j;

        for (j = 0U; j < record_count; ++j) {
            const uint8_t *other;
            int32_t dx;
            int32_t dy;
            int32_t distance_squared;
            int32_t weight;
            if (j == i) continue;
            other = records + (size_t)j * GX_FEATURE_RECORD_BYTES;
            dx = (int32_t)other[0x03] - current_x;
            dy = current_y - (int32_t)other[0x05];
            distance_squared = dx * dx + dy * dy;
            if (distance_squared == 0 || distance_squared > 255) continue;

            weight = gx_neighbor_weight_table[255 - distance_squared];
            if ((int8_t)feature_quality[j] > 50)
                weight = (weight * 12) >> 3;
            weight_sum += weight;
            score_sum += (int32_t)input_scores[j];
            divisor += 100;
        }

        output_scores[i] = (int8_t)((score_sum * 100) / divisor);
        weight_sum >>= 3;
        if (weight_sum > 100) weight_sum = 100;
        records[(size_t)i * GX_FEATURE_RECORD_BYTES + 0x39U] =
            (uint8_t)weight_sum;
    }
    return 0;
}
