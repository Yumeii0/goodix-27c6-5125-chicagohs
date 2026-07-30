#include "gx5125/feature_root.h"

#include <limits.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "gx5125/feature_descriptor_lifecycle.h"
#include "gx5125/feature_filter.h"
#include "gx5125/feature_response.h"

int gx_feature_profile24_parameters_build(
    uint32_t output[GX_FEATURE_PROFILE24_PARAMETER_WORDS],
    const uint32_t template_a[4], const uint32_t template_b[4],
    uint32_t capacity, uint32_t quality, uint32_t coverage) {
    if (output == NULL || template_a == NULL || template_b == NULL ||
        capacity == 0U || capacity > GX_FEATURE_PROFILE24_MAX_RECORDS) {
        return -1;
    }

    memset(output, 0,
           GX_FEATURE_PROFILE24_PARAMETER_WORDS * sizeof(output[0]));
    output[0] = GX_FEATURE_PROFILE24_ID;
    output[1] = capacity;
    output[2] = GX_FEATURE_PROFILE24_MAX_RECORDS;
    output[3] = GX_FEATURE_PROFILE24_EDGE_THRESHOLD;
    output[4] = 1U;
    memcpy(output + 5U, template_a, 4U * sizeof(output[0]));
    output[5] = GX_FEATURE_PROFILE24_SCALE_Q16;
    memcpy(output + 9U, template_b, 4U * sizeof(output[0]));
    output[13] = quality;
    output[14] = coverage;
    return 0;
}


static int32_t root_wrap_add_i32(int32_t left, int32_t right) {
    return (int32_t)((uint32_t)left + (uint32_t)right);
}

static int32_t root_wrap_sub_i32(int32_t left, int32_t right) {
    return (int32_t)((uint32_t)left - (uint32_t)right);
}

static int32_t root_wrap_mul_i32(int32_t left, int32_t right) {
    return (int32_t)((uint32_t)left * (uint32_t)right);
}

static int32_t root_integral_rectangle(const int32_t *integral,
                                       int32_t width,
                                       int32_t x0, int32_t y0,
                                       int32_t x1, int32_t y1) {
    const size_t bottom_right = (size_t)y1 * (size_t)width + (size_t)x1;
    const size_t top_right = (size_t)(y0 - 1) * (size_t)width + (size_t)x1;
    const size_t bottom_left = (size_t)y1 * (size_t)width + (size_t)(x0 - 1);
    const size_t top_left = (size_t)(y0 - 1) * (size_t)width + (size_t)(x0 - 1);
    uint32_t value = (uint32_t)integral[bottom_right];
    value -= (uint32_t)integral[top_right];
    value -= (uint32_t)integral[bottom_left];
    value += (uint32_t)integral[top_left];
    return (int32_t)value;
}

int gx_feature_optional_orientation_kernel(
    const uint8_t *source, uint8_t *workspace,
    int32_t width, int32_t height, uint32_t radius) {
    int32_t *gradient_x = NULL;
    int32_t *gradient_y = NULL;
    int32_t *double_xy = NULL;
    int32_t *difference_square = NULL;
    int32_t *integral_xy = NULL;
    int32_t *integral_difference = NULL;
    size_t count;
    int32_t y;

    if (source == NULL || workspace == NULL || width < 3 || height < 3 ||
        radius == 0U || radius > INT32_MAX) {
        return -1;
    }
    if ((size_t)width > SIZE_MAX / (size_t)height) {
        return -1;
    }
    count = (size_t)width * (size_t)height;
    if (count > GX_FEATURE_OPTIONAL_WORKSPACE_BYTES ||
        count > SIZE_MAX / sizeof(int32_t)) {
        return -1;
    }
    gradient_x = (int32_t *)calloc(count, sizeof(int32_t));
    gradient_y = (int32_t *)calloc(count, sizeof(int32_t));
    double_xy = (int32_t *)calloc(count, sizeof(int32_t));
    difference_square = (int32_t *)calloc(count, sizeof(int32_t));
    if (gradient_x == NULL || gradient_y == NULL || double_xy == NULL ||
        difference_square == NULL) {
        free(gradient_x);
        free(gradient_y);
        free(double_xy);
        free(difference_square);
        return -1;
    }

    for (y = 1; y < height - 1; ++y) {
        int32_t x;
        for (x = 1; x < width - 1; ++x) {
            const size_t index = (size_t)y * (size_t)width + (size_t)x;
            const uint32_t top_left = source[index - (size_t)width - 1U];
            const uint32_t top_center = source[index - (size_t)width];
            const uint32_t top_right = source[index - (size_t)width + 1U];
            const uint32_t middle_left = source[index - 1U];
            const uint32_t middle_right = source[index + 1U];
            const uint32_t bottom_left = source[index + (size_t)width - 1U];
            const uint32_t bottom_center = source[index + (size_t)width];
            const uint32_t bottom_right = source[index + (size_t)width + 1U];
            gradient_x[index] =
                (int32_t)(middle_right - middle_left) * 2 -
                (int32_t)bottom_left - (int32_t)top_left +
                (int32_t)top_right + (int32_t)bottom_right;
            gradient_y[index] =
                (int32_t)(bottom_center - top_center) * 2 -
                (int32_t)top_right - (int32_t)top_left +
                (int32_t)bottom_left + (int32_t)bottom_right;
        }
    }

    for (size_t index = 0U; index < count; ++index) {
        const int32_t product = root_wrap_mul_i32(
            gradient_x[index], gradient_y[index]);
        const int32_t square_x = root_wrap_mul_i32(
            gradient_x[index], gradient_x[index]);
        const int32_t square_y = root_wrap_mul_i32(
            gradient_y[index], gradient_y[index]);
        double_xy[index] = root_wrap_add_i32(product, product);
        difference_square[index] = root_wrap_sub_i32(square_x, square_y);
    }
    free(gradient_x);
    free(gradient_y);
    gradient_x = NULL;
    gradient_y = NULL;

    integral_xy = (int32_t *)calloc(count, sizeof(int32_t));
    integral_difference = (int32_t *)calloc(count, sizeof(int32_t));
    if (integral_xy == NULL || integral_difference == NULL) {
        free(double_xy);
        free(difference_square);
        free(integral_xy);
        free(integral_difference);
        return -1;
    }

    for (y = 1; y < height; ++y) {
        int32_t x;
        for (x = 1; x < width; ++x) {
            const size_t index = (size_t)y * (size_t)width + (size_t)x;
            const size_t left = index - 1U;
            const size_t above = index - (size_t)width;
            const size_t above_left = above - 1U;
            uint32_t xy = (uint32_t)double_xy[index];
            uint32_t difference = (uint32_t)difference_square[index];
            xy += (uint32_t)integral_xy[left];
            xy += (uint32_t)integral_xy[above];
            xy -= (uint32_t)integral_xy[above_left];
            difference += (uint32_t)integral_difference[left];
            difference += (uint32_t)integral_difference[above];
            difference -= (uint32_t)integral_difference[above_left];
            integral_xy[index] = (int32_t)xy;
            integral_difference[index] = (int32_t)difference;
        }
    }
    free(double_xy);
    free(difference_square);
    double_xy = NULL;
    difference_square = NULL;

    for (y = 0; y < height; ++y) {
        int32_t x;
        const int32_t low_y_candidate = y - (int32_t)radius;
        const int32_t high_y_candidate = y + (int32_t)radius;
        const int32_t low_y = low_y_candidate > 1 ? low_y_candidate : 1;
        const int32_t high_y = high_y_candidate < height - 1 ?
            high_y_candidate : height - 1;
        for (x = 0; x < width; ++x) {
            const int32_t low_x_candidate = x - (int32_t)radius;
            const int32_t high_x_candidate = x + (int32_t)radius;
            const int32_t low_x = low_x_candidate > 1 ? low_x_candidate : 1;
            const int32_t high_x = high_x_candidate < width - 1 ?
                high_x_candidate : width - 1;
            const int32_t sum_xy = root_integral_rectangle(
                integral_xy, width, low_x, low_y, high_x, high_y);
            const int32_t sum_difference = root_integral_rectangle(
                integral_difference, width, low_x, low_y, high_x, high_y);
            int32_t magnitude = 0;
            int32_t angle = (int32_t)(int16_t)
                gx_feature_vector_angle_magnitude(
                    sum_difference, sum_xy, &magnitude);
            int32_t degrees;
            int32_t rotated;
            if (angle < 0) {
                angle += (int32_t)GX_FEATURE_VECTOR_ANGLE_PERIOD;
            }
            degrees = (int32_t)(((int64_t)angle * INT64_C(0x1ca6)) >> 20U);
            rotated = degrees - 135;
            if (rotated < 1) {
                rotated = degrees + 45;
            }
            workspace[(size_t)y * (size_t)width + (size_t)x] =
                (uint8_t)(180 - rotated);
        }
    }

    free(integral_xy);
    free(integral_difference);
    return 0;
}

