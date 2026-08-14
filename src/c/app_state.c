#include "app_state.h"

const int8_t s_hint_offsets[8] = {
  0, 1, 3, 5, 3, 1, 0, 0
};

DaypartSettings s_dayparts;
MedicationSettings s_medications[MAX_MEDICATIONS];
uint8_t s_medication_count;
MedicationSettings s_pending_medications[MAX_MEDICATIONS];
uint8_t s_pending_count;
uint16_t s_pending_received_mask;

char s_row_labels[MAX_LIST_ROWS][MEDICATION_LABEL_LENGTH];
const char *s_rows[MAX_LIST_ROWS];
MedicationRowKind s_row_kinds[MAX_LIST_ROWS];
int8_t s_row_medication_indices[MAX_LIST_ROWS];
uint8_t s_list_row_count = 1;

int8_t s_intake_medication_indices[MAX_LIST_ROWS];
uint8_t s_intake_row_count;
MedicationSymbol s_intake_symbol;
bool s_intake_symbol_set;

MedicationTime s_visible_medication_time;
bool s_visible_medication_time_set;
bool s_pills_confirmed;
bool s_pen_confirmed;

Window *s_window;
Layer *s_canvas_layer;
Layer *s_band_layer;
Layer *s_band_arrow_layer;
Layer *s_confirmation_layer;
BandAnimationState s_band;
GBitmap *s_sheet;
GBitmap *s_frames[FRAME_COUNT];
AppTimer *s_ui_timer;
AppTimer *s_confirmation_timer;
AppTimer *s_scroll_physics_timer;
AppTimer *s_band_animation_timer;
GFont s_medication_font;
GFont s_medication_detail_font;
GFont s_header_font;
int16_t s_frame_width;
int16_t s_frame_height;
uint8_t s_animation_tick;
uint16_t s_medication_marquee_tick;
int8_t s_medication_marquee_row = -1;
int8_t s_taken_hint_phase;
bool s_light_theme;
ThemeMode s_theme_mode = THEME_MODE_DARK;
AppLanguage s_language = APP_LANGUAGE_GERMAN;
bool s_show_swiss_emblem = true;
bool s_show_japanese_pattern = true;
bool s_confirmed_screen_active;

bool s_transfer_screen_active;
AppTimer *s_transfer_close_timer;
AppTimer *s_transfer_animation_timer;
TransferAnimationState s_transfer_animation_state;
uint16_t s_transfer_animation_elapsed_ms;
int16_t s_transfer_fall_offset;

PillPhysicsBody s_pill_physics_bodies[PILL_PHYSICS_MAX_BODIES];
uint8_t s_pill_physics_body_count;
AppTimer *s_pill_physics_timer;
bool s_pill_physics_accel_subscribed;
bool s_pill_physics_window_visible;
int16_t s_pill_physics_gravity_x;
int16_t s_pill_physics_gravity_y;
int16_t s_pill_physics_last_target_x;
int16_t s_pill_physics_last_target_y;
uint8_t s_pill_physics_quiet_frames;
uint8_t s_pill_physics_sensor_quiet_samples;

uint8_t s_alarm_audio_volume = DEFAULT_ALARM_AUDIO_VOLUME;
bool s_alarm_vibration_enabled = DEFAULT_ALARM_VIBRATION_ENABLED;
uint8_t s_alarm_reminder_interval_minutes = DEFAULT_ALARM_REMINDER_INTERVAL_MINUTES;
AlarmWindowState s_alarm_window_state;
bool s_alarm_window_state_loaded;
bool s_alarm_active;
bool s_alarm_launch_pending;
time_t s_alarm_stop_time;
uint8_t s_alarm_due_symbol_mask;
AppTimer *s_alarm_pulse_timer;
AppTimer *s_alarm_audio_pump_timer;
ResHandle s_alarm_audio_resource;
size_t s_alarm_audio_resource_size;
size_t s_alarm_audio_resource_offset;
uint8_t s_alarm_audio_buffer[ALARM_AUDIO_BUFFER_SIZE];
size_t s_alarm_audio_buffer_size;
size_t s_alarm_audio_buffer_offset;
bool s_alarm_audio_active;

ScrollState s_scroll;
#if defined(PBL_TOUCH)
ScrollTouchState s_touch;
#endif
int16_t s_confirm_radius;
int16_t s_confirm_max_radius;
ConfirmationState s_confirmation_state;
MedicationSymbol s_confirmation_symbol;
bool s_confirmation_symbol_set;
int16_t s_check_size;
CheckState s_check_state;

MedicationAppearance s_medication_appearances[MAX_MEDICATIONS];
MedicationAppearance s_pending_medication_appearances[MAX_MEDICATIONS];
uint8_t s_medication_appearance_count;
uint8_t s_pending_medication_appearance_count;
uint16_t s_pending_medication_appearance_mask;
