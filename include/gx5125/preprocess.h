#ifndef GX5125_PREPROCESS_H
#define GX5125_PREPROCESS_H

#include <stddef.h>
#include <stdint.h>


/*
 * AlgoChicago/EngineAdapter ABI geometry for sensorType 0x0c.
 * The capture layer historically calls the image 64x80, while the algorithm
 * descriptor stores the same 5120 samples as 80 columns x 64 rows.
 */
#define GX_PREPROC_COLUMNS 80u
#define GX_PREPROC_ROWS 64u
#define GX_PREPROC_PIXELS (GX_PREPROC_COLUMNS * GX_PREPROC_ROWS)
#define GX_PREPROC_VERSION "0.2.0"

#define GX_PREPROC_OK 0
#define GX_PREPROC_ERR_ARGUMENT (-1)
#define GX_PREPROC_ERR_MEMORY (-2)
#define GX_PREPROC_ERR_BASE_INVALID (-3)
#define GX_PREPROC_ERR_NOT_INITIALIZED (-4)
#define GX_PREPROC_ERR_IMAGE_INVALID (-5)

typedef enum gx_preproc_signal_mode {
    GX_PREPROC_SIGNAL_DIFFERENCE = 0,
    GX_PREPROC_SIGNAL_RATIO = 1
} gx_preproc_signal_mode;

typedef struct gx_preproc_params {
    gx_preproc_signal_mode signal_mode;
    unsigned int pre_blur_radius;
    unsigned int background_radius;
    double lower_clip_fraction;
    double upper_clip_fraction;
    int invert_output;
} gx_preproc_params;

typedef struct gx_preproc_metrics {
    uint8_t quality;
    uint8_t coverage;
    uint16_t foreground_threshold;
    uint32_t foreground_pixels;
    double foreground_fraction;
    double ridge_coherence;
    double gray_mean;
    double gray_standard_deviation;
    uint32_t gray_zero_count;
    uint32_t gray_ff_count;
} gx_preproc_metrics;

typedef struct gx_preproc gx_preproc;

void gx_preproc_default_params(gx_preproc_params *params);

gx_preproc *gx_preproc_create(const gx_preproc_params *params);
void gx_preproc_destroy(gx_preproc *ctx);

int gx_preproc_initialize(gx_preproc *ctx,
                          const uint16_t base[GX_PREPROC_PIXELS]);

int gx_preproc_process(gx_preproc *ctx,
                       const uint16_t current[GX_PREPROC_PIXELS],
                       uint8_t gray[GX_PREPROC_PIXELS],
                       gx_preproc_metrics *metrics);

int gx_preproc_is_initialized(const gx_preproc *ctx);

#endif
