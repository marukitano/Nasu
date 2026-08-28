#pragma once

#include "app_types.h"

/* Medication grouping, dayparts and visible rows. */
bool medication_is_scheduled_on_date(
    const MedicationSettings *medication,
    const struct tm *local_date
);
bool medication_interval_hours_valid(int value);
bool medication_interval_window_at(
    uint8_t medication_index,
    time_t timestamp,
    time_t *window_start,
    time_t *window_end
);
bool medication_runtime_view(
    uint8_t medication_index,
    MedicationRuntimeView *view
);
MedicationTime current_medication_time(void);
bool medication_group_first_index(
    MedicationSymbol symbol,
    uint8_t *medication_index
);
bool medication_group_is_due(
    MedicationSymbol symbol
);
bool active_medication_symbol(
    MedicationSymbol *symbol
);
void rebuild_medication_rows(void);
void rebuild_all_medication_rows(void);
void refresh_medication_rows_for_time(void);
bool unconfirmed_medication_group_is_due(void);
void daypart_tick_handler(
    struct tm *tick_time,
    TimeUnits units_changed
);
