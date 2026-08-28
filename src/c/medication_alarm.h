#pragma once

#include "app_types.h"

/* Alarm scheduling, wakeups, audio and vibration. */
bool alarm_visuals_paused(void);
void medication_alarm_init(void);
void medication_alarm_deinit(void);
void alarm_confirmation_received(
    MedicationSymbol symbol
);
void alarm_handle_minute_tick(
    const struct tm *tick_time
);
void alarm_refresh_window_state(void);
bool alarm_reminder_interval_valid(int value);
bool alarm_reset_after_settings_save(void);
void alarm_start(void);
void alarm_stop(void);
uint8_t alarm_unconfirmed_symbol_mask_at(
    time_t timestamp
);
bool alarm_medication_is_unconfirmed_due_at(
    uint8_t medication_index,
    time_t timestamp
);
bool alarm_intake_navigation_lock_required(void);
time_t alarm_next_timestamp(void);
void apply_alarm_settings(
    uint8_t volume,
    bool vibration_enabled,
    uint8_t reminder_interval_minutes,
    bool save
);
void load_alarm_settings(void);
void schedule_next_alarm_wakeup(void);
