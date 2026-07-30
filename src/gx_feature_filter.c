#include "gx5125/feature_filter.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#define GX_PROFILE24_FILTER_FIRST_CODE 300
#define GX_PROFILE24_FILTER_LAST_CODE 308
#define GX_PROFILE24_FILTER_KERNEL_COUNT 9U
#define GX_IMAGE_PREPARE_FILTER_CODE 6
#define GX_IMAGE_PREPARE_FILTER_SUM 65535U
#define GX_AUXILIARY_FILTER_KERNEL_COUNT 3U
#define GX_AUXILIARY_FILTER_SUM 65536U

/* Exact DLL initialized-data rows used by the active profile-24 path.
 * Code 7 is called by FUN_180050800 before image preparation. Codes 0 and 1
 * are chained by FUN_180011330 after response-pyramid preparation; code 0 is
 * in-place and code 1 writes a second response image. All sums are exact Q16
 * unity and therefore remain within uint32_t accumulation range for u16 input. */
static const int32_t gx_auxiliary_kernel_code0[] = {
    84, 957, 5433, 15399, 21790, 15399, 5433, 957, 84
};
static const int32_t gx_auxiliary_kernel_code1[] = {
    139, 2672, 15742, 28430, 15742, 2672, 139
};
static const int32_t gx_auxiliary_kernel_code7[] = {
    3571, 16004, 26386, 16004, 3571
};

/* DLL initialized-data row DAT_1800937e0 + 6 * 0x40. The vendor fast
 * path FUN_18004fa40/FUN_18004fbf0 exploits that all three taps are equal,
 * but the arithmetic is exactly the same separable Q16 convolution. */
static const int32_t gx_image_prepare_kernel_code6[] = {
    21845, 21845, 21845
};

static const int32_t gx_profile24_kernel_300[] = {
    291, 3539, 15862, 26152, 15862, 3539, 291
};
static const int32_t gx_profile24_kernel_301[] = {
    77, 913, 5345, 15439, 21988, 15439, 5345, 913, 77
};
static const int32_t gx_profile24_kernel_302[] = {
    16, 1124, 14549, 34158, 14549, 1124, 16
};
static const int32_t gx_profile24_kernel_303[] = {
    126, 2569, 15710, 28726, 15710, 2569, 126
};
static const int32_t gx_profile24_kernel_304[] = {
    26, 519, 4382, 15764, 24155, 15764, 4382, 519, 26
};
static const int32_t gx_profile24_kernel_305[] = {
    163, 1344, 6077, 15025, 20318, 15025, 6077, 1344, 163
};
static const int32_t gx_profile24_kernel_306[] = {
    82, 562, 2503, 7276, 13802, 17086, 13802, 7276, 2503, 562, 82
};
static const int32_t gx_profile24_kernel_307[] = {
    63, 331, 1285, 3695, 7857, 12355, 14367, 12355, 7857, 3695, 1285,
    331, 63
};
static const int32_t gx_profile24_kernel_308[] = {
    65, 259, 839, 2192, 4625, 7886, 10860, 12084, 10860, 7886, 4625,
    2192, 839, 259, 65
};

typedef struct gx_feature_filter_kernel_entry {
    int32_t scale_code;
    const int32_t *coefficients;
    size_t coefficient_count;
    uint32_t coefficient_sum;
} gx_feature_filter_kernel_entry;

#define GX_ARRAY_COUNT(values) (sizeof(values) / sizeof((values)[0]))

static const gx_feature_filter_kernel_entry gx_auxiliary_kernels[] = {
    {0, gx_auxiliary_kernel_code0, GX_ARRAY_COUNT(gx_auxiliary_kernel_code0),
     GX_AUXILIARY_FILTER_SUM},
    {1, gx_auxiliary_kernel_code1, GX_ARRAY_COUNT(gx_auxiliary_kernel_code1),
     GX_AUXILIARY_FILTER_SUM},
    {7, gx_auxiliary_kernel_code7, GX_ARRAY_COUNT(gx_auxiliary_kernel_code7),
     GX_AUXILIARY_FILTER_SUM}
};

_Static_assert(GX_ARRAY_COUNT(gx_auxiliary_kernels) ==
                   GX_AUXILIARY_FILTER_KERNEL_COUNT,
               "auxiliary filter table count mismatch");

