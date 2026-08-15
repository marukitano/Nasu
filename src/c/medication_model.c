#include <pebble.h>

#include <string.h>

#include "app_state.h"
#include "app_util.h"
#include "medication_model.h"
#include "watch_settings.h"
#include "medication_alarm.h"
#include "pill_physics.h"
#include "pill_renderer.h"
#include "scroll_controller.h"
#include "confirmation_ui.h"
#include "medication_ui.h"

static MedicationTime medication_time_for_minute(
    int minute
);
static bool medication_group_is_confirmed(
    MedicationSymbol symbol
);
static bool medication_name_is_listed(
    const char *name
);
static bool medication_matches_group(
    const MedicationSettings *medication,
    time_t timestamp,
    MedicationSymbol symbol
);
void daypart_tick_handler(
    struct tm *tick_time,
    TimeUnits units_changed
);

static MedicationTime medication_time_for_minute(
    int minute
) {
  if (
    minute >= s_dayparts.morning &&
    minute < s_dayparts.noon
  ) {
    return MEDICATION_TIME_MORNING;
  }

  if (
    minute >= s_dayparts.noon &&
    minute < s_dayparts.evening
  ) {
    return MEDICATION_TIME_NOON;
  }

  if (
    minute >= s_dayparts.evening &&
    minute < s_dayparts.night
  ) {
    return MEDICATION_TIME_EVENING;
  }

  return MEDICATION_TIME_NIGHT;
}

bool medication_is_scheduled_on_date(
    const MedicationSettings *medication,
    const struct tm *local_date
) {
  if (
    !medication ||
    !local_date ||
    !medication->enabled
  ) {
    return false;
  }

  if (
    medication->schedule ==
        MEDICATION_SCHEDULE_DAILY
  ) {
    return true;
  }

  if (
    medication->schedule ==
        MEDICATION_SCHEDULE_WEEKLY
  ) {
    const uint8_t monday_based_weekday =
        (uint8_t)((local_date->tm_wday + 6) % 7);

    return medication->day ==
        monday_based_weekday;
  }

  if (
    medication->schedule ==
        MEDICATION_SCHEDULE_MONTHLY
  ) {
    return medication->day ==
        local_date->tm_mday;
  }

  return false;
}

bool medication_is_due_at(
    const MedicationSettings *medication,
    time_t timestamp
) {
  if (!medication || !medication->enabled) {
    return false;
  }

  struct tm *local_ptr = localtime(&timestamp);

  if (!local_ptr) {
    return false;
  }

  struct tm schedule_date = *local_ptr;
  const int minute =
      schedule_date.tm_hour * 60 +
      schedule_date.tm_min;
  const MedicationTime slot =
      medication_time_for_minute(minute);

  /*
   * Das Nachtfenster läuft über Mitternacht:
   *
   *   Dienstag 22:00 -> Mittwoch 06:00
   *
   * Ein für "Dienstag Nacht" geplantes Medikament bleibt deshalb auch
   * Mittwoch um 01:00 Teil des Dienstag-Fensters. Der Alarm-Scheduler
   * verwendet dafür ebenfalls das Datum des Fensterstarts.
   */
  if (
    slot == MEDICATION_TIME_NIGHT &&
    minute < s_dayparts.morning
  ) {
    schedule_date.tm_mday -= 1;
    schedule_date.tm_isdst = -1;

    const time_t normalized =
        mktime(&schedule_date);

    if (normalized <= 0) {
      return false;
    }

    local_ptr = localtime(&normalized);

    if (!local_ptr) {
      return false;
    }

    schedule_date = *local_ptr;
  }

  return
      medication->time == (uint8_t)slot &&
      medication_is_scheduled_on_date(
        medication,
        &schedule_date
      );
}

bool medication_runtime_view(
    uint8_t medication_index,
    MedicationRuntimeView *view
) {
  if (
    !view ||
    medication_index >= s_medication_count
  ) {
    return false;
  }

  const MedicationSettings *settings =
      &s_medications[medication_index];

  *view = (MedicationRuntimeView) {
    .settings = settings,
    .appearance = {
      .valid = true,
      .shape =
          settings->shape <= 3
              ? settings->shape
              : 0,
      .primary_color = settings->color,
      .secondary_color = settings->color,
      .size = 100,
      .imprint = { 0 }
    }
  };

  if (
    medication_index <
        s_medication_appearance_count &&
    s_medication_appearances[
      medication_index
    ].valid
  ) {
    view->appearance =
        s_medication_appearances[
          medication_index
        ];
  }

  if (view->appearance.size < 60) {
    view->appearance.size = 60;
  } else if (view->appearance.size > 140) {
    view->appearance.size = 140;
  }

  return true;
}

MedicationTime current_medication_time(void) {
  const time_t now = time(NULL);
  struct tm *local_time = localtime(&now);

  if (!local_time) {
    return MEDICATION_TIME_MORNING;
  }

  return medication_time_for_minute(
    local_time->tm_hour * 60 +
    local_time->tm_min
  );
}

