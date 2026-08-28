#include "app_state.h"

DaypartSettings s_dayparts;
MedicationSettings s_medications[MAX_MEDICATIONS];
uint8_t s_medication_count;

int8_t s_row_medication_indices[MAX_LIST_ROWS];
uint8_t s_list_row_count;

int8_t s_intake_medication_indices[MAX_LIST_ROWS];
uint8_t s_intake_row_count;
MedicationSymbol s_intake_symbol;
bool s_intake_symbol_set;

MedicationTime s_visible_medication_time;
bool s_visible_medication_time_set;

Window *s_window;
Layer *s_canvas_layer;
Layer *s_band_layer;
Layer *s_band_arrow_layer;
Layer *s_confirmation_layer;
BandAnimationState s_band;
AppTimer *s_band_animation_timer;
GFont s_medication_font;
GFont s_medication_detail_font;
GFont s_header_font;
uint8_t s_animation_tick;
uint16_t s_medication_marquee_tick;
int8_t s_medication_marquee_row = -1;
bool s_light_theme;
ThemeMode s_theme_mode = THEME_MODE_DARK;
AppLanguage s_language = APP_LANGUAGE_GERMAN;
bool s_show_japanese_pattern = true;
bool s_confirmed_screen_active;

bool s_transfer_screen_active;

uint8_t s_alarm_audio_volume = DEFAULT_ALARM_AUDIO_VOLUME;
bool s_alarm_vibration_enabled = DEFAULT_ALARM_VIBRATION_ENABLED;
uint8_t s_alarm_reminder_interval_minutes = DEFAULT_ALARM_REMINDER_INTERVAL_MINUTES;
bool s_alarm_active;
bool s_alarm_launch_pending;

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
uint8_t s_medication_appearance_count;