static const gx_feature_filter_kernel_entry gx_profile24_kernels[] = {
    {300, gx_profile24_kernel_300, GX_ARRAY_COUNT(gx_profile24_kernel_300), 65536U},
    {301, gx_profile24_kernel_301, GX_ARRAY_COUNT(gx_profile24_kernel_301), 65536U},
    {302, gx_profile24_kernel_302, GX_ARRAY_COUNT(gx_profile24_kernel_302), 65536U},
    {303, gx_profile24_kernel_303, GX_ARRAY_COUNT(gx_profile24_kernel_303), 65536U},
    {304, gx_profile24_kernel_304, GX_ARRAY_COUNT(gx_profile24_kernel_304), 65537U},
    {305, gx_profile24_kernel_305, GX_ARRAY_COUNT(gx_profile24_kernel_305), 65536U},
    {306, gx_profile24_kernel_306, GX_ARRAY_COUNT(gx_profile24_kernel_306), 65536U},
    {307, gx_profile24_kernel_307, GX_ARRAY_COUNT(gx_profile24_kernel_307), 65539U},
    {308, gx_profile24_kernel_308, GX_ARRAY_COUNT(gx_profile24_kernel_308), 65536U}
};

_Static_assert(GX_ARRAY_COUNT(gx_profile24_kernels) ==
                   GX_PROFILE24_FILTER_KERNEL_COUNT,
               "profile-24 filter table count mismatch");

int gx_feature_filter_profile24_kernel(
    int32_t scale_code,
    gx_feature_filter_kernel *kernel) {
    const int32_t index = scale_code - GX_PROFILE24_FILTER_FIRST_CODE;
    const gx_feature_filter_kernel_entry *entry;
    if (kernel == NULL || index < 0 ||
        index >= (int32_t)GX_PROFILE24_FILTER_KERNEL_COUNT) {
        return -1;
    }
    entry = &gx_profile24_kernels[(size_t)index];
    if (entry->scale_code != scale_code) return -1;
    kernel->scale_code = entry->scale_code;
    kernel->coefficients = entry->coefficients;
    kernel->coefficient_count = entry->coefficient_count;
    kernel->coefficient_sum = entry->coefficient_sum;
    return 0;
}

int gx_feature_filter_auxiliary_kernel(
    int32_t scale_code,
    gx_feature_filter_kernel *kernel) {
    size_t index;
    if (kernel == NULL) return -1;
    for (index = 0U; index < GX_ARRAY_COUNT(gx_auxiliary_kernels); ++index) {
        const gx_feature_filter_kernel_entry *entry =
            &gx_auxiliary_kernels[index];
        if (entry->scale_code == scale_code) {
            kernel->scale_code = entry->scale_code;
            kernel->coefficients = entry->coefficients;
            kernel->coefficient_count = entry->coefficient_count;
            kernel->coefficient_sum = entry->coefficient_sum;
            return 0;
        }
    }
    return -1;
}

int gx_feature_filter_auxiliary_validate(void) {
    static const int32_t expected_codes[GX_AUXILIARY_FILTER_KERNEL_COUNT] =
        {0, 1, 7};
    static const size_t expected_lengths[GX_AUXILIARY_FILTER_KERNEL_COUNT] =
        {9U, 7U, 5U};
    size_t table_index;
    for (table_index = 0U;
         table_index < GX_AUXILIARY_FILTER_KERNEL_COUNT; ++table_index) {
        gx_feature_filter_kernel kernel;
        uint32_t sum = 0U;
        size_t coefficient_index;
        memset(&kernel, 0, sizeof(kernel));
        if (gx_feature_filter_auxiliary_kernel(
                expected_codes[table_index], &kernel) != 0 ||
            kernel.scale_code != expected_codes[table_index] ||
            kernel.coefficients == NULL ||
            kernel.coefficient_count != expected_lengths[table_index] ||
            kernel.coefficient_sum != GX_AUXILIARY_FILTER_SUM ||
            (kernel.coefficient_count & 1U) == 0U) {
            return -1;
        }
        for (coefficient_index = 0U;
             coefficient_index < kernel.coefficient_count;
             ++coefficient_index) {
            const int32_t value = kernel.coefficients[coefficient_index];
            if (value < 0 ||
                value != kernel.coefficients[
                    kernel.coefficient_count - 1U - coefficient_index]) {
                return -1;
            }
            sum += (uint32_t)value;
        }
        if (sum != kernel.coefficient_sum) return -1;
    }
    {
        gx_feature_filter_kernel invalid;
        if (gx_feature_filter_auxiliary_kernel(2, &invalid) == 0 ||
            gx_feature_filter_auxiliary_kernel(6, &invalid) == 0 ||
            gx_feature_filter_auxiliary_kernel(8, &invalid) == 0) {
            return -1;
        }
    }
    return 0;
}

