#pragma once

#include "app_types.h"

/* Watch-side AppMessage settings and persistence. */
const MedicationIntervalSettings *medication_interval_settings_at(
    uint8_t medication_index
);
uint16_t medication_alarm_minute_at(
    uint8_t medication_index
);
void watch_settings_init(void);
void watch_settings_deinit(void);
