#include "gx5125/feature_aux_metadata.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define GX_AUX_COMPONENT_QUEUE_CAPACITY 2450U
#define GX_AUX_COMPONENT_MAX 70
#define GX_AUX_COMPONENT_HISTORY_MAX 30
#define GX_AUX_COMPONENT_SELECT 3U

static const uint8_t gx_aux_fill_lut[256] = {
    0, 0, 1, 1, 0, 0, 1, 1, 1, 1, 0, 1, 1, 1, 0, 1,
    1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 1,
    0, 0, 1, 1, 0, 0, 1, 1, 1, 1, 0, 1, 1, 1, 0, 1,
    1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 1,
    1, 1, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 1, 1, 1, 0, 1,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 1, 1, 0, 0, 1, 1, 1, 1, 0, 1, 1, 1, 0, 1,
    1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 1,
    0, 0, 1, 1, 0, 0, 1, 1, 1, 1, 0, 1, 1, 1, 0, 1,
    1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0,
    1, 1, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0,
    1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 1, 1, 1, 0, 0,
    1, 1, 0, 0, 1, 1, 1, 0, 1, 1, 0, 0, 1, 0, 0, 0,
};

typedef struct gx_aux_point {
    int32_t x;
    int32_t y;
} gx_aux_point;

static int gx_aux_safe_pixel_count(int32_t width, int32_t height,
                                   size_t *count) {
    if (count == NULL || width <= 0 || height <= 0 ||
        (size_t)width > SIZE_MAX / (size_t)height) {
        return -1;
    }
    *count = (size_t)width * (size_t)height;
    return *count > GX_FEATURE_AUX_HISTORY_BYTES ? -1 : 0;
}

void gx_feature_aux_metadata_state_reset(gx_feature_aux_metadata_state *state) {
    if (state != NULL) memset(state, 0, sizeof(*state));
}

int gx_feature_aux_metadata_state_import(
    gx_feature_aux_metadata_state *state,
    uint32_t history_count, int32_t previous_coverage,
    const uint8_t history[GX_FEATURE_AUX_HISTORY_SLOTS][GX_FEATURE_AUX_HISTORY_BYTES],
    int32_t stability_counter, int32_t previous_level) {
    if (state == NULL || history == NULL ||
        history_count > GX_FEATURE_AUX_HISTORY_SLOTS) {
        return -1;
    }
    state->history_count = history_count;
    state->previous_coverage = previous_coverage;
    memcpy(state->history, history, sizeof(state->history));
    state->stability_counter = stability_counter;
    state->previous_level = previous_level;
    return 0;
}

int gx_feature_root_aux_flags_parse(
    const uint8_t *input, int32_t rows, int32_t columns,
    int32_t *packed_state, uint8_t **temporary) {
    int32_t index;
    int32_t prefix_limit;
    int prefix_all_even_zero = 1;
    uint8_t code;
    size_t total;

    if (input == NULL || packed_state == NULL || temporary == NULL ||
        rows <= 0 || columns < 8 ||
        (size_t)rows > SIZE_MAX / (size_t)columns) {
        return -1;
    }
    *temporary = NULL;
    prefix_limit = columns - 8;
    for (index = 0; index < prefix_limit; index += 2) {
        if ((input[index] & UINT8_C(1)) != 0U) {
            prefix_all_even_zero = 0;
            break;
        }
    }
    for (index = 1; index < prefix_limit; index += 2) {
        if ((input[index] & UINT8_C(1)) == 0U) return 0;
    }
    if (prefix_all_even_zero == 0) return 0;

    code = (uint8_t)((input[columns - 8] & UINT8_C(1)) |
                     ((input[columns - 7] & UINT8_C(1)) << 1U));
    if (code == 1U) *packed_state = 1;
    else if (code == 2U) *packed_state = 3;
    else if (code == 3U) *packed_state = 2;
    else *packed_state = 0;
    *packed_state +=
        (int32_t)((input[columns - 5] & UINT8_C(1)) |
                  (((input[columns - 4] & UINT8_C(1)) |
                    ((input[columns - 3] & UINT8_C(1)) << 1U)) << 1U)) * 0x100;

    if ((input[columns - 6] & UINT8_C(1)) == 0U) return 0;
    total = (size_t)rows * (size_t)columns;
    *temporary = (uint8_t *)malloc(total);
    if (*temporary == NULL) return -1;
    memset(*temporary, 1, (size_t)columns);
    for (index = columns; (size_t)index < total; ++index) {
        (*temporary)[index] = input[index] & UINT8_C(1);
    }
    return 1;
}