int gx_feature_optional_reconstruct_kernel(
    const uint8_t *workspace, const uint8_t *source, uint8_t *destination,
    int32_t width, int32_t height) {
    static const int32_t weights[7] = {1, 2, 4, 8, 4, 2, 1};
    static const int8_t offsets[12][7][2] = {
        {{-3,0},{-2,0},{-1,0},{0,0},{1,0},{2,0},{3,0}},
        {{-3,-1},{-2,-1},{-1,0},{0,0},{1,0},{2,1},{3,1}},
        {{-3,-2},{-2,-1},{-1,-1},{0,0},{1,1},{2,1},{3,2}},
        {{-3,-3},{-2,-2},{-1,-1},{0,0},{1,1},{2,2},{3,3}},
        {{-2,-3},{-1,-2},{-1,-1},{0,0},{1,1},{1,2},{2,3}},
        {{-1,-3},{-1,-2},{0,-1},{0,0},{0,1},{1,2},{1,3}},
        {{0,-3},{0,-2},{0,-1},{0,0},{0,1},{0,2},{0,3}},
        {{-1,3},{-1,2},{0,1},{0,0},{0,-1},{1,-2},{1,-3}},
        {{-2,3},{-1,2},{-1,1},{0,0},{1,-1},{1,-2},{2,-3}},
        {{-3,3},{-2,2},{-1,1},{0,0},{1,-1},{2,-2},{3,-3}},
        {{-3,2},{-2,1},{-1,1},{0,0},{1,-1},{2,-1},{3,-2}},
        {{-3,1},{-2,1},{-1,0},{0,0},{1,0},{2,-1},{3,-1}}
    };
    int32_t y;

    if (workspace == NULL || source == NULL || destination == NULL ||
        width <= 0 || height <= 0) {
        return -1;
    }

    for (y = 0; y < height; ++y) {
        int32_t x;
        for (x = 0; x < width; ++x) {
            const size_t index = (size_t)y * (size_t)width + (size_t)x;
            const uint8_t wrapped = (uint8_t)(workspace[index] - 8U);
            int32_t direction = 0;
            int32_t weighted_sum = 0;
            int32_t weight_sum = 0;
            int sample;
            if (wrapped < UINT8_C(0xa5)) {
                direction = (int32_t)(workspace[index] - 8U) / 15 + 1;
            }
            for (sample = 0; sample < 7; ++sample) {
                const int32_t sample_x = x + offsets[direction][sample][0];
                const int32_t sample_y = y + offsets[direction][sample][1];
                if (sample_x >= 0 && sample_x < width &&
                    sample_y >= 0 && sample_y < height) {
                    weighted_sum +=
                        (int32_t)source[(size_t)sample_y * (size_t)width +
                                        (size_t)sample_x] * weights[sample];
                    weight_sum += weights[sample];
                }
            }
            destination[index] = weight_sum == 0 ? UINT8_C(0xff) :
                (uint8_t)(weighted_sum / weight_sum);
        }
    }
    return 0;
}



#define GX_ROOT_MAP_ADAPTIVE_PRIMARY_OFFSET 0x08U
#define GX_ROOT_MAP_ADAPTIVE_OPTIONAL_OFFSET 0x10U
#define GX_ROOT_MAP_FIXED_PRIMARY_OFFSET 0x18U
#define GX_ROOT_MAP_WORK_MASK_OFFSET 0x20U
#define GX_ROOT_MASK_BOX_RADIUS 7
#define GX_ROOT_MASK_BOX_DIAMETER (GX_ROOT_MASK_BOX_RADIUS * 2 + 1)
#define GX_ROOT_MASK_BOX_RECIPROCAL_Q16 UINT32_C(291)

static int root_reflect101(int32_t index, int32_t length) {
    if (length <= 1) return 0;
    while (index < 0 || index >= length) {
        if (index < 0) index = -index;
        else index = length * 2 - index - 2;
    }
    return index;
}

