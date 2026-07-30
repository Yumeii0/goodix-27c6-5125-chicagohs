#ifndef GX5125_FEATURE_OBJECT_H
#define GX5125_FEATURE_OBJECT_H

#include <stddef.h>
#include <stdint.h>

#include "gx5125/feature_pyramid.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GX_FEATURE_OBJECT_BYTES 0x230U
#define GX_FEATURE_OBJECT_RECORD_BYTES 0x3cU
#define GX_FEATURE_OBJECT_DESCRIPTOR_SLOT_COUNT 5U
#define GX_FEATURE_OBJECT_HEAP_FIELD_COUNT 5U
#define GX_FEATURE_OBJECT_TOTAL_HEAP_ALLOCATIONS 6U

#define GX_FEATURE_OBJECT_RECORD_COUNT_OFFSET 0x0f0U
#define GX_FEATURE_OBJECT_RECORD_ARRAY_OFFSET 0x0f8U
#define GX_FEATURE_OBJECT_PACKED_MAP_OFFSET 0x130U
#define GX_FEATURE_OBJECT_AUX_MAP_OFFSET 0x148U
#define GX_FEATURE_OBJECT_OUTPUT_MAP0_OFFSET 0x218U
#define GX_FEATURE_OBJECT_OUTPUT_MAP1_OFFSET 0x220U
#define GX_FEATURE_OBJECT_OUTPUT_MAP2_OFFSET 0x228U

typedef void *(*gx_feature_object_allocate_fn)(size_t bytes);
typedef void (*gx_feature_object_free_fn)(void *pointer);
typedef uint32_t (*gx_feature_object_descriptor_release_fn)(
    gx_feature_pyramid_image **descriptor);

typedef struct gx_feature_object_callbacks {
    gx_feature_object_allocate_fn allocate;
    gx_feature_object_free_fn deallocate;
    gx_feature_object_descriptor_release_fn release_descriptor;
} gx_feature_object_callbacks;

typedef struct gx_feature_object_release_stats {
    uint32_t descriptor_slots_released;
    uint32_t heap_fields_released;
    uint32_t object_released;
} gx_feature_object_release_stats;

int gx_feature_object_create(void **feature_handle, uint32_t max_records,
                             const gx_feature_object_callbacks *callbacks);
int gx_feature_object_aux_map_ensure(void *feature_object, size_t pixel_count,
                                     const gx_feature_object_callbacks *callbacks);
int gx_feature_object_output_maps_ensure(
    void *feature_object, size_t pixel_count,
    const gx_feature_object_callbacks *callbacks);
uint32_t gx_feature_object_release(
    void **feature_handle, const gx_feature_object_callbacks *callbacks,
    gx_feature_object_release_stats *stats);
int gx_feature_object_selftest(void);

#ifdef __cplusplus
}
#endif

#endif