static uint32_t gx_aux_decode_level(uint32_t value) {
    switch (value) {
        case 1U:
        case 2U:
        case 6U: return 2U;
        case 3U: return 3U;
        case 5U: return 1U;
        case 7U: return 4U;
        case 8U:
        case 9U: return 5U;
        default: return 0U;
    }
}

void gx_feature_root_packed_state_decode(
    uint32_t packed_state, uint32_t *first, uint32_t *second) {
    uint32_t first_raw;
    uint32_t second_raw;
    if (first == NULL || second == NULL) return;
    first_raw = packed_state & UINT32_C(3);
    second_raw = ((uint32_t)((int32_t)packed_state >> 8U)) & UINT32_C(7);
    if (second_raw != 0U) second_raw += 4U;
    *first = gx_aux_decode_level(first_raw);
    *second = gx_aux_decode_level(second_raw);
}

void gx_feature_root_aux_state_decode(
    const uint8_t input[6], uint32_t output[6]) {
    size_t index;
    if (input == NULL || output == NULL) return;
    for (index = 0U; index < 6U; ++index) output[index] = input[index];
}

static int gx_aux_label_components(
    const uint8_t *binary, int32_t rows, int32_t columns,
    int32_t *labels, int32_t *sizes, int32_t maximum_components) {
    static const int32_t dx[4] = {-1, 0, 1, 0};
    static const int32_t dy[4] = {0, -1, 0, 1};
    gx_aux_point *queue;
    size_t count;
    int32_t component_count = 0;
    int32_t y;

    if (binary == NULL || labels == NULL || sizes == NULL ||
        maximum_components <= 0 ||
        gx_aux_safe_pixel_count(columns, rows, &count) != 0) {
        return -1;
    }
    queue = (gx_aux_point *)malloc(
        GX_AUX_COMPONENT_QUEUE_CAPACITY * sizeof(*queue));
    if (queue == NULL) return -1;
    for (size_t index = 0U; index < count; ++index) labels[index] = -1;
    memset(sizes, 0, (size_t)(maximum_components + 1) * sizeof(*sizes));

    for (y = 0; y < rows; ++y) {
        int32_t x;
        for (x = 0; x < columns; ++x) {
            const size_t start = (size_t)y * (size_t)columns + (size_t)x;
            size_t head = 0U;
            size_t tail = 0U;
            size_t queued = 0U;
            int32_t label;
            if (labels[start] != -1) continue;
            if (binary[start] == 0U) {
                labels[start] = 0;
                continue;
            }
            ++component_count;
            label = component_count;
            labels[start] = label;
            ++sizes[label];
            queue[tail].x = x;
            queue[tail].y = y;
            tail = (tail + 1U) % GX_AUX_COMPONENT_QUEUE_CAPACITY;
            ++queued;

            while (queued > 0U) {
                gx_aux_point point = queue[head];
                int direction;
                head = (head + 1U) % GX_AUX_COMPONENT_QUEUE_CAPACITY;
                --queued;
                for (direction = 0; direction < 4; ++direction) {
                    const int32_t nx = point.x + dx[direction];
                    const int32_t ny = point.y + dy[direction];
                    size_t neighbor;
                    if (nx < 0 || nx >= columns || ny < 0 || ny >= rows)
                        continue;
                    neighbor = (size_t)ny * (size_t)columns + (size_t)nx;
                    if (labels[neighbor] != -1) continue;
                    if (binary[neighbor] == 0U) {
                        labels[neighbor] = 0;
                        continue;
                    }
                    labels[neighbor] = label;
                    ++sizes[label];
                    if (queued < GX_AUX_COMPONENT_QUEUE_CAPACITY) {
                        queue[tail].x = nx;
                        queue[tail].y = ny;
                        tail = (tail + 1U) % GX_AUX_COMPONENT_QUEUE_CAPACITY;
                        ++queued;
                    }
                }
            }
            if (component_count >= maximum_components) {
                free(queue);
                return component_count;
            }
        }
    }
    free(queue);
    return component_count;
}

