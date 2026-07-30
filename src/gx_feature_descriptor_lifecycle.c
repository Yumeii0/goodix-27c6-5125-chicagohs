#include "gx5125/feature_descriptor_lifecycle.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

_Static_assert(sizeof(gx_feature_pyramid_image) ==
                   GX_FEATURE_DESCRIPTOR_HEADER_BYTES,
               "descriptor ABI size mismatch");
_Static_assert(offsetof(gx_feature_pyramid_image, pixels) == 0x18U,
               "descriptor pixel pointer offset mismatch");

static int compute_layout(int32_t width, int32_t height,
                          int32_t element_type,
                          int32_t *reserved_08,
                          int32_t *byte_length) {
    int64_t pixels;
    int64_t stride;
    int64_t bytes;

    if (reserved_08 == NULL || byte_length == NULL ||
        width <= 0 || height <= 0 || element_type <= 0) {
        return -1;
    }

    pixels = (int64_t)width * (int64_t)height;
    if (pixels <= 0 || pixels > INT32_MAX) {
        return -1;
    }

    if (element_type == 8) {
        bytes = (pixels + 7) / 8;
        stride = -1;
    } else {
        stride = (int64_t)width * (int64_t)element_type;
        bytes = pixels * (int64_t)element_type;
        if (stride <= 0 || stride > INT32_MAX) {
            return -1;
        }
    }

    if (bytes <= 0 || bytes > INT32_MAX ||
        (uint64_t)bytes > (uint64_t)SIZE_MAX -
                              GX_FEATURE_DESCRIPTOR_HEADER_BYTES) {
        return -1;
    }

    *reserved_08 = (int32_t)stride;
    *byte_length = (int32_t)bytes;
    return 0;
}

gx_feature_pyramid_image *gx_feature_descriptor_allocate(
    int32_t width, int32_t height, int32_t element_type) {
    gx_feature_pyramid_image *descriptor;
    int32_t reserved_08;
    int32_t byte_length;
    size_t allocation_bytes;

    if (compute_layout(width, height, element_type,
                       &reserved_08, &byte_length) != 0) {
        return NULL;
    }

    allocation_bytes = GX_FEATURE_DESCRIPTOR_HEADER_BYTES +
                       (size_t)byte_length;
    descriptor = (gx_feature_pyramid_image *)malloc(allocation_bytes);
    if (descriptor == NULL) {
        return NULL;
    }

    descriptor->width = width;
    descriptor->height = height;
    descriptor->reserved_08 = reserved_08;
    descriptor->element_count = byte_length;
    descriptor->element_bytes = element_type;
    /* reserved_14 is intentionally left untouched. FUN_180042c60 writes the
     * same six fields and does not initialize this word. */
    descriptor->pixels = (uint8_t *)(void *)descriptor +
                         GX_FEATURE_DESCRIPTOR_HEADER_BYTES;
    return descriptor;
}

uint32_t gx_feature_descriptor_release(
    gx_feature_pyramid_image **descriptor) {
    if (descriptor == NULL || *descriptor == NULL) {
        return GX_FEATURE_DESCRIPTOR_RELEASE_INVALID;
    }
    free(*descriptor);
    *descriptor = NULL;
    return 0U;
}

static int validate_descriptor(const gx_feature_pyramid_image *descriptor,
                               int32_t width, int32_t height,
                               int32_t reserved_08, int32_t byte_length,
                               int32_t element_type) {
    const uint8_t *base = (const uint8_t *)(const void *)descriptor;
    const uint8_t *pixels;

    if (descriptor == NULL) {
        return -1;
    }
    pixels = (const uint8_t *)descriptor->pixels;
    if (descriptor->width != width || descriptor->height != height ||
        descriptor->reserved_08 != reserved_08 ||
        descriptor->element_count != byte_length ||
        descriptor->element_bytes != element_type ||
        pixels != base + GX_FEATURE_DESCRIPTOR_HEADER_BYTES) {
        return -1;
    }
    return 0;
}

int gx_feature_descriptor_lifecycle_selftest(void) {
    static const int32_t types[] = {1, 2, 4};
    size_t index;

    for (index = 0U; index < sizeof(types) / sizeof(types[0]); ++index) {
        gx_feature_pyramid_image *descriptor;
        const int32_t element_type = types[index];
        descriptor = gx_feature_descriptor_allocate(7, 5, element_type);
        if (validate_descriptor(descriptor, 7, 5,
                                7 * element_type,
                                35 * element_type,
                                element_type) != 0) {
            if (descriptor != NULL) {
                (void)gx_feature_descriptor_release(&descriptor);
            }
            return -1;
        }
        if (gx_feature_descriptor_release(&descriptor) != 0U ||
            descriptor != NULL) {
            return -1;
        }
    }

    {
        gx_feature_pyramid_image *descriptor =
            gx_feature_descriptor_allocate(9, 3, 8);
        if (validate_descriptor(descriptor, 9, 3, -1, 4, 8) != 0) {
            if (descriptor != NULL) {
                (void)gx_feature_descriptor_release(&descriptor);
            }
            return -1;
        }
        if (gx_feature_descriptor_release(&descriptor) != 0U ||
            descriptor != NULL) {
            return -1;
        }
    }

    {
        gx_feature_pyramid_image *descriptor = NULL;
        if (gx_feature_descriptor_release(NULL) !=
                GX_FEATURE_DESCRIPTOR_RELEASE_INVALID ||
            gx_feature_descriptor_release(&descriptor) !=
                GX_FEATURE_DESCRIPTOR_RELEASE_INVALID) {
            return -1;
        }
    }

    {
        gx_feature_pyramid_image *invalid[4];
        invalid[0] = gx_feature_descriptor_allocate(0, 5, 1);
        invalid[1] = gx_feature_descriptor_allocate(5, 0, 1);
        invalid[2] = gx_feature_descriptor_allocate(5, 5, 0);
        invalid[3] = gx_feature_descriptor_allocate(
            INT32_MAX, INT32_MAX, 4);
        for (index = 0U; index < sizeof(invalid) / sizeof(invalid[0]);
             ++index) {
            if (invalid[index] != NULL) {
                (void)gx_feature_descriptor_release(&invalid[index]);
                return -1;
            }
        }
    }

    return 0;
}