static gx_feature_pyramid_image *root_read_descriptor_slot(
    const uint8_t *object, size_t offset) {
    gx_feature_pyramid_image *value = NULL;
    if (object != NULL) memcpy(&value, object + offset, sizeof(value));
    return value;
}

static void root_write_descriptor_slot(uint8_t *object, size_t offset,
                                       gx_feature_pyramid_image *value) {
    memcpy(object + offset, &value, sizeof(value));
}

static int root_descriptor_release_local(
    gx_feature_pyramid_image **descriptor,
    gx_feature_descriptor_release_fn descriptor_release) {
    if (descriptor == NULL || *descriptor == NULL) return 0;
    if (descriptor_release == NULL || descriptor_release(descriptor) != 0U ||
        *descriptor != NULL) {
        return -1;
    }
    return 0;
}

static int root_descriptor_payload_bytes(int32_t width, int32_t height,
                                         int32_t element_type,
                                         size_t *bytes) {
    size_t pixels;
    if (bytes == NULL || width <= 0 || height <= 0 ||
        (element_type != 1 && element_type != 2 && element_type != 4 &&
         element_type != 8) ||
        (size_t)width > SIZE_MAX / (size_t)height) {
        return -1;
    }
    pixels = (size_t)width * (size_t)height;
    if (element_type == 8) {
        *bytes = (pixels + 7U) / 8U;
    } else {
        if (pixels > SIZE_MAX / (size_t)element_type) return -1;
        *bytes = pixels * (size_t)element_type;
    }
    return *bytes > (size_t)INT32_MAX ? -1 : 0;
}

static int root_ensure_descriptor_slot(
    uint8_t *object, size_t offset,
    int32_t width, int32_t height, int32_t element_type,
    gx_feature_descriptor_alloc_fn descriptor_alloc,
    gx_feature_descriptor_release_fn descriptor_release,
    gx_feature_pyramid_image **result) {
    gx_feature_pyramid_image *descriptor;
    size_t required;
    if (object == NULL || descriptor_alloc == NULL || result == NULL ||
        root_descriptor_payload_bytes(width, height, element_type,
                                      &required) != 0) {
        return -1;
    }
    descriptor = root_read_descriptor_slot(object, offset);
    if (descriptor != NULL &&
        (descriptor->element_count < 0 ||
         (size_t)descriptor->element_count < required)) {
        if (root_descriptor_release_local(&descriptor, descriptor_release) != 0)
            return -1;
        root_write_descriptor_slot(object, offset, NULL);
    }
    if (descriptor == NULL) {
        descriptor = descriptor_alloc(width, height, element_type);
        if (descriptor == NULL || descriptor->pixels == NULL) return -1;
        root_write_descriptor_slot(object, offset, descriptor);
    }
    if (descriptor->pixels == NULL || descriptor->element_count < 0 ||
        (size_t)descriptor->element_count < required) {
        return -1;
    }
    *result = descriptor;
    return 0;
}

static int root_copy_descriptor_to_slot(
    const gx_feature_pyramid_image *source, uint8_t *object, size_t offset,
    gx_feature_descriptor_alloc_fn descriptor_alloc,
    gx_feature_descriptor_release_fn descriptor_release) {
    gx_feature_pyramid_image *destination;
    if (source == NULL || source->pixels == NULL || source->element_count < 0 ||
        root_ensure_descriptor_slot(object, offset, source->width,
                                    source->height, source->element_bytes,
                                    descriptor_alloc, descriptor_release,
                                    &destination) != 0) {
        return -1;
    }
    destination->width = source->width;
    destination->height = source->height;
    destination->reserved_08 = source->reserved_08;
    destination->element_count = source->element_count;
    destination->element_bytes = source->element_bytes;
    if (source->element_count > 0) {
        memcpy(destination->pixels, source->pixels,
               (size_t)source->element_count);
    }
    return 0;
}

static uint8_t root_quantile_threshold(
    const gx_feature_pyramid_image *image,
    const gx_feature_pyramid_image *mask,
    int32_t alternate_mode) {
    uint32_t histogram[255];
    uint32_t included = 0U;
    uint32_t target;
    uint32_t cumulative = 0U;
    uint32_t previous = 0U;
    uint32_t value;
    const uint8_t *pixels;
    const uint8_t *mask_pixels;
    size_t count;

    memset(histogram, 0, sizeof(histogram));
    if (image == NULL || mask == NULL || image->pixels == NULL ||
        mask->pixels == NULL || image->element_bytes != 1 ||
        mask->element_bytes != 1 || image->element_count <= 0 ||
        mask->element_count < image->element_count) {
        return 0U;
    }
    pixels = (const uint8_t *)image->pixels;
    mask_pixels = (const uint8_t *)mask->pixels;
    count = (size_t)image->element_count;
    for (size_t index = 0U; index < count; ++index) {
        if (mask_pixels[index] != 0U && pixels[index] != 0U) {
            ++histogram[(size_t)pixels[index] - 1U];
            ++included;
        }
    }
    target = (((alternate_mode == 0 ? UINT32_C(0xcd) : UINT32_C(0x32)) *
               included) + UINT32_C(0x80)) >> 8U;
    for (value = 1U; value < 256U; ++value) {
        previous = cumulative;
        cumulative += histogram[value - 1U];
        if (target <= cumulative) {
            return (cumulative - target <= target - previous) ?
                (uint8_t)value : (uint8_t)(value - 1U);
        }
    }
    return 0U;
}

static int root_pack_threshold_map(
    const gx_feature_pyramid_image *source,
    gx_feature_pyramid_image *destination,
    uint8_t threshold, int inclusive) {
    const uint8_t *pixels;
    uint8_t *packed;
    size_t count;
    size_t packed_bytes;
    if (source == NULL || destination == NULL || source->pixels == NULL ||
        destination->pixels == NULL || source->element_bytes != 1 ||
        destination->element_bytes != 8 || source->width != destination->width ||
        source->height != destination->height || source->element_count <= 0 ||
        destination->element_count <= 0) {
        return -1;
    }
    count = (size_t)source->width * (size_t)source->height;
    packed_bytes = (count + 7U) / 8U;
    if ((size_t)source->element_count < count ||
        (size_t)destination->element_count < packed_bytes) {
        return -1;
    }
    pixels = (const uint8_t *)source->pixels;
    packed = (uint8_t *)destination->pixels;
    memset(packed, 0, packed_bytes);
    for (size_t index = 0U; index < count; ++index) {
        const int set = inclusive ? pixels[index] >= threshold :
                                    pixels[index] > threshold;
        if (set) packed[index >> 3U] |= (uint8_t)(UINT8_C(1) << (index & 7U));
    }
    return 0;
}