static void gx_aux_select_largest(
    int32_t *sizes, int32_t component_count,
    int32_t *selected, uint32_t selected_count, int32_t threshold) {
    int32_t saved[10];
    uint32_t slot;
    if (selected_count > 10U) return;
    memset(saved, 0, sizeof(saved));
    for (slot = 0U; slot < selected_count; ++slot) {
        int32_t label;
        int32_t best_size = threshold;
        selected[slot] = -2;
        for (label = 0; label <= component_count; ++label) {
            if (sizes[label] > best_size) {
                selected[slot] = label;
                best_size = sizes[label];
            }
        }
        if (selected[slot] > 0) {
            saved[slot] = sizes[selected[slot]];
            sizes[selected[slot]] = 0;
        }
    }
    for (slot = 0U; slot < selected_count; ++slot) {
        if (selected[slot] > 0) sizes[selected[slot]] = saved[slot];
    }
}

static int gx_aux_label_is_selected(int32_t label,
                                    const int32_t *selected,
                                    uint32_t selected_count) {
    uint32_t slot;
    for (slot = 0U; slot < selected_count; ++slot) {
        if (label == selected[slot]) return 1;
    }
    return 0;
}

static int gx_aux_component_filter(
    uint8_t *binary, int32_t rows, int32_t columns,
    uint8_t class_value, uint8_t *classes,
    const gx_feature_aux_metadata_state *state) {
    size_t count;
    int32_t *labels = NULL;
    int32_t *sizes = NULL;
    uint8_t *consensus = NULL;
    int32_t selected[5];
    uint32_t selected_count;
    int32_t primary_threshold;
    int32_t history_threshold;
    int32_t components;

    if (binary == NULL || classes == NULL || state == NULL ||
        gx_aux_safe_pixel_count(columns, rows, &count) != 0) return -1;
    labels = (int32_t *)malloc(count * sizeof(*labels));
    sizes = (int32_t *)calloc(GX_AUX_COMPONENT_MAX + 2U, sizeof(*sizes));
    consensus = (uint8_t *)calloc(count, 1U);
    if (labels == NULL || sizes == NULL || consensus == NULL) {
        free(labels); free(sizes); free(consensus); return -1;
    }
    selected_count = rows < 50 ? 5U : 3U;
    primary_threshold = count < 6000U ? 350 : 500;
    history_threshold = 350;
    if (rows < 50) {
        primary_threshold = 200;
        history_threshold = 200;
    }
    components = gx_aux_label_components(binary, rows, columns, labels, sizes,
                                         GX_AUX_COMPONENT_MAX);
    if (components < 0 || components > GX_AUX_COMPONENT_MAX) {
        free(labels); free(sizes); free(consensus); return -1;
    }
    gx_aux_select_largest(sizes, components, selected, selected_count,
                          primary_threshold);
    for (size_t index = 0U; index < count; ++index) {
        binary[index] = gx_aux_label_is_selected(
            labels[index], selected, selected_count) ? UINT8_C(0xff) : 0U;
    }

    if (state->history_count > 2U) {
        int32_t history_selected[3];
        memset(labels, 0, count * sizeof(*labels));
        memset(sizes, 0,
               (GX_AUX_COMPONENT_HISTORY_MAX + 2U) * sizeof(*sizes));
        for (size_t index = 0U; index < count; ++index) {
            uint8_t votes = 0U;
            if (binary[index] != 0U) {
                for (size_t slot = 0U; slot < GX_FEATURE_AUX_HISTORY_SLOTS;
                     ++slot) {
                    if (state->history[slot][index] == class_value) ++votes;
                }
            }
            consensus[index] = votes > 2U ? UINT8_C(0xff) : 0U;
        }
        components = gx_aux_label_components(
            consensus, rows, columns, labels, sizes,
            GX_AUX_COMPONENT_HISTORY_MAX);
        if (components < 0 ||
            components > GX_AUX_COMPONENT_HISTORY_MAX) {
            free(labels); free(sizes); free(consensus); return -1;
        }
        gx_aux_select_largest(sizes, components, history_selected, 3U,
                              history_threshold);
        for (size_t index = 0U; index < count; ++index) {
            if (classes[index] == 0U) {
                classes[index] = gx_aux_label_is_selected(
                    labels[index], history_selected, 3U) ? class_value : 0U;
            }
        }
    }
    free(labels); free(sizes); free(consensus);
    return 0;
}

