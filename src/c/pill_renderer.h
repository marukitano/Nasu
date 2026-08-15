#pragma once

#include "app_types.h"

/* Medication and pill rendering. */
void pill_renderer_deinit(void);
bool medication_name_needs_marquee(
    int row_index
);
void draw_intake_medications(
    GContext *ctx,
    GRect bounds,
    int32_t scroll_offset_y,
    GColor text_color,
    GColor background_color
);
void draw_medications(
    GContext *ctx,
    GRect bounds,
    int32_t scroll_offset_y,
    GColor text_color,
    GColor background_color
);
void draw_physics_pills(
    GContext *ctx,
    GRect bounds,
    int32_t arena_y
);
void draw_pen_alert_animation(
    GContext *ctx,
    GRect bounds,
    int32_t scroll_offset_y,
    uint8_t phase,
    GColor outline_color
);