static int root_subsample_even_bytes(
    const gx_feature_pyramid_image *source,
    gx_feature_pyramid_image *destination) {
    int32_t y;
    if (source == NULL || destination == NULL || source->pixels == NULL ||
        destination->pixels == NULL || source->width < 2 || source->height < 2 ||
        destination->width != source->width / 2 ||
        destination->height != source->height / 2 ||
        source->reserved_08 <= 0 || destination->reserved_08 <= 0) {
        return -1;
    }
    for (y = 0; y < destination->height; ++y) {
        int32_t x;
        for (x = 0; x < destination->width; ++x) {
            const size_t source_offset =
                ((size_t)source->reserved_08 * (size_t)y + (size_t)x) * 2U;
            const size_t destination_offset =
                (size_t)destination->reserved_08 * (size_t)y + (size_t)x;
            if (source_offset >= (size_t)source->element_count ||
                destination_offset >= (size_t)destination->element_count) {
                return -1;
            }
            ((uint8_t *)destination->pixels)[destination_offset] =
                ((const uint8_t *)source->pixels)[source_offset];
        }
    }
    return 0;
}

int gx_feature_root_mask_prepare(
    const gx_feature_pyramid_image *gray,
    gx_feature_pyramid_image *mask,
    int32_t enabled, uint16_t threshold, uint8_t fill_value,
    gx_feature_descriptor_alloc_fn descriptor_alloc,
    gx_feature_descriptor_release_fn descriptor_release,
    gx_feature_root_filter_fn filter_execute, void *filter_context,
    uint32_t *coverage_q16) {
    gx_feature_pyramid_image *input_u16 = NULL;
    gx_feature_pyramid_image *filtered_u16 = NULL;
    gx_feature_pyramid_image *filtered_u8 = NULL;
    gx_feature_pyramid_image *gradient_x = NULL;
    gx_feature_pyramid_image *gradient_y = NULL;
    gx_feature_pyramid_image *magnitude = NULL;
    gx_feature_pyramid_image *smoothed = NULL;
    size_t pixel_count;
    uint32_t positive = 0U;
    int rc = -1;

    if (coverage_q16 != NULL) *coverage_q16 = 0U;
    if (gray == NULL || gray->pixels == NULL || gray->width < 3 ||
        gray->height < 3 || gray->element_bytes != 1 ||
        gray->reserved_08 < gray->width || gray->element_count <= 0 ||
        descriptor_alloc == NULL || descriptor_release == NULL ||
        filter_execute == NULL ||
        (enabled != 0 && (mask == NULL || mask->pixels == NULL ||
                          mask->element_bytes != 1 ||
                          mask->width != gray->width ||
                          mask->height != gray->height)) ||
        (size_t)gray->width > SIZE_MAX / (size_t)gray->height) {
        return -1;
    }
    pixel_count = (size_t)gray->width * (size_t)gray->height;
    if ((size_t)gray->element_count < pixel_count) return -1;

    input_u16 = descriptor_alloc(gray->width, gray->height, 2);
    filtered_u16 = descriptor_alloc(gray->width, gray->height, 2);
    filtered_u8 = descriptor_alloc(gray->width, gray->height, 1);
    gradient_x = descriptor_alloc(gray->width, gray->height, 2);
    gradient_y = descriptor_alloc(gray->width, gray->height, 2);
    magnitude = descriptor_alloc(gray->width, gray->height, 2);
    smoothed = descriptor_alloc(gray->width, gray->height, 2);
    if (input_u16 == NULL || filtered_u16 == NULL || filtered_u8 == NULL ||
        gradient_x == NULL || gradient_y == NULL || magnitude == NULL ||
        smoothed == NULL || input_u16->pixels == NULL ||
        filtered_u16->pixels == NULL || filtered_u8->pixels == NULL ||
        gradient_x->pixels == NULL || gradient_y->pixels == NULL ||
        magnitude->pixels == NULL || smoothed->pixels == NULL) {
        goto cleanup;
    }

    for (size_t index = 0U; index < pixel_count; ++index) {
        ((uint16_t *)input_u16->pixels)[index] =
            (uint16_t)((uint16_t)((const uint8_t *)gray->pixels)[index] << 8U);
    }
    if (filter_execute(input_u16, filtered_u16, 7,
                       filter_context) != 0) {
        goto cleanup;
    }
    for (size_t index = 0U; index < pixel_count; ++index) {
        ((uint8_t *)filtered_u8->pixels)[index] =
            (uint8_t)(((const uint16_t *)filtered_u16->pixels)[index] >> 8U);
    }

    memset(gradient_x->pixels, 0, (size_t)gradient_x->element_count);
    memset(gradient_y->pixels, 0, (size_t)gradient_y->element_count);
    for (int32_t y = 0; y < gray->height; ++y) {
        const int32_t ym = root_reflect101(y - 1, gray->height);
        const int32_t yp = root_reflect101(y + 1, gray->height);
        for (int32_t x = 1; x < gray->width - 1; ++x) {
            const uint8_t *p = (const uint8_t *)filtered_u8->pixels;
            const int32_t top =
                (int32_t)p[(size_t)ym * (size_t)gray->width + (size_t)x + 1U] -
                (int32_t)p[(size_t)ym * (size_t)gray->width + (size_t)x - 1U];
            const int32_t middle =
                (int32_t)p[(size_t)y * (size_t)gray->width + (size_t)x + 1U] -
                (int32_t)p[(size_t)y * (size_t)gray->width + (size_t)x - 1U];
            const int32_t bottom =
                (int32_t)p[(size_t)yp * (size_t)gray->width + (size_t)x + 1U] -
                (int32_t)p[(size_t)yp * (size_t)gray->width + (size_t)x - 1U];
            ((int16_t *)gradient_x->pixels)[
                (size_t)y * (size_t)gray->width + (size_t)x] =
                (int16_t)(top + middle * 2 + bottom);
        }
    }
    for (int32_t y = 1; y < gray->height - 1; ++y) {
        for (int32_t x = 0; x < gray->width; ++x) {
            const int32_t xm = root_reflect101(x - 1, gray->width);
            const int32_t xp = root_reflect101(x + 1, gray->width);
            const uint8_t *p = (const uint8_t *)filtered_u8->pixels;
            const int32_t left =
                (int32_t)p[(size_t)(y + 1) * (size_t)gray->width + (size_t)xm] -
                (int32_t)p[(size_t)(y - 1) * (size_t)gray->width + (size_t)xm];
            const int32_t middle =
                (int32_t)p[(size_t)(y + 1) * (size_t)gray->width + (size_t)x] -
                (int32_t)p[(size_t)(y - 1) * (size_t)gray->width + (size_t)x];
            const int32_t right =
                (int32_t)p[(size_t)(y + 1) * (size_t)gray->width + (size_t)xp] -
                (int32_t)p[(size_t)(y - 1) * (size_t)gray->width + (size_t)xp];
            ((int16_t *)gradient_y->pixels)[
                (size_t)y * (size_t)gray->width + (size_t)x] =
                (int16_t)(left + middle * 2 + right);
        }
    }

    for (size_t index = 0U; index < pixel_count; ++index) {
        const int32_t gx = (int32_t)((const int16_t *)gradient_x->pixels)[index];
        const int32_t gy = (int32_t)((const int16_t *)gradient_y->pixels)[index];
        const uint32_t value = (uint32_t)(gx < 0 ? -gx : gx) / 2U +
                               (uint32_t)(gy < 0 ? -gy : gy) / 2U;
        ((uint16_t *)magnitude->pixels)[index] = (uint16_t)value;
    }

    for (int32_t y = 0; y < gray->height; ++y) {
        for (int32_t x = 0; x < gray->width; ++x) {
            uint32_t sum = 0U;
            for (int32_t ky = -GX_ROOT_MASK_BOX_RADIUS;
                 ky <= GX_ROOT_MASK_BOX_RADIUS; ++ky) {
                const int32_t sy = root_reflect101(y + ky, gray->height);
                for (int32_t kx = -GX_ROOT_MASK_BOX_RADIUS;
                     kx <= GX_ROOT_MASK_BOX_RADIUS; ++kx) {
                    const int32_t sx = root_reflect101(x + kx, gray->width);
                    sum += ((const uint16_t *)magnitude->pixels)[
                        (size_t)sy * (size_t)gray->width + (size_t)sx];
                }
            }
            ((uint16_t *)smoothed->pixels)[
                (size_t)y * (size_t)gray->width + (size_t)x] =
                (uint16_t)((sum * GX_ROOT_MASK_BOX_RECIPROCAL_Q16) >> 16U);
        }
    }

    for (size_t index = 0U; index < pixel_count; ++index) {
        const int selected = ((const uint16_t *)smoothed->pixels)[index] > threshold;
        if (selected) ++positive;
        if (enabled != 0) {
            ((uint8_t *)mask->pixels)[index] = selected ? fill_value : 0U;
        }
    }
    if (coverage_q16 != NULL) {
        *coverage_q16 = (uint32_t)(((uint64_t)positive << 16U) / pixel_count);
    }
    rc = 0;

cleanup:
    if (root_descriptor_release_local(&input_u16, descriptor_release) != 0 ||
        root_descriptor_release_local(&filtered_u16, descriptor_release) != 0 ||
        root_descriptor_release_local(&filtered_u8, descriptor_release) != 0 ||
        root_descriptor_release_local(&gradient_x, descriptor_release) != 0 ||
        root_descriptor_release_local(&gradient_y, descriptor_release) != 0 ||
        root_descriptor_release_local(&magnitude, descriptor_release) != 0 ||
        root_descriptor_release_local(&smoothed, descriptor_release) != 0) {
        rc = -1;
    }
    return rc;
}