static uint8_t gx_aux_neighbor_index(const uint8_t *image,
                                     int32_t width, int32_t x, int32_t y) {
    const size_t center = (size_t)y * (size_t)width + (size_t)x;
    uint8_t value = 0U;
    value |= (uint8_t)((image[center + (size_t)width - 1U] & 1U) << 0U);
    value |= (uint8_t)((image[center + (size_t)width] & 1U) << 1U);
    value |= (uint8_t)((image[center + (size_t)width + 1U] & 1U) << 2U);
    value |= (uint8_t)((image[center - 1U] & 1U) << 3U);
    value |= (uint8_t)((image[center + 1U] & 1U) << 4U);
    value |= (uint8_t)((image[center - (size_t)width - 1U] & 1U) << 5U);
    value |= (uint8_t)((image[center - (size_t)width] & 1U) << 6U);
    value |= (uint8_t)((image[center - (size_t)width + 1U] & 1U) << 7U);
    return value;
}

static int gx_aux_pattern_fill(uint8_t *input, uint8_t *output,
                               int32_t width, int32_t height) {
    size_t count;
    int changed;
    if (input == NULL || output == NULL || width < 3 || height < 3 ||
        gx_aux_safe_pixel_count(width, height, &count) != 0) return -1;
    memcpy(output, input, count);
    do {
        int32_t y;
        changed = 0;
        for (y = 1; y < height - 1; ++y) {
            int32_t x = 1;
            while (x < width - 1) {
                const size_t index = (size_t)y * (size_t)width + (size_t)x;
                if (input[index] == 0U &&
                    (input[index - 1U] == 1U || input[index + 1U] == 1U) &&
                    gx_aux_fill_lut[gx_aux_neighbor_index(
                        input, width, x, y)] == 1U) {
                    input[index] = 1U;
                    output[index] = 1U;
                    changed = 1;
                    ++x;
                }
                ++x;
            }
        }
        for (int32_t x = 1; x < width - 1; ++x) {
            int32_t y = 1;
            while (y < height - 1) {
                const size_t index = (size_t)y * (size_t)width + (size_t)x;
                if (input[index] == 0U &&
                    (input[index - (size_t)width] == 1U ||
                     input[index + (size_t)width] == 1U) &&
                    gx_aux_fill_lut[gx_aux_neighbor_index(
                        input, width, x, y)] == 1U) {
                    input[index] = 1U;
                    output[index] = 1U;
                    changed = 1;
                    ++y;
                }
                ++y;
            }
        }
    } while (changed != 0);
    return 0;
}

static void gx_aux_history_update(gx_feature_aux_metadata_state *state,
                                  const uint8_t *classes, size_t count) {
    uint8_t *destination;
    if (state->history_count < GX_FEATURE_AUX_HISTORY_SLOTS) {
        destination = state->history[state->history_count];
    } else {
        memcpy(state->history[0], state->history[1],
               GX_FEATURE_AUX_HISTORY_BYTES);
        memcpy(state->history[1], state->history[2],
               GX_FEATURE_AUX_HISTORY_BYTES);
        destination = state->history[2];
    }
    memcpy(destination, classes, count);
    if (state->history_count < GX_FEATURE_AUX_HISTORY_SLOTS)
        ++state->history_count;
}

