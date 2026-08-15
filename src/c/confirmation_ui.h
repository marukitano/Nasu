#pragma once

#include "app_types.h"

/* Medication confirmation and settings-transfer animation. */
void confirmation_cancel_animation(void);
void confirmation_cancel_transfer_timers(void);
void back_button_handler(
    ClickRecognizerRef recognizer,
    void *context
);
void confirmation_update_proc(
    Layer *layer,
    GContext *ctx
);
void draw_confirmed_page(
    GContext *ctx,
    GRect bounds
);
void schedule_transfer_close(void);
void select_button_down(
    ClickRecognizerRef recognizer,
    void *context
);
void select_button_up(
    ClickRecognizerRef recognizer,
    void *context
);
void show_transfer_screen(void);