int gx_feature_root_feature_maps_prepare(
    int32_t mode,
    const gx_feature_pyramid_image *primary,
    const gx_feature_pyramid_image *optional,
    const gx_feature_pyramid_image *mask,
    uint8_t *feature_object,
    gx_feature_descriptor_alloc_fn descriptor_alloc,
    gx_feature_descriptor_release_fn descriptor_release) {
    gx_feature_pyramid_image *adaptive_primary = NULL;
    gx_feature_pyramid_image *adaptive_optional = NULL;
    gx_feature_pyramid_image *fixed_primary = NULL;
    gx_feature_pyramid_image *temporary = NULL;
    gx_feature_pyramid_image *work_mask = NULL;
    int rc = -1;

    if (primary == NULL || primary->pixels == NULL || mask == NULL ||
        mask->pixels == NULL || feature_object == NULL ||
        descriptor_alloc == NULL || descriptor_release == NULL ||
        primary->element_bytes != 1 || mask->element_bytes != 1 ||
        primary->width <= 0 || primary->height <= 0 ||
        primary->width != mask->width || primary->height != mask->height) {
        return -1;
    }

    if (mode == 0) {
        if (root_ensure_descriptor_slot(
                feature_object, GX_ROOT_MAP_ADAPTIVE_PRIMARY_OFFSET,
                primary->width, primary->height, 8,
                descriptor_alloc, descriptor_release,
                &adaptive_primary) != 0) goto cleanup;
        {
            const uint8_t threshold = root_quantile_threshold(primary, mask, 0);
            if (root_pack_threshold_map(primary, adaptive_primary,
                                        threshold, 0) != 0) goto cleanup;
        }

        if (optional != NULL) {
            if (optional->pixels == NULL || optional->element_bytes != 1 ||
                optional->width != primary->width ||
                optional->height != primary->height ||
                root_ensure_descriptor_slot(
                    feature_object, GX_ROOT_MAP_ADAPTIVE_OPTIONAL_OFFSET,
                    optional->width, optional->height, 8,
                    descriptor_alloc, descriptor_release,
                    &adaptive_optional) != 0) goto cleanup;
            {
                const uint8_t threshold = mask == NULL ? UINT8_C(200) :
                    root_quantile_threshold(optional, mask, 0);
                if (root_pack_threshold_map(optional, adaptive_optional,
                                            threshold, 0) != 0) goto cleanup;
            }
        }

        if (root_ensure_descriptor_slot(
                feature_object, GX_ROOT_MAP_FIXED_PRIMARY_OFFSET,
                primary->width, primary->height, 8,
                descriptor_alloc, descriptor_release,
                &fixed_primary) != 0 ||
            root_pack_threshold_map(primary, fixed_primary,
                                    UINT8_C(0x38), 1) != 0 ||
            root_copy_descriptor_to_slot(
                mask, feature_object, GX_ROOT_MAP_WORK_MASK_OFFSET,
                descriptor_alloc, descriptor_release) != 0) {
            goto cleanup;
        }
        return 0;
    }

    if (primary->width < 2 || primary->height < 2) goto cleanup;
    temporary = descriptor_alloc(primary->width / 2, primary->height / 2,
                                 primary->element_bytes);
    if (temporary == NULL || temporary->pixels == NULL ||
        root_subsample_even_bytes(primary, temporary) != 0 ||
        root_ensure_descriptor_slot(
            feature_object, GX_ROOT_MAP_WORK_MASK_OFFSET,
            temporary->width, temporary->height, primary->element_bytes,
            descriptor_alloc, descriptor_release, &work_mask) != 0 ||
        root_subsample_even_bytes(mask, work_mask) != 0 ||
        root_ensure_descriptor_slot(
            feature_object, GX_ROOT_MAP_ADAPTIVE_PRIMARY_OFFSET,
            temporary->width, temporary->height, 8,
            descriptor_alloc, descriptor_release, &adaptive_primary) != 0 ||
        root_pack_threshold_map(temporary, adaptive_primary,
                                UINT8_C(0xc9), 1) != 0 ||
        root_ensure_descriptor_slot(
            feature_object, GX_ROOT_MAP_FIXED_PRIMARY_OFFSET,
            temporary->width, temporary->height, 8,
            descriptor_alloc, descriptor_release, &fixed_primary) != 0 ||
        root_pack_threshold_map(temporary, fixed_primary,
                                UINT8_C(0x38), 1) != 0) {
        goto cleanup;
    }

    if (optional == NULL) {
        root_write_descriptor_slot(feature_object,
                                   GX_ROOT_MAP_ADAPTIVE_OPTIONAL_OFFSET, NULL);
    } else {
        uint8_t threshold;
        if (optional->pixels == NULL || optional->element_bytes != 1 ||
            optional->width != primary->width ||
            optional->height != primary->height ||
            root_subsample_even_bytes(optional, temporary) != 0 ||
            root_ensure_descriptor_slot(
                feature_object, GX_ROOT_MAP_ADAPTIVE_OPTIONAL_OFFSET,
                temporary->width, temporary->height, 8,
                descriptor_alloc, descriptor_release,
                &adaptive_optional) != 0) {
            goto cleanup;
        }
        threshold = work_mask == NULL ? UINT8_C(200) :
            root_quantile_threshold(temporary, work_mask, 0);
        if (root_pack_threshold_map(temporary, adaptive_optional,
                                    threshold, 0) != 0) goto cleanup;
    }
    rc = 0;

cleanup:
    if (root_descriptor_release_local(&temporary, descriptor_release) != 0)
        rc = -1;
    return rc;
}