int gx_feature_root_aux_metadata_prepare_profile24(
    const gx_feature_pyramid_image *optional_gray,
    const gx_feature_pyramid_image *mask,
    const uint32_t context[GX_FEATURE_AUX_CONTEXT_WORDS],
    int32_t *metric_state, uint8_t *aux_map,
    gx_feature_aux_metadata_state *state,
    gx_feature_aux_metadata_stats *stats) {
    uint8_t *selected = NULL;
    uint8_t *binary = NULL;
    uint8_t *classes = NULL;
    uint8_t *high = NULL;
    uint8_t *filled = NULL;
    size_t count;
    uint32_t class_one = 0U;
    uint32_t class_two = 0U;
    uint32_t dark_holes = 0U;
    uint32_t anomaly = 0U;
    int32_t primary;
    int32_t secondary;
    int32_t tertiary;
    int32_t mode_group;
    int32_t packed_first;
    int32_t coverage;
    int32_t dark_reference;
    int32_t resolved;

    if (optional_gray == NULL || mask == NULL || context == NULL ||
        metric_state == NULL || aux_map == NULL || state == NULL ||
        optional_gray->pixels == NULL || mask->pixels == NULL ||
        optional_gray->element_bytes != 1 || mask->element_bytes != 1 ||
        optional_gray->width != mask->width ||
        optional_gray->height != mask->height ||
        context[2] != GX_FEATURE_AUX_PROFILE24 ||
        gx_aux_safe_pixel_count(optional_gray->width,
                                optional_gray->height, &count) != 0 ||
        optional_gray->element_count < 0 || mask->element_count < 0 ||
        (size_t)optional_gray->element_count < count ||
        (size_t)mask->element_count < count) {
        return -1;
    }
    selected = (uint8_t *)calloc(count, 1U);
    binary = (uint8_t *)malloc(count);
    classes = (uint8_t *)calloc(count, 1U);
    high = (uint8_t *)malloc(count);
    filled = (uint8_t *)calloc(count, 1U);
    if (selected == NULL || binary == NULL || classes == NULL ||
        high == NULL || filled == NULL) {
        free(selected); free(binary); free(classes); free(high); free(filled);
        return -1;
    }

    mode_group = (int32_t)context[3];
    primary = (int32_t)context[4];
    packed_first = (int32_t)context[5];
    secondary = (int32_t)context[6];
    tertiary = (int32_t)context[7];
    coverage = (int32_t)context[8];
    dark_reference = (int32_t)context[9];
    if (mode_group != 2) state->previous_coverage = coverage;

    for (size_t index = 0U; index < count; ++index) {
        const uint8_t pixel = ((const uint8_t *)optional_gray->pixels)[index];
        binary[index] = pixel < UINT8_C(0x80) ? UINT8_C(0xff) : 0U;
    }
    if (gx_aux_component_filter(binary, optional_gray->height,
                                optional_gray->width, 1U, classes,
                                state) != 0) goto fail;
    for (size_t index = 0U; index < count; ++index) {
        if (binary[index] != 0U) selected[index] = 1U;
        binary[index] =
            (((const uint8_t *)optional_gray->pixels)[index] >= UINT8_C(0x80) &&
             ((const uint8_t *)mask->pixels)[index] != 0U) ?
            UINT8_C(0xff) : 0U;
    }
    if (gx_aux_component_filter(binary, optional_gray->height,
                                optional_gray->width, 2U, classes,
                                state) != 0) goto fail;
    for (size_t index = 0U; index < count; ++index) {
        if (binary[index] != 0U) selected[index] = 2U;
        high[index] = ((const uint8_t *)optional_gray->pixels)[index] >
                      UINT8_C(0x7f) ? 1U : 0U;
    }
    if (gx_aux_pattern_fill(high, filled, optional_gray->width,
                            optional_gray->height) != 0) goto fail;

    for (size_t index = 0U; index < count; ++index) {
        if (classes[index] == 1U) ++class_one;
        else if (classes[index] == 2U) ++class_two;
        if (filled[index] == 0U &&
            ((const uint8_t *)optional_gray->pixels)[index] < UINT8_C(0x1e)) {
            ++dark_holes;
        } else {
            filled[index] = UINT8_C(0xff);
        }
    }

    resolved = primary;
    if (class_one > 300U || class_two > 300U ||
        class_one + class_two > 400U) {
        const int64_t dark_scaled = (int64_t)dark_holes * INT64_C(100);
        anomaly = 1U;
        ++secondary;
        if (secondary < 3) secondary = 3;
        if (resolved < 4) resolved = 4;
        if ((int64_t)dark_reference * INT64_C(256) < dark_scaled) {
            if (dark_scaled <= (int64_t)dark_reference * INT64_C(300))
                secondary = 5;
        } else {
            secondary = 6;
        }
    }
    if ((int64_t)dark_holes * INT64_C(100) <=
        (int64_t)dark_reference * INT64_C(256) && secondary < 2) {
        secondary = 2;
    }
    if (mode_group == 2) {
        if (resolved < state->previous_level) resolved = state->previous_level;
    } else {
        if (state->stability_counter < 5 &&
            (resolved > 1 || secondary > 1 || tertiary > 1)) {
            ++state->stability_counter;
        }
        if (state->stability_counter > 0 && resolved < 2 &&
            secondary < 2 && tertiary < 2) {
            --state->stability_counter;
        }
    }
    state->previous_level = resolved;
    if (state->stability_counter > 2 && resolved < 2) resolved = 2;
    *metric_state = packed_first + resolved * 0x100;

    if ((coverage > 74 && mode_group != 2) ||
        (state->previous_coverage < 75 && coverage > 74)) {
        gx_aux_history_update(state, selected, count);
    }

    aux_map[0] = (uint8_t)secondary;
    aux_map[1] = (uint8_t)mode_group;
    aux_map[2] = (uint8_t)tertiary;
    aux_map[3] = (uint8_t)(primary + 4);
    aux_map[4] = 0U;
    aux_map[5] = (uint8_t)anomaly;
    if (stats != NULL) {
        stats->class_one_pixels = class_one;
        stats->class_two_pixels = class_two;
        stats->dark_hole_pixels = dark_holes;
        stats->anomaly_flag = anomaly;
        stats->resolved_primary_level = resolved;
        stats->resolved_secondary_level = secondary;
        stats->metric_state = *metric_state;
    }
    free(selected); free(binary); free(classes); free(high); free(filled);
    return 0;

fail:
    free(selected); free(binary); free(classes); free(high); free(filled);
    return -1;
}