int gx_feature_filter_image_prepare_code6_kernel(
    gx_feature_filter_kernel *kernel) {
    if (kernel == NULL) return -1;
    kernel->scale_code = GX_IMAGE_PREPARE_FILTER_CODE;
    kernel->coefficients = gx_image_prepare_kernel_code6;
    kernel->coefficient_count = GX_ARRAY_COUNT(gx_image_prepare_kernel_code6);
    kernel->coefficient_sum = GX_IMAGE_PREPARE_FILTER_SUM;
    return 0;
}

int gx_feature_filter_image_prepare_code6_validate(void) {
    gx_feature_filter_kernel kernel;
    uint32_t sum = 0U;
    size_t index;
    memset(&kernel, 0, sizeof(kernel));
    if (gx_feature_filter_image_prepare_code6_kernel(&kernel) != 0 ||
        kernel.scale_code != GX_IMAGE_PREPARE_FILTER_CODE ||
        kernel.coefficients == NULL || kernel.coefficient_count != 3U ||
        kernel.coefficient_sum != GX_IMAGE_PREPARE_FILTER_SUM) {
        return -1;
    }
    for (index = 0U; index < kernel.coefficient_count; ++index) {
        const int32_t value = kernel.coefficients[index];
        if (value != 21845 ||
            value != kernel.coefficients[kernel.coefficient_count - 1U - index]) {
            return -1;
        }
        sum += (uint32_t)value;
    }
    return sum == kernel.coefficient_sum ? 0 : -1;
}

int gx_feature_filter_profile24_validate(void) {
    int32_t code;
    for (code = GX_PROFILE24_FILTER_FIRST_CODE;
         code <= GX_PROFILE24_FILTER_LAST_CODE; ++code) {
        gx_feature_filter_kernel kernel;
        uint32_t sum = 0U;
        size_t index;
        memset(&kernel, 0, sizeof(kernel));
        if (gx_feature_filter_profile24_kernel(code, &kernel) != 0 ||
            kernel.scale_code != code || kernel.coefficients == NULL ||
            kernel.coefficient_count == 0U ||
            (kernel.coefficient_count & 1U) == 0U) {
            return -1;
        }
        for (index = 0U; index < kernel.coefficient_count; ++index) {
            const int32_t value = kernel.coefficients[index];
            if (value < 0 ||
                value != kernel.coefficients[
                    kernel.coefficient_count - 1U - index]) {
                return -1;
            }
            sum += (uint32_t)value;
        }
        if (sum != kernel.coefficient_sum) return -1;
    }
    return 0;
}

static int reflect101(int index, int length) {
    if (length <= 1) return 0;
    while (index < 0 || index >= length) {
        if (index < 0) {
            index = -index;
        } else {
            index = length * 2 - index - 2;
        }
    }
    return index;
}

static int validate_image(const gx_feature_pyramid_image *image) {
    int64_t expected;
    if (image == NULL || image->pixels == NULL || image->width <= 0 ||
        image->height <= 0 || image->element_bytes != 2 ||
        image->element_count <= 0) {
        return -1;
    }
    expected = (int64_t)image->width * (int64_t)image->height;
    if (expected <= 0 || expected > INT32_MAX ||
        image->element_count < (int32_t)expected) {
        return -1;
    }
    return 0;
}

