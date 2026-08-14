#pragma once

#include "app_types.h"

extern const int8_t s_hint_offsets[8];

extern DaypartSettings s_dayparts;
extern MedicationSettings s_medications[MAX_MEDICATIONS];
extern uint8_t s_medication_count;
extern MedicationSettings s_pending_medications[MAX_MEDICATIONS];
extern uint8_t s_pending_count;
extern uint16_t s_pending_received_mask;

extern char s_row_labels[MAX_LIST_ROWS][MEDICATION_LABEL_LENGTH];
extern const char *s_rows[MAX_LIST_ROWS];
extern MedicationRowKind s_row_kinds[MAX_LIST_ROWS];
extern int8_t s_row_medication_indices[MAX_LIST_ROWS];
extern uint8_t s_list_row_count;

extern int8_t s_intake_medication_indices[MAX_LIST_ROWS];
extern uint8_t s_intake_row_count;
extern MedicationSymbol s_intake_symbol;
extern bool s_intake_symbol_set;

extern MedicationTime s_visible_medication_time;
extern bool s_visible_medication_time_set;
extern bool s_pills_confirmed;
extern bool s_pen_confirmed;
#define LIST_ROW_COUNT ((int)s_list_row_count)
#define INTAKE_ROW_COUNT ((int)s_intake_row_count)

extern Window *s_window;
extern Layer *s_canvas_layer;
extern Layer *s_band_layer;
extern Layer *s_band_arrow_layer;
extern Layer *s_confirmation_layer;
extern BandAnimationState s_band;
extern GBitmap *s_sheet;
extern GBitmap *s_frames[FRAME_COUNT];
extern AppTimer *s_ui_timer;
extern AppTimer *s_confirmation_timer;
extern AppTimer *s_scroll_physics_timer;
extern AppTimer *s_band_animation_timer;
extern GFont s_medication_font;
extern GFont s_medication_detail_font;
extern GFont s_header_font;
extern int16_t s_frame_width;
extern int16_t s_frame_height;
extern uint8_t s_animation_tick;
extern uint16_t s_medication_marquee_tick;
extern int8_t s_medication_marquee_row;
extern int8_t s_taken_hint_phase;
extern bool s_light_theme;
extern ThemeMode s_theme_mode;
extern AppLanguage s_language;
extern bool s_show_swiss_emblem;
extern bool s_show_japanese_pattern;
extern bool s_confirmed_screen_active;

extern bool s_transfer_screen_active;
extern AppTimer *s_transfer_close_timer;
extern AppTimer *s_transfer_animation_timer;
extern TransferAnimationState s_transfer_animation_state;
extern uint16_t s_transfer_animation_elapsed_ms;
extern int16_t s_transfer_fall_offset;

extern PillPhysicsBody s_pill_physics_bodies[PILL_PHYSICS_MAX_BODIES];
extern uint8_t s_pill_physics_body_count;
extern AppTimer *s_pill_physics_timer;
extern bool s_pill_physics_accel_subscribed;
extern bool s_pill_physics_window_visible;
extern int16_t s_pill_physics_gravity_x;
extern int16_t s_pill_physics_gravity_y;
extern int16_t s_pill_physics_last_target_x;
extern int16_t s_pill_physics_last_target_y;
extern uint8_t s_pill_physics_quiet_frames;
extern uint8_t s_pill_physics_sensor_quiet_samples;

extern uint8_t s_alarm_audio_volume;
extern bool s_alarm_vibration_enabled;
extern uint8_t s_alarm_reminder_interval_minutes;
extern AlarmWindowState s_alarm_window_state;
extern bool s_alarm_window_state_loaded;
extern bool s_alarm_active;
extern bool s_alarm_launch_pending;
extern time_t s_alarm_stop_time;
extern uint8_t s_alarm_due_symbol_mask;
extern AppTimer *s_alarm_pulse_timer;
extern AppTimer *s_alarm_audio_pump_timer;
extern ResHandle s_alarm_audio_resource;
extern size_t s_alarm_audio_resource_size;
extern size_t s_alarm_audio_resource_offset;
extern uint8_t s_alarm_audio_buffer[ALARM_AUDIO_BUFFER_SIZE];
extern size_t s_alarm_audio_buffer_size;
extern size_t s_alarm_audio_buffer_offset;
extern bool s_alarm_audio_active;

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
extern MedicationAppearance s_pending_medication_appearances[MAX_MEDICATIONS];
extern uint8_t s_medication_appearance_count;
extern uint8_t s_pending_medication_appearance_count;
extern uint16_t s_pending_medication_appearance_mask;
