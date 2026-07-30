#include "gx5125/feature_object.h"
#include "gx5125/feature_descriptor_lifecycle.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static void *read_pointer(const uint8_t *object, size_t offset) {
    void *pointer = NULL;
    memcpy(&pointer, object + offset, sizeof(pointer));
    return pointer;
}

static void write_pointer(uint8_t *object, size_t offset, void *pointer) {
    memcpy(object + offset, &pointer, sizeof(pointer));
}

static int callbacks_valid(const gx_feature_object_callbacks *callbacks) {
    return callbacks != NULL && callbacks->allocate != NULL &&
           callbacks->deallocate != NULL &&
           callbacks->release_descriptor != NULL;
}

int gx_feature_object_create(void **feature_handle, uint32_t max_records,
                             const gx_feature_object_callbacks *callbacks) {
    uint8_t *object;
    void *records;
    size_t record_bytes;

    if (feature_handle == NULL || *feature_handle != NULL ||
        !callbacks_valid(callbacks) || max_records == 0U ||
        max_records > (uint32_t)(SIZE_MAX / GX_FEATURE_OBJECT_RECORD_BYTES)) {
        return -1;
    }
    record_bytes = (size_t)max_records * GX_FEATURE_OBJECT_RECORD_BYTES;
    object = (uint8_t *)callbacks->allocate(GX_FEATURE_OBJECT_BYTES);
    if (object == NULL) return -1;
    memset(object, 0, GX_FEATURE_OBJECT_BYTES);
    records = callbacks->allocate(record_bytes);
    if (records == NULL) {
        callbacks->deallocate(object);
        return -1;
    }
    write_pointer(object, GX_FEATURE_OBJECT_RECORD_ARRAY_OFFSET, records);
    *feature_handle = object;
    return 0;
}

int gx_feature_object_aux_map_ensure(void *feature_object, size_t pixel_count,
                                     const gx_feature_object_callbacks *callbacks) {
    uint8_t *object = (uint8_t *)feature_object;
    void *map;
    if (object == NULL || pixel_count == 0U || !callbacks_valid(callbacks)) {
        return -1;
    }
    map = read_pointer(object, GX_FEATURE_OBJECT_AUX_MAP_OFFSET);
    if (map == NULL) {
        map = callbacks->allocate(pixel_count);
        if (map == NULL) return -1;
        write_pointer(object, GX_FEATURE_OBJECT_AUX_MAP_OFFSET, map);
    }
    memset(map, 0, pixel_count);
    return 0;
}

int gx_feature_object_output_maps_ensure(
    void *feature_object, size_t pixel_count,
    const gx_feature_object_callbacks *callbacks) {
    static const size_t offsets[3] = {
        GX_FEATURE_OBJECT_OUTPUT_MAP0_OFFSET,
        GX_FEATURE_OBJECT_OUTPUT_MAP1_OFFSET,
        GX_FEATURE_OBJECT_OUTPUT_MAP2_OFFSET
    };
    uint8_t *object = (uint8_t *)feature_object;
    size_t index;
    if (object == NULL || pixel_count == 0U || !callbacks_valid(callbacks)) {
        return -1;
    }
    for (index = 0U; index < 3U; ++index) {
        void *map = read_pointer(object, offsets[index]);
        if (map == NULL) {
            map = callbacks->allocate(pixel_count);
            if (map == NULL) return -1;
            write_pointer(object, offsets[index], map);
        }
        memset(map, 0, pixel_count);
    }
    return 0;
}

uint32_t gx_feature_object_release(
    void **feature_handle, const gx_feature_object_callbacks *callbacks,
    gx_feature_object_release_stats *stats) {
    static const size_t descriptor_offsets[GX_FEATURE_OBJECT_DESCRIPTOR_SLOT_COUNT] = {
        0x008U, 0x010U, 0x018U, 0x020U,
        GX_FEATURE_OBJECT_PACKED_MAP_OFFSET
    };
    static const size_t heap_offsets[GX_FEATURE_OBJECT_HEAP_FIELD_COUNT] = {
        GX_FEATURE_OBJECT_RECORD_ARRAY_OFFSET,
        GX_FEATURE_OBJECT_AUX_MAP_OFFSET,
        GX_FEATURE_OBJECT_OUTPUT_MAP0_OFFSET,
        GX_FEATURE_OBJECT_OUTPUT_MAP1_OFFSET,
        GX_FEATURE_OBJECT_OUTPUT_MAP2_OFFSET
    };
    uint8_t *object;
    size_t index;
    gx_feature_object_release_stats local_stats;

    memset(&local_stats, 0, sizeof(local_stats));
    if (stats != NULL) memset(stats, 0, sizeof(*stats));
    if (feature_handle == NULL || *feature_handle == NULL ||
        !callbacks_valid(callbacks)) {
        return UINT32_C(0x80000002);
    }
    object = (uint8_t *)*feature_handle;
    for (index = 0U; index < GX_FEATURE_OBJECT_DESCRIPTOR_SLOT_COUNT; ++index) {
        gx_feature_pyramid_image *descriptor =
            (gx_feature_pyramid_image *)read_pointer(object,
                                                     descriptor_offsets[index]);
        if (descriptor != NULL) {
            uint32_t rc = callbacks->release_descriptor(&descriptor);
            if (rc != 0U || descriptor != NULL) {
                if (stats != NULL) *stats = local_stats;
                return rc != 0U ? rc : UINT32_C(0x80000002);
            }
            ++local_stats.descriptor_slots_released;
        }
        write_pointer(object, descriptor_offsets[index], NULL);
    }
    for (index = 0U; index < GX_FEATURE_OBJECT_HEAP_FIELD_COUNT; ++index) {
        void *pointer = read_pointer(object, heap_offsets[index]);
        if (pointer != NULL) {
            callbacks->deallocate(pointer);
            ++local_stats.heap_fields_released;
        }
        write_pointer(object, heap_offsets[index], NULL);
    }
    callbacks->deallocate(object);
    ++local_stats.object_released;
    *feature_handle = NULL;
    if (stats != NULL) *stats = local_stats;
    return 0U;
}

