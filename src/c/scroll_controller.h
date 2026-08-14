#pragma once

#include "app_types.h"

/* Scroll snapping, touch input and band animation. */
void cancel_scroll_physics(void);
void schedule_band_animation(void);
void set_band_and_arrow_hidden(
    bool hidden
);
void set_band_layer_x_q8(
    int32_t x_q8
);
bool step_snap_index(int direction);
void scroll_to_snap_index(int target_index);
int scroll_vespa_snap_index(void);
int scroll_all_medication_row_for_snap(int snap_index);
int32_t scroll_vespa_page_top_y(void);
int32_t scroll_all_medications_page_start_y(void);
int32_t scroll_snap_anchor_y(int snap_index);
void update_band_animation_target(void);
int32_t visual_canvas_offset_y(void);

#if defined(PBL_TOUCH)
void touch_handler(
    const TouchEvent *event,
    void *context
);
#endif