gx_feature_pyramid_image *gx_feature_optional_image_prepare(
    const gx_feature_pyramid_image *source,
    gx_feature_descriptor_alloc_fn descriptor_alloc,
    gx_feature_optional_orientation_fn orientation_kernel,
    gx_feature_optional_reconstruct_fn reconstruct_kernel) {
    gx_feature_pyramid_image *destination;
    uint8_t workspace[GX_FEATURE_OPTIONAL_WORKSPACE_BYTES];

    if (source == NULL || source->pixels == NULL || source->width <= 0 ||
        source->height <= 0 || source->element_bytes <= 0 ||
        descriptor_alloc == NULL || orientation_kernel == NULL ||
        reconstruct_kernel == NULL) {
        return NULL;
    }

    destination = descriptor_alloc(source->width, source->height,
                                   source->element_bytes);
    if (destination == NULL || destination->pixels == NULL) {
        return NULL;
    }

    orientation_kernel(source->pixels, workspace, source->width,
                       source->height, GX_FEATURE_OPTIONAL_RADIUS);
    reconstruct_kernel(workspace, source->pixels, destination->pixels,
                       source->width, source->height);
    return destination;
}

static int selftest_root_identity_filter(
    const gx_feature_pyramid_image *source,
    gx_feature_pyramid_image *destination,
    int32_t scale_code, void *context) {
    (void)context;
    if (source == NULL || destination == NULL || source->pixels == NULL ||
        destination->pixels == NULL || scale_code != 7 ||
        source->width != destination->width ||
        source->height != destination->height ||
        source->element_bytes != 2 || destination->element_bytes != 2 ||
        source->element_count != destination->element_count ||
        source->element_count <= 0) {
        return -1;
    }
    memcpy(destination->pixels, source->pixels,
           (size_t)source->element_count);
    return 0;
}

static int selftest_root_filter(
    const gx_feature_pyramid_image *source,
    gx_feature_pyramid_image *destination,
    int32_t scale_code, void *context) {
    gx_feature_filter_kernel kernel;
    gx_feature_filter_stats stats;
    (void)context;
    memset(&kernel, 0, sizeof(kernel));
    memset(&stats, 0, sizeof(stats));
    if (scale_code != 7 ||
        gx_feature_filter_auxiliary_kernel(scale_code, &kernel) != 0 ||
        gx_feature_filter_u16_q16_reflect101(
            source, destination, kernel.coefficients,
            kernel.coefficient_count, &stats) != 0 ||
        stats.kernel_length != kernel.coefficient_count ||
        stats.kernel_sum != kernel.coefficient_sum) {
        return -1;
    }
    return 0;
}

static uint32_t selftest_orientation_calls;
static uint32_t selftest_reconstruct_calls;

static void selftest_orientation(const uint8_t *source, uint8_t *workspace,
                                 int32_t width, int32_t height,
                                 uint32_t radius) {
    size_t count = (size_t)width * (size_t)height;
    size_t index;
    ++selftest_orientation_calls;
    memset(workspace, 0, GX_FEATURE_OPTIONAL_WORKSPACE_BYTES);
    for (index = 0U; index < count; ++index) {
        workspace[index] = (uint8_t)(source[index] ^ (uint8_t)radius);
    }
}

static void selftest_reconstruct(const uint8_t *workspace,
                                 const uint8_t *source,
                                 uint8_t *destination,
                                 int32_t width, int32_t height) {
    size_t count = (size_t)width * (size_t)height;
    size_t index;
    ++selftest_reconstruct_calls;
    for (index = 0U; index < count; ++index) {
        destination[index] = (uint8_t)(workspace[index] + source[index]);
    }
}

