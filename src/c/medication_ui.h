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
GColor theme_background_color(void);
