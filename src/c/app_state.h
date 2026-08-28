#pragma once

#include "app_types.h"

extern DaypartSettings s_dayparts;
extern MedicationSettings s_medications[MAX_MEDICATIONS];
extern uint8_t s_medication_count;

extern int8_t s_row_medication_indices[MAX_LIST_ROWS];
extern uint8_t s_list_row_count;

extern int8_t s_intake_medication_indices[MAX_LIST_ROWS];
extern uint8_t s_intake_row_count;
extern MedicationSymbol s_intake_symbol;
extern bool s_intake_symbol_set;

extern MedicationTime s_visible_medication_time;
extern bool s_visible_medication_time_set;
#define LIST_ROW_COUNT ((int)s_list_row_count)
#define INTAKE_ROW_COUNT ((int)s_intake_row_count)

extern Window *s_window;
extern Layer *s_canvas_layer;
extern Layer *s_band_layer;
extern Layer *s_band_arrow_layer;
extern Layer *s_confirmation_layer;
extern BandAnimationState s_band;
extern AppTimer *s_band_animation_timer;
extern GFont s_medication_font;
extern GFont s_medication_detail_font;
extern GFont s_header_font;
extern uint8_t s_animation_tick;
extern uint16_t s_medication_marquee_tick;
extern int8_t s_medication_marquee_row;
extern bool s_light_theme;
extern ThemeMode s_theme_mode;
extern AppLanguage s_language;
extern bool s_show_japanese_pattern;
extern bool s_confirmed_screen_active;

extern bool s_transfer_screen_active;

extern uint8_t s_alarm_audio_volume;
extern bool s_alarm_vibration_enabled;
extern uint8_t s_alarm_reminder_interval_minutes;
extern AlarmWindowState s_alarm_window_state;
extern bool s_alarm_active;
extern bool s_alarm_launch_pending;

extern ScrollState s_scroll;
#if defined(PBL_TOUCH)
extern ScrollTouchState s_touch;
#endif
extern int16_t s_confirm_radius;
extern int16_t s_confirm_max_radius;
extern ConfirmationState s_confirmation_state;
extern MedicationSymbol s_confirmation_symbol;
extern bool s_confirmation_symbol_set;
extern int16_t s_check_size;
extern CheckState s_check_state;

extern MedicationAppearance s_medication_appearances[MAX_MEDICATIONS];
extern uint8_t s_medication_appearance_count;