int gx_feature_profile24_root_selftest(void) {
    static const uint32_t template_a[4] = {
        UINT32_C(0x11111111), UINT32_C(0x22222222),
        UINT32_C(0x33333333), UINT32_C(0x44444444)
    };
    static const uint32_t template_b[4] = {
        UINT32_C(0x55555555), UINT32_C(0x66666666),
        UINT32_C(0x77777777), UINT32_C(0x88888888)
    };
    uint32_t output[GX_FEATURE_PROFILE24_PARAMETER_WORDS];

    if (gx_feature_profile24_parameters_build(
            output, template_a, template_b, 120U, 92U, 100U) != 0) {
        return -1;
    }
    if (output[0] != GX_FEATURE_PROFILE24_ID || output[1] != 120U ||
        output[2] != GX_FEATURE_PROFILE24_MAX_RECORDS ||
        output[3] != GX_FEATURE_PROFILE24_EDGE_THRESHOLD ||
        output[4] != 1U ||
        output[5] != GX_FEATURE_PROFILE24_SCALE_Q16 ||
        output[6] != template_a[1] || output[7] != template_a[2] ||
        output[8] != template_a[3] || output[9] != template_b[0] ||
        output[10] != template_b[1] || output[11] != template_b[2] ||
        output[12] != template_b[3] || output[13] != 92U ||
        output[14] != 100U) {
        return -1;
    }
    if (gx_feature_profile24_parameters_build(
            output, template_a, template_b, 0U, 92U, 100U) == 0 ||
        gx_feature_profile24_parameters_build(
            output, template_a, template_b, 301U, 92U, 100U) == 0 ||
        gx_feature_profile24_parameters_build(
            NULL, template_a, template_b, 120U, 92U, 100U) == 0) {
        return -1;
    }

    {
        uint8_t constant_source[17U * 13U];
        uint8_t orientation[GX_FEATURE_OPTIONAL_WORKSPACE_BYTES];
        uint8_t reconstructed[17U * 13U];
        size_t index;
        memset(constant_source, 77, sizeof(constant_source));
        memset(orientation, 0xa5, sizeof(orientation));
        memset(reconstructed, 0, sizeof(reconstructed));
        if (gx_feature_optional_orientation_kernel(
                constant_source, orientation, 17, 13,
                GX_FEATURE_OPTIONAL_RADIUS) != 0 ||
            gx_feature_optional_reconstruct_kernel(
                orientation, constant_source, reconstructed, 17, 13) != 0) {
            return -1;
        }
        for (index = 0U; index < sizeof(constant_source); ++index) {
            if (orientation[index] != UINT8_C(45) ||
                reconstructed[index] != UINT8_C(77)) {
                return -1;
            }
        }
        for (index = sizeof(constant_source);
             index < sizeof(orientation); ++index) {
            if (orientation[index] != UINT8_C(0xa5)) {
                return -1;
            }
        }
    }

    {
        gx_feature_pyramid_image *source =
            gx_feature_descriptor_allocate(7, 5, 1);
        gx_feature_pyramid_image *result;
        size_t index;
        if (source == NULL || source->pixels == NULL) {
            return -1;
        }
        {
            uint8_t *source_pixels = (uint8_t *)source->pixels;
            for (index = 0U; index < 35U; ++index) {
                source_pixels[index] = (uint8_t)(index * 7U + 3U);
            }
        }
        selftest_orientation_calls = 0U;
        selftest_reconstruct_calls = 0U;
        result = gx_feature_optional_image_prepare(
            source, gx_feature_descriptor_allocate,
            selftest_orientation, selftest_reconstruct);
        if (result == NULL || result->width != 7 || result->height != 5 ||
            result->element_bytes != 1 || selftest_orientation_calls != 1U ||
            selftest_reconstruct_calls != 1U) {
            if (result != NULL) {
                (void)gx_feature_descriptor_release(&result);
            }
            (void)gx_feature_descriptor_release(&source);
            return -1;
        }
        for (index = 0U; index < 35U; ++index) {
            const uint8_t *source_pixels = (const uint8_t *)source->pixels;
            const uint8_t *result_pixels = (const uint8_t *)result->pixels;
            uint8_t expected = (uint8_t)(
                (uint8_t)(source_pixels[index] ^
                          (uint8_t)GX_FEATURE_OPTIONAL_RADIUS) +
                source_pixels[index]);
            if (result_pixels[index] != expected) {
                (void)gx_feature_descriptor_release(&result);
                (void)gx_feature_descriptor_release(&source);
                return -1;
            }
        }
        if (gx_feature_descriptor_release(&result) != 0U || result != NULL ||
            gx_feature_descriptor_release(&source) != 0U || source != NULL) {
            return -1;
        }
    }

    {
        gx_feature_pyramid_image *gray =
            gx_feature_descriptor_allocate(17, 13, 1);
        gx_feature_pyramid_image *mask =
            gx_feature_descriptor_allocate(17, 13, 1);
        uint32_t coverage = UINT32_MAX;
        size_t index;
        if (gray == NULL || mask == NULL || gray->pixels == NULL ||
            mask->pixels == NULL) {
            if (gray != NULL) (void)gx_feature_descriptor_release(&gray);
            if (mask != NULL) (void)gx_feature_descriptor_release(&mask);
            return -1;
        }
        memset(gray->pixels, 77, 17U * 13U);
        memset(mask->pixels, 0x5a, 17U * 13U);
        if (gx_feature_root_mask_prepare(
                gray, mask, 1, UINT16_C(110), UINT8_C(1),
                gx_feature_descriptor_allocate,
                gx_feature_descriptor_release,
                selftest_root_filter, NULL, &coverage) != 0 ||
            coverage != 0U) {
            (void)gx_feature_descriptor_release(&gray);
            (void)gx_feature_descriptor_release(&mask);
            return -1;
        }
        for (index = 0U; index < 17U * 13U; ++index) {
            if (((uint8_t *)mask->pixels)[index] != 0U) {
                (void)gx_feature_descriptor_release(&gray);
                (void)gx_feature_descriptor_release(&mask);
                return -1;
            }
        }
        memset(mask->pixels, 0x5a, 17U * 13U);
        if (gx_feature_root_mask_prepare(
                gray, mask, 0, UINT16_C(110), UINT8_C(1),
                gx_feature_descriptor_allocate,
                gx_feature_descriptor_release,
                selftest_root_filter, NULL, &coverage) != 0 ||
            coverage != 0U) {
            (void)gx_feature_descriptor_release(&gray);
            (void)gx_feature_descriptor_release(&mask);
            return -1;
        }
        for (index = 0U; index < 17U * 13U; ++index) {
            if (((uint8_t *)mask->pixels)[index] != UINT8_C(0x5a)) {
                (void)gx_feature_descriptor_release(&gray);
                (void)gx_feature_descriptor_release(&mask);
                return -1;
            }
        }
        if (gx_feature_descriptor_release(&gray) != 0U || gray != NULL ||
            gx_feature_descriptor_release(&mask) != 0U || mask != NULL) {
            return -1;
        }
    }

    {
        gx_feature_pyramid_image *gray =
            gx_feature_descriptor_allocate(17, 13, 1);
        gx_feature_pyramid_image *mask =
            gx_feature_descriptor_allocate(17, 13, 1);
        uint32_t coverage = UINT32_MAX;
        uint32_t hash = UINT32_C(2166136261);
        size_t index;
        if (gray == NULL || mask == NULL || gray->pixels == NULL ||
            mask->pixels == NULL) {
            if (gray != NULL) (void)gx_feature_descriptor_release(&gray);
            if (mask != NULL) (void)gx_feature_descriptor_release(&mask);
            return -1;
        }
        for (index = 0U; index < 17U * 13U; ++index) {
            const uint32_t x = (uint32_t)(index % 17U);
            const uint32_t y = (uint32_t)(index / 17U);
            ((uint8_t *)gray->pixels)[index] = (uint8_t)(
                UINT32_C(3) + x * UINT32_C(5) + y * UINT32_C(11) +
                x * y * UINT32_C(3));
        }
        memset(mask->pixels, 0, 17U * 13U);
        if (gx_feature_root_mask_prepare(
                gray, mask, 1, UINT16_C(180), UINT8_C(1),
                gx_feature_descriptor_allocate,
                gx_feature_descriptor_release,
                selftest_root_identity_filter, NULL, &coverage) != 0 ||
            coverage != UINT32_C(53970)) {
            (void)gx_feature_descriptor_release(&gray);
            (void)gx_feature_descriptor_release(&mask);
            return -1;
        }
        for (index = 0U; index < 17U * 13U; ++index) {
            hash ^= ((const uint8_t *)mask->pixels)[index];
            hash *= UINT32_C(16777619);
        }
        if (hash != UINT32_C(0xecabb5c9)) {
            (void)gx_feature_descriptor_release(&gray);
            (void)gx_feature_descriptor_release(&mask);
            return -1;
        }
        if (gx_feature_descriptor_release(&gray) != 0U || gray != NULL ||
            gx_feature_descriptor_release(&mask) != 0U || mask != NULL) {
            return -1;
        }
    }

    {
        uint8_t object[0x28U];
        gx_feature_pyramid_image *primary =
            gx_feature_descriptor_allocate(8, 4, 1);
        gx_feature_pyramid_image *optional =
            gx_feature_descriptor_allocate(8, 4, 1);
        gx_feature_pyramid_image *mask =
            gx_feature_descriptor_allocate(8, 4, 1);
        gx_feature_pyramid_image *slot8;
        gx_feature_pyramid_image *slot10;
        gx_feature_pyramid_image *slot18;
        gx_feature_pyramid_image *slot20;
        size_t index;
        memset(object, 0, sizeof(object));
        if (primary == NULL || optional == NULL || mask == NULL ||
            primary->pixels == NULL || optional->pixels == NULL ||
            mask->pixels == NULL) {
            if (primary != NULL) (void)gx_feature_descriptor_release(&primary);
            if (optional != NULL) (void)gx_feature_descriptor_release(&optional);
            if (mask != NULL) (void)gx_feature_descriptor_release(&mask);
            return -1;
        }
        for (index = 0U; index < 32U; ++index) {
            ((uint8_t *)primary->pixels)[index] = (uint8_t)(index * 9U);
            ((uint8_t *)optional->pixels)[index] =
                (uint8_t)(255U - index * 5U);
            ((uint8_t *)mask->pixels)[index] = (uint8_t)((index & 3U) != 0U);
        }
        if (gx_feature_root_feature_maps_prepare(
                0, primary, optional, mask, object,
                gx_feature_descriptor_allocate,
                gx_feature_descriptor_release) != 0) {
            (void)gx_feature_descriptor_release(&primary);
            (void)gx_feature_descriptor_release(&optional);
            (void)gx_feature_descriptor_release(&mask);
            return -1;
        }
        slot8 = root_read_descriptor_slot(
            object, GX_ROOT_MAP_ADAPTIVE_PRIMARY_OFFSET);
        slot10 = root_read_descriptor_slot(
            object, GX_ROOT_MAP_ADAPTIVE_OPTIONAL_OFFSET);
        slot18 = root_read_descriptor_slot(
            object, GX_ROOT_MAP_FIXED_PRIMARY_OFFSET);
        slot20 = root_read_descriptor_slot(
            object, GX_ROOT_MAP_WORK_MASK_OFFSET);
        if (slot8 == NULL || slot10 == NULL || slot18 == NULL ||
            slot20 == NULL || slot8->element_bytes != 8 ||
            slot10->element_bytes != 8 || slot18->element_bytes != 8 ||
            slot20->element_bytes != 1 || slot8->element_count != 4 ||
            slot10->element_count != 4 || slot18->element_count != 4 ||
            memcmp(slot20->pixels, mask->pixels, 32U) != 0) {
            return -1;
        }
        if (gx_feature_descriptor_release(&slot8) != 0U ||
            gx_feature_descriptor_release(&slot10) != 0U ||
            gx_feature_descriptor_release(&slot18) != 0U ||
            gx_feature_descriptor_release(&slot20) != 0U ||
            gx_feature_descriptor_release(&primary) != 0U ||
            gx_feature_descriptor_release(&optional) != 0U ||
            gx_feature_descriptor_release(&mask) != 0U) {
            return -1;
        }
    }

    if (gx_feature_optional_image_prepare(NULL,
            gx_feature_descriptor_allocate,
            selftest_orientation, selftest_reconstruct) != NULL) {
        return -1;
    }
    return 0;
}
