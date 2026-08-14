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
    MedicationTime visible_time,
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
    MedicationTime visible_time,
    MedicationSymbol symbol
) {
  return
      medication &&
      medication->enabled &&
      medication->time ==
          (uint8_t)visible_time &&
      medication->symbol ==
          (uint8_t)symbol;
}

bool medication_group_is_due(
    MedicationSymbol symbol
) {
  if (medication_group_is_confirmed(symbol)) {
    return false;
  }

  const MedicationTime visible_time =
      current_medication_time();

  for (
    uint8_t index = 0;
    index < s_medication_count;
    index++
  ) {
    if (
      medication_matches_group(
        &s_medications[index],
        visible_time,
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
        visible_time,
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
