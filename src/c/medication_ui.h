#pragma once

#include "app_types.h"

/* Watch window and screen orchestration. */
void medication_ui_init(void);
void medication_ui_deinit(void);
void apply_theme(
    bool light_theme,
    bool save
);
void apply_theme_mode(
    ThemeMode mode,
    bool save
);
int32_t pill_arena_origin_y(void);
int32_t current_pill_y(void);
void mark_scene_dirty(void);
void refresh_app_screen_state(void);
void medication_ui_begin_alarm_sequence(void);
void medication_ui_return_to_vespa_after_confirmation(void);
void medication_ui_scroll_settled(void);
bool medication_ui_alarm_transitioning_to_pills(void);
bool medication_ui_alarm_navigation_locked(void);
GColor theme_background_color(void);