static uint32_t gx_aux_hash(const uint8_t *data, size_t bytes) {
    uint32_t hash = UINT32_C(2166136261);
    for (size_t index = 0U; index < bytes; ++index) {
        hash ^= data[index];
        hash *= UINT32_C(16777619);
    }
    return hash;
}

int gx_feature_aux_metadata_selftest(void) {
    enum { width = 31, height = 19, pixels = width * height };
    gx_feature_pyramid_image gray;
    gx_feature_pyramid_image mask;
    gx_feature_aux_metadata_state state;
    gx_feature_aux_metadata_stats stats;
    uint8_t gray_pixels[pixels];
    uint8_t mask_pixels[pixels];
    uint8_t aux_map[pixels];
    uint32_t context[GX_FEATURE_AUX_CONTEXT_WORDS];
    uint32_t first = 99U;
    uint32_t second = 99U;
    uint32_t decoded[6];
    uint8_t state_bytes[6] = {1U,2U,3U,4U,5U,6U};
    uint8_t auxiliary[16];
    uint8_t *temporary = NULL;
    int32_t packed = 0;
    int32_t metric = 0;

    memset(&gray, 0, sizeof(gray));
    memset(&mask, 0, sizeof(mask));
    memset(context, 0, sizeof(context));
    memset(auxiliary, 0, sizeof(auxiliary));
    gx_feature_aux_metadata_state_reset(&state);
    for (size_t index = 0U; index < pixels; ++index) {
        gray_pixels[index] = index < 400U ? 12U : 220U;
        mask_pixels[index] = UINT8_C(0xff);
    }
    memset(aux_map, 0, sizeof(aux_map));
    gray.width = width; gray.height = height; gray.element_count = pixels;
    gray.element_bytes = 1; gray.pixels = gray_pixels;
    mask = gray; mask.pixels = mask_pixels;
    context[2] = GX_FEATURE_AUX_PROFILE24;
    context[3] = 0U;
    context[4] = 1U;
    context[5] = 2U;
    context[6] = 0U;
    context[7] = 0U;
    context[8] = 80U;
    context[9] = 1U;
    if (gx_feature_root_aux_metadata_prepare_profile24(
            &gray, &mask, context, &metric, aux_map, &state, &stats) != 0 ||
        state.history_count != 1U || metric != 0x102 ||
        stats.class_one_pixels != 0U || stats.class_two_pixels != 0U ||
        stats.dark_hole_pixels != 54U || stats.anomaly_flag != 0U ||
        stats.resolved_primary_level != 1 ||
        stats.resolved_secondary_level != 0 ||
        memcmp(aux_map, (const uint8_t[6]){0U, 0U, 0U, 5U, 0U, 0U},
               6U) != 0 || gx_aux_hash(aux_map, 6U) == 0U) {
        return -1;
    }

    gx_feature_aux_metadata_state_reset(&state);
    state.history_count = GX_FEATURE_AUX_HISTORY_SLOTS;
    for (size_t slot = 0U; slot < GX_FEATURE_AUX_HISTORY_SLOTS; ++slot)
        memset(state.history[slot], 1, 400U);
    memset(aux_map, 0, sizeof(aux_map));
    metric = 0;
    if (gx_feature_root_aux_metadata_prepare_profile24(
            &gray, &mask, context, &metric, aux_map, &state, &stats) != 0 ||
        state.history_count != GX_FEATURE_AUX_HISTORY_SLOTS ||
        metric != 0x402 || stats.class_one_pixels != 400U ||
        stats.class_two_pixels != 0U || stats.dark_hole_pixels != 54U ||
        stats.anomaly_flag != 1U || stats.resolved_primary_level != 4 ||
        stats.resolved_secondary_level != 3 ||
        memcmp(aux_map, (const uint8_t[6]){3U, 0U, 0U, 5U, 0U, 1U},
               6U) != 0) {
        return -1;
    }
    gx_feature_aux_metadata_state_reset(&state);
    for (size_t index = 0U; index < pixels; ++index) {
        gray_pixels[index] = (uint8_t)((index * 37U + 19U) & UINT32_C(0xff));
        mask_pixels[index] = (index % 7U) == 0U ? 0U : UINT8_C(0xff);
    }
    memset(context, 0, sizeof(context));
    context[2] = GX_FEATURE_AUX_PROFILE24;
    context[5] = 4U;
    context[7] = 2U;
    context[8] = 60U;
    context[9] = 3U;
    memset(aux_map, 0, sizeof(aux_map));
    metric = 0;
    if (gx_feature_root_aux_metadata_prepare_profile24(
            &gray, &mask, context, &metric, aux_map, &state, &stats) != 0 ||
        state.history_count != 0U || state.previous_coverage != 60 ||
        state.stability_counter != 1 || state.previous_level != 0 ||
        metric != 4 || stats.class_one_pixels != 0U ||
        stats.class_two_pixels != 0U || stats.dark_hole_pixels != 70U ||
        stats.anomaly_flag != 0U ||
        memcmp(aux_map, (const uint8_t[6]){0U, 0U, 2U, 4U, 0U, 0U},
               6U) != 0) {
        return -1;
    }

    auxiliary[1] = 1U;
    auxiliary[3] = 1U;
    auxiliary[5] = 1U;
    auxiliary[7] = 1U;
    auxiliary[8] = 1U;
    auxiliary[9] = 0U;
    auxiliary[10] = 0U;
    auxiliary[11] = 1U;
    auxiliary[12] = 1U;
    auxiliary[13] = 0U;
    if (gx_feature_root_aux_flags_parse(
            auxiliary, 1, 16, &packed, &temporary) != 0 ||
        temporary != NULL) return -1;
    gx_feature_root_packed_state_decode(UINT32_C(0x301), &first, &second);
    if (first != 2U || second != 4U) return -1;
    gx_feature_root_aux_state_decode(state_bytes, decoded);
    for (size_t index = 0U; index < 6U; ++index) {
        if (decoded[index] != state_bytes[index]) return -1;
    }
    return 0;
}