int gx_feature_filter_u16_q16_reflect101(
    const gx_feature_pyramid_image *source,
    gx_feature_pyramid_image *destination,
    const int32_t *coefficients,
    size_t coefficient_count,
    gx_feature_filter_stats *stats) {
    const uint16_t *input;
    uint16_t *output;
    uint32_t *horizontal;
    size_t pixel_count;
    size_t center;
    size_t index;
    uint32_t kernel_sum = 0U;
    int y;

    if (stats != NULL) memset(stats, 0, sizeof(*stats));
    if (validate_image(source) != 0 || validate_image(destination) != 0 ||
        source->width != destination->width ||
        source->height != destination->height || coefficients == NULL ||
        coefficient_count == 0U || (coefficient_count & 1U) == 0U ||
        coefficient_count > (size_t)INT_MAX) {
        return -1;
    }
    for (index = 0U; index < coefficient_count; ++index) {
        if (coefficients[index] < 0) return -1;
        kernel_sum += (uint32_t)coefficients[index];
        if (coefficients[index] !=
            coefficients[coefficient_count - 1U - index]) {
            return -1;
        }
    }
    pixel_count = (size_t)source->width * (size_t)source->height;
    if (pixel_count > SIZE_MAX / sizeof(*horizontal)) return -1;
    horizontal = (uint32_t *)malloc(pixel_count * sizeof(*horizontal));
    if (horizontal == NULL) return -1;

    input = (const uint16_t *)source->pixels;
    output = (uint16_t *)destination->pixels;
    center = coefficient_count / 2U;

    for (y = 0; y < source->height; ++y) {
        int x;
        for (x = 0; x < source->width; ++x) {
            uint32_t sum = 0U;
            size_t k;
            for (k = 0U; k < coefficient_count; ++k) {
                const int sx = reflect101(
                    x + (int)k - (int)center, source->width);
                const uint32_t sample =
                    input[(size_t)y * (size_t)source->width + (size_t)sx];
                sum += sample * (uint32_t)coefficients[k];
            }
            horizontal[(size_t)y * (size_t)source->width + (size_t)x] =
                sum >> 16U;
        }
    }

    for (y = 0; y < source->height; ++y) {
        int x;
        for (x = 0; x < source->width; ++x) {
            uint32_t sum = 0U;
            size_t k;
            for (k = 0U; k < coefficient_count; ++k) {
                const int sy = reflect101(
                    y + (int)k - (int)center, source->height);
                const uint32_t sample =
                    horizontal[(size_t)sy * (size_t)source->width +
                               (size_t)x];
                sum += sample * (uint32_t)coefficients[k];
            }
            output[(size_t)y * (size_t)source->width + (size_t)x] =
                (uint16_t)(sum >> 16U);
        }
    }

    if (stats != NULL) {
        stats->horizontal_passes = 1U;
        stats->vertical_passes = 1U;
        stats->kernel_length = (uint32_t)coefficient_count;
        stats->kernel_sum = kernel_sum;
    }
    free(horizontal);
    return 0;
}
int gx_feature_filter_image_prepare_code6_u16_reflect101(
    const gx_feature_pyramid_image *source,
    gx_feature_pyramid_image *destination,
    gx_feature_filter_stats *stats) {
    const uint16_t *input;
    uint16_t *output;
    uint32_t *horizontal;
    size_t pixel_count;
    int y;

    if (stats != NULL) memset(stats, 0, sizeof(*stats));
    if (validate_image(source) != 0 || validate_image(destination) != 0 ||
        source->width != destination->width ||
        source->height != destination->height) {
        return -1;
    }
    pixel_count = (size_t)source->width * (size_t)source->height;
    if (pixel_count > SIZE_MAX / sizeof(*horizontal)) return -1;
    horizontal = (uint32_t *)malloc(pixel_count * sizeof(*horizontal));
    if (horizontal == NULL) return -1;
    input = (const uint16_t *)source->pixels;
    output = (uint16_t *)destination->pixels;

    for (y = 0; y < source->height; ++y) {
        int x;
        for (x = 0; x < source->width; ++x) {
            const int x0 = reflect101(x - 1, source->width);
            const int x2 = reflect101(x + 1, source->width);
            const size_t row = (size_t)y * (size_t)source->width;
            const uint32_t sample_sum =
                (uint32_t)input[row + (size_t)x0] +
                (uint32_t)input[row + (size_t)x] +
                (uint32_t)input[row + (size_t)x2];
            horizontal[row + (size_t)x] =
                (sample_sum * UINT32_C(21845)) >> 16U;
        }
    }
    for (y = 0; y < source->height; ++y) {
        int x;
        const int y0 = reflect101(y - 1, source->height);
        const int y2 = reflect101(y + 1, source->height);
        for (x = 0; x < source->width; ++x) {
            const size_t column = (size_t)x;
            const uint32_t sample_sum =
                horizontal[(size_t)y0 * (size_t)source->width + column] +
                horizontal[(size_t)y * (size_t)source->width + column] +
                horizontal[(size_t)y2 * (size_t)source->width + column];
            output[(size_t)y * (size_t)source->width + column] =
                (uint16_t)((sample_sum * UINT32_C(21845)) >> 16U);
        }
    }
    if (stats != NULL) {
        stats->horizontal_passes = 1U;
        stats->vertical_passes = 1U;
        stats->kernel_length = 3U;
        stats->kernel_sum = GX_IMAGE_PREPARE_FILTER_SUM;
    }
    free(horizontal);
    return 0;
}