void reset_medication_confirmations(void) {
  alarm_refresh_window_state();
  s_pills_confirmed =
      (s_alarm_window_state.confirmed_mask &
       (1u << MEDICATION_SYMBOL_PILL)) != 0;
  s_pen_confirmed =
      (s_alarm_window_state.confirmed_mask &
       (1u << MEDICATION_SYMBOL_PEN)) != 0;
}

static bool medication_group_is_confirmed(
    MedicationSymbol symbol
) {
  return
      symbol == MEDICATION_SYMBOL_PILL
          ? s_pills_confirmed
          : s_pen_confirmed;
}

static bool medication_name_is_listed(
    const char *name
) {
  if (!name || name[0] == '\0') {
    return true;
  }

  for (
    uint8_t row_index = 0;
    row_index < s_list_row_count;
    row_index++
  ) {
    const int8_t medication_index =
        s_row_medication_indices[row_index];

    if (
      medication_index >= 0 &&
      medication_index < (int8_t)s_medication_count &&
      strcmp(
        s_medications[medication_index].name,
        name
      ) == 0
    ) {
      return true;
    }
  }

  return false;
}

void mark_medication_group_confirmed(
    MedicationSymbol symbol
) {
  if (symbol == MEDICATION_SYMBOL_PILL) {
    s_pills_confirmed = true;
    return;
  }

  s_pen_confirmed = true;
}

static bool medication_matches_group(
    const MedicationSettings *medication,
    time_t timestamp,
    MedicationSymbol symbol
) {
  return
      medication &&
      medication->symbol ==
          (uint8_t)symbol &&
      medication_is_due_at(
        medication,
        timestamp
      );
}

bool medication_group_is_due(
    MedicationSymbol symbol
) {
  if (medication_group_is_confirmed(symbol)) {
    return false;
  }

  const time_t now = time(NULL);

  for (
    uint8_t index = 0;
    index < s_medication_count;
    index++
  ) {
    if (
      medication_matches_group(
        &s_medications[index],
        now,
        symbol
      )
    ) {
      return true;
    }
  }

  return false;
}

bool active_medication_symbol(
    MedicationSymbol *symbol
) {
  if (
    medication_group_is_due(
      MEDICATION_SYMBOL_PILL
    )
  ) {
    if (symbol) {
      *symbol = MEDICATION_SYMBOL_PILL;
    }
    return true;
  }

  if (
    medication_group_is_due(
      MEDICATION_SYMBOL_PEN
    )
  ) {
    if (symbol) {
      *symbol = MEDICATION_SYMBOL_PEN;
    }
    return true;
  }

  return false;
}

bool unconfirmed_medication_group_is_due(void) {
  return active_medication_symbol(NULL);
}

void rebuild_medication_rows(void) {
  const time_t now = time(NULL);
  const MedicationTime visible_time =
      current_medication_time();

  s_visible_medication_time = visible_time;
  s_visible_medication_time_set = true;
  s_intake_row_count = 0;
  s_intake_symbol_set = false;

  memset(
    s_intake_medication_indices,
    -1,
    sizeof(s_intake_medication_indices)
  );

  MedicationSymbol symbol;

  if (!active_medication_symbol(&symbol)) {
    return;
  }

  s_intake_symbol = symbol;
  s_intake_symbol_set = true;

  for (
    uint8_t index = 0;
    index < s_medication_count &&
    s_intake_row_count < MAX_LIST_ROWS;
    index++
  ) {
    if (
      !medication_matches_group(
        &s_medications[index],
        now,
        symbol
      )
    ) {
      continue;
    }

    s_intake_medication_indices[
      s_intake_row_count
    ] = (int8_t)index;
    s_intake_row_count++;
  }
}

void rebuild_all_medication_rows(void) {
  s_visible_medication_time =
      current_medication_time();
  s_visible_medication_time_set = true;
  s_list_row_count = 0;

  memset(
    s_row_medication_indices,
    -1,
    sizeof(s_row_medication_indices)
  );

  for (
    uint8_t index = 0;
    index < s_medication_count;
    index++
  ) {
    const MedicationSettings *medication =
        &s_medications[index];

    /*
     * Derselbe Medikamentenname kann für mehrere
     * Einnahmezeiten konfiguriert sein. In der
     * Übersicht wird er trotzdem nur einmal gezeigt.
     */
    if (medication_name_is_listed(medication->name)) {
      continue;
    }

    s_row_medication_indices[s_list_row_count] =
        (int8_t)index;
    s_list_row_count++;
  }
}

void refresh_medication_rows_for_time(void) {
  const MedicationTime visible_time =
      current_medication_time();

  if (
    s_visible_medication_time_set &&
    visible_time == s_visible_medication_time
  ) {
    return;
  }

  refresh_app_screen_state();
}

void daypart_tick_handler(
    struct tm *tick_time,
    TimeUnits units_changed
) {
  (void)units_changed;

  refresh_medication_rows_for_time();
  alarm_handle_minute_tick(tick_time);
}