static uint32_t selftest_allocations;
static uint32_t selftest_frees;
static uint32_t selftest_descriptor_releases;

static void *selftest_allocate(size_t bytes) {
    void *pointer = malloc(bytes);
    if (pointer != NULL) ++selftest_allocations;
    return pointer;
}

static void selftest_free(void *pointer) {
    if (pointer != NULL) {
        ++selftest_frees;
        free(pointer);
    }
}

static uint32_t selftest_release_descriptor(
    gx_feature_pyramid_image **descriptor) {
    uint32_t rc;
    if (descriptor == NULL || *descriptor == NULL) return UINT32_C(0x80000002);
    rc = gx_feature_descriptor_release(descriptor);
    if (rc == 0U) ++selftest_descriptor_releases;
    return rc;
}

int gx_feature_object_selftest(void) {
    gx_feature_object_callbacks callbacks;
    gx_feature_object_release_stats stats;
    uint8_t *object = NULL;
    static const size_t descriptor_offsets[GX_FEATURE_OBJECT_DESCRIPTOR_SLOT_COUNT] = {
        0x008U, 0x010U, 0x018U, 0x020U,
        GX_FEATURE_OBJECT_PACKED_MAP_OFFSET
    };
    size_t index;
    int result = -1;

    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.allocate = selftest_allocate;
    callbacks.deallocate = selftest_free;
    callbacks.release_descriptor = selftest_release_descriptor;
    selftest_allocations = 0U;
    selftest_frees = 0U;
    selftest_descriptor_releases = 0U;

    if (gx_feature_object_create((void **)(void *)&object, 120U,
                                 &callbacks) != 0 || object == NULL ||
        read_pointer(object, GX_FEATURE_OBJECT_RECORD_ARRAY_OFFSET) == NULL ||
        gx_feature_object_aux_map_ensure(object, 80U * 64U, &callbacks) != 0 ||
        gx_feature_object_output_maps_ensure(object, 80U * 64U,
                                             &callbacks) != 0) {
        goto cleanup;
    }
    for (index = 0U; index < GX_FEATURE_OBJECT_DESCRIPTOR_SLOT_COUNT; ++index) {
        gx_feature_pyramid_image *descriptor =
            gx_feature_descriptor_allocate(7, 5, index == 4U ? 8 : 1);
        if (descriptor == NULL) goto cleanup;
        write_pointer(object, descriptor_offsets[index], descriptor);
    }
    memset(&stats, 0, sizeof(stats));
    if (gx_feature_object_release((void **)(void *)&object, &callbacks,
                                  &stats) != 0U || object != NULL ||
        stats.descriptor_slots_released !=
            GX_FEATURE_OBJECT_DESCRIPTOR_SLOT_COUNT ||
        stats.heap_fields_released != GX_FEATURE_OBJECT_HEAP_FIELD_COUNT ||
        stats.object_released != 1U ||
        selftest_descriptor_releases !=
            GX_FEATURE_OBJECT_DESCRIPTOR_SLOT_COUNT ||
        selftest_allocations != GX_FEATURE_OBJECT_TOTAL_HEAP_ALLOCATIONS ||
        selftest_frees != GX_FEATURE_OBJECT_TOTAL_HEAP_ALLOCATIONS ||
        gx_feature_object_release((void **)(void *)&object, &callbacks,
                                  &stats) != UINT32_C(0x80000002)) {
        goto cleanup;
    }
    result = 0;

cleanup:
    if (object != NULL) {
        (void)gx_feature_object_release((void **)(void *)&object,
                                        &callbacks, NULL);
    }
    return result;
}
