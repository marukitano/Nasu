#include <pebble.h>

#include <stdio.h>
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

#define ALARM_VISUAL_LEAD_IN_MS 160
#define ALARM_EVENT_COOKIE_BASE 0x4E530000u
#define ALARM_EVENT_COOKIE_PREFIX_MASK 0xFFFF0000u

static AppTimer *s_alarm_intro_timer;
static bool s_alarm_visuals_are_paused;

typedef struct {
  int32_t occurrence_start;
  int32_t last_reminder;
  uint8_t confirmed;
  uint8_t reserved[3];
} MedicationAlarmState;

typedef struct {
  time_t timestamp;
  uint16_t medication_mask;
} AlarmEvent;

static MedicationAlarmState
    s_medication_alarm_states[MAX_MEDICATIONS];
static bool s_medication_alarm_states_loaded;

static time_t s_alarm_stop_time;
static uint16_t s_alarm_due_medication_mask;
static uint16_t s_launch_wakeup_medication_mask;
static AppTimer *s_alarm_pulse_timer;
static AppTimer *s_alarm_audio_pump_timer;
static ResHandle s_alarm_audio_resource;
static size_t s_alarm_audio_resource_size;
static size_t s_alarm_audio_resource_offset;
static uint8_t s_alarm_audio_buffer[ALARM_AUDIO_BUFFER_SIZE];
static size_t s_alarm_audio_buffer_size;
static size_t s_alarm_audio_buffer_offset;
static bool s_alarm_audio_active;

static void alarm_release_visuals(void);
static void alarm_intro_timer_handler(void *context);
static void alarm_audio_finish_callback(
    SpeakerFinishReason reason,
    void *context
);
static void alarm_audio_reset_state(void);
static void alarm_audio_stop(void);
static bool alarm_audio_load_next_chunk(void);
static void alarm_audio_schedule_pump(void);
static void alarm_audio_pump(void *context);
static bool alarm_audio_start(void);
static bool alarm_window_bounds_at(
    time_t timestamp,
    time_t *window_start,
    time_t *window_end,
    MedicationTime *window_slot
);
static void medication_alarm_states_load(void);
static bool persist_medication_alarm_states(void);
static bool reset_medication_alarm_states(void);
static MedicationAlarmState *medication_alarm_state_for_occurrence(
    uint8_t medication_index,
    time_t occurrence_start
);
static bool medication_occurrence_at(
    uint8_t medication_index,
    time_t timestamp,
    time_t *occurrence_start,
    time_t *occurrence_end
);
static bool medication_alarm_state_at(
    uint8_t medication_index,
    time_t timestamp,
    time_t *occurrence_start,
    time_t *occurrence_end,
    MedicationAlarmState **state
);
static uint16_t alarm_event_medication_mask_at(
    time_t timestamp
);
static void alarm_event_merge(
    AlarmEvent candidate,
    AlarmEvent *best
);
static AlarmEvent next_scheduled_occurrence_after(
    time_t now
);
static AlarmEvent next_open_reminder_after(
    time_t now
);
static AlarmEvent next_alarm_event_after(
    time_t now
);
static int32_t alarm_event_cookie_encode(
    uint16_t medication_mask
);
static bool alarm_event_cookie_decode(
    int32_t cookie,
    uint16_t *medication_mask
);
static void record_alarm_reminder_at(
    time_t timestamp,
    uint16_t medication_mask
);
static void alarm_start_for_request(
    uint16_t requested_mask
);
static void alarm_vibrate(void);
static void alarm_pulse_timer_handler(void *context);
static void alarm_wakeup_handler(
    WakeupId wakeup_id,
    int32_t cookie
);

bool alarm_reminder_interval_valid(int value) {
  switch (value) {
    case 1:
    case 5:
    case 10:
    case 15:
    case 20:
    case 30:
    case 60:
      return true;

    default:
      return false;
  }
}

bool alarm_visuals_paused(void) {
  return s_alarm_visuals_are_paused;
}

static int32_t alarm_event_cookie_encode(
    uint16_t medication_mask
) {
  return
      (int32_t)(
        ALARM_EVENT_COOKIE_BASE |
        (uint32_t)medication_mask
      );
}

static bool alarm_event_cookie_decode(
    int32_t cookie,
    uint16_t *medication_mask
) {
  const uint32_t raw = (uint32_t)cookie;

  if (
    (
      raw &
      ALARM_EVENT_COOKIE_PREFIX_MASK
    ) != ALARM_EVENT_COOKIE_BASE
  ) {
    return false;
  }

  const uint16_t decoded =
      (uint16_t)(raw & 0xFFFFu);

  if (decoded == 0) {
    return false;
  }

  if (medication_mask) {
    *medication_mask = decoded;
  }

  return true;
}

uint16_t alarm_active_medication_mask(void) {
  return
      s_alarm_active
          ? s_alarm_due_medication_mask
          : 0;
}

static void alarm_release_visuals(void) {
  if (!s_alarm_visuals_are_paused) {
    return;
  }

  s_alarm_visuals_are_paused = false;

  /*
   * The static alert frame has already been drawn. From here on the normal
   * UI animation and pill rigid-body physics may resume.
   */
  mark_scene_dirty();
  pill_physics_update_activity();
}

void load_alarm_settings(void) {
  s_alarm_audio_volume = DEFAULT_ALARM_AUDIO_VOLUME;
  s_alarm_vibration_enabled =
      DEFAULT_ALARM_VIBRATION_ENABLED;
  s_alarm_reminder_interval_minutes =
      DEFAULT_ALARM_REMINDER_INTERVAL_MINUTES;

  if (persist_exists(ALARM_AUDIO_VOLUME_PERSIST_KEY)) {
    const int stored_volume = persist_read_int(
      ALARM_AUDIO_VOLUME_PERSIST_KEY
    );

    if (stored_volume >= 0 && stored_volume <= 100) {
      s_alarm_audio_volume = (uint8_t)stored_volume;
    }
  }

  if (persist_exists(ALARM_VIBRATION_PERSIST_KEY)) {
    s_alarm_vibration_enabled =
        persist_read_int(ALARM_VIBRATION_PERSIST_KEY) != 0;
  }

  if (persist_exists(ALARM_INTERVAL_PERSIST_KEY)) {
    const int stored_interval = persist_read_int(
      ALARM_INTERVAL_PERSIST_KEY
    );

    if (alarm_reminder_interval_valid(stored_interval)) {
      s_alarm_reminder_interval_minutes =
          (uint8_t)stored_interval;
    }
  }
}

void apply_alarm_settings(
    uint8_t volume,
    bool vibration_enabled,
    uint8_t reminder_interval_minutes,
    bool save
) {
  if (!alarm_reminder_interval_valid(reminder_interval_minutes)) {
    reminder_interval_minutes =
        DEFAULT_ALARM_REMINDER_INTERVAL_MINUTES;
  }

  s_alarm_audio_volume = volume;
  s_alarm_vibration_enabled = vibration_enabled;
  s_alarm_reminder_interval_minutes =
      reminder_interval_minutes;

  if (save) {
    persist_write_int(
      ALARM_AUDIO_VOLUME_PERSIST_KEY,
      s_alarm_audio_volume
    );
    persist_write_int(
      ALARM_VIBRATION_PERSIST_KEY,
      s_alarm_vibration_enabled ? 1 : 0
    );
    persist_write_int(
      ALARM_INTERVAL_PERSIST_KEY,
      s_alarm_reminder_interval_minutes
    );
  }

  if (!s_alarm_vibration_enabled) {
    vibes_cancel();
  }

  if (s_alarm_active) {
    if (
      s_alarm_audio_volume == 0 ||
      speaker_is_muted()
    ) {
      if (s_alarm_audio_active) {
        alarm_audio_stop();
        alarm_release_visuals();
      }
    } else if (s_alarm_audio_active) {
      speaker_set_volume(s_alarm_audio_volume);
    }

    /*
     * Never start a fresh sound merely because settings changed while an
     * alarm is already active. The acoustic alert is strictly one-shot.
     */
  }
}

static void alarm_audio_reset_state(void) {
  s_alarm_audio_resource = NULL;
  s_alarm_audio_resource_size = 0;
  s_alarm_audio_resource_offset = 0;
  s_alarm_audio_buffer_size = 0;
  s_alarm_audio_buffer_offset = 0;
  s_alarm_audio_active = false;
}

static void alarm_audio_stop(void) {
  cancel_timer(&s_alarm_audio_pump_timer);

  const bool was_active = s_alarm_audio_active;

  /*
   * Clear this first so a finish callback caused by speaker_stop() cannot
   * accidentally release an intro that is being cancelled.
   */
  s_alarm_audio_active = false;

  if (was_active) {
    speaker_stop();
  }

  alarm_audio_reset_state();
}

static void alarm_audio_finish_callback(
    SpeakerFinishReason reason,
    void *context
) {
  (void)reason;
  (void)context;

  if (!s_alarm_audio_active) {
    return;
  }

  cancel_timer(&s_alarm_audio_pump_timer);
  alarm_audio_reset_state();

  /* The speaker has really drained its PCM buffer. Animation may start. */
  alarm_release_visuals();
}

static bool alarm_audio_load_next_chunk(void) {
  if (
    s_alarm_audio_resource == NULL ||
    s_alarm_audio_resource_size == 0
  ) {
    return false;
  }

  /* EOF is handled by alarm_audio_pump(), which drains the stream. */
  if (
    s_alarm_audio_resource_offset >=
        s_alarm_audio_resource_size
  ) {
    return false;
  }

  const size_t bytes_remaining =
      s_alarm_audio_resource_size -
      s_alarm_audio_resource_offset;

  const size_t bytes_requested =
      bytes_remaining < sizeof(s_alarm_audio_buffer)
          ? bytes_remaining
          : sizeof(s_alarm_audio_buffer);

  const size_t bytes_loaded =
      resource_load_byte_range(
        s_alarm_audio_resource,
        (uint32_t)s_alarm_audio_resource_offset,
        s_alarm_audio_buffer,
        bytes_requested
      );

  if (bytes_loaded == 0) {
    return false;
  }

  s_alarm_audio_buffer_size = bytes_loaded;
  s_alarm_audio_buffer_offset = 0;
  return true;
}

static void alarm_audio_schedule_pump(void) {
  if (
    !s_alarm_audio_active ||
    s_alarm_audio_pump_timer != NULL
  ) {
    return;
  }

  s_alarm_audio_pump_timer = app_timer_register(
    ALARM_AUDIO_PUMP_INTERVAL_MS,
    alarm_audio_pump,
    NULL
  );

  if (!s_alarm_audio_pump_timer) {
    alarm_audio_stop();
    alarm_release_visuals();
  }
}

static void alarm_audio_pump(void *context) {
  (void)context;
  s_alarm_audio_pump_timer = NULL;

  if (!s_alarm_audio_active) {
    return;
  }

  for (
    uint8_t attempt = 0;
    attempt < ALARM_AUDIO_MAX_WRITES_PER_PUMP;
    attempt++
  ) {
    if (
      s_alarm_audio_buffer_offset >=
          s_alarm_audio_buffer_size
    ) {
      if (
        s_alarm_audio_resource_offset >=
            s_alarm_audio_resource_size
      ) {
        /*
         * Do not call speaker_stop() at EOF: that cuts buffered samples.
         * close() drains the hardware buffer and the finish callback tells
         * us when the acoustic intro has actually ended.
         */
        speaker_stream_close();
        return;
      }

      if (!alarm_audio_load_next_chunk()) {
        alarm_audio_stop();
        alarm_release_visuals();
        return;
      }
    }

    const size_t bytes_available =
        s_alarm_audio_buffer_size -
        s_alarm_audio_buffer_offset;

    const uint32_t bytes_written =
        speaker_stream_write(
          s_alarm_audio_buffer +
              s_alarm_audio_buffer_offset,
          (uint32_t)bytes_available
        );

    if (bytes_written == 0) {
      break;
    }

    s_alarm_audio_buffer_offset += bytes_written;

    if (
      s_alarm_audio_buffer_offset >=
          s_alarm_audio_buffer_size
    ) {
      s_alarm_audio_resource_offset +=
          s_alarm_audio_buffer_size;
      s_alarm_audio_buffer_size = 0;
      s_alarm_audio_buffer_offset = 0;

    }
  }

  alarm_audio_schedule_pump();
}

static bool alarm_audio_start(void) {
  alarm_audio_stop();

  if (
    s_alarm_audio_volume == 0 ||
    speaker_is_muted()
  ) {
    return false;
  }

  s_alarm_audio_resource =
      resource_get_handle(RESOURCE_ID_ALARM_PCM);
  s_alarm_audio_resource_size =
      resource_size(s_alarm_audio_resource);

  if (
    s_alarm_audio_resource == NULL ||
    s_alarm_audio_resource_size == 0
  ) {
    alarm_audio_reset_state();
    return false;
  }

  if (
    !speaker_stream_open(
      SpeakerPcmFormat_16kHz_8bit,
      s_alarm_audio_volume
    )
  ) {
    alarm_audio_reset_state();
    return false;
  }

  s_alarm_audio_active = true;
  alarm_audio_pump(NULL);
  return s_alarm_audio_active;
}

static bool alarm_window_bounds_at(
    time_t timestamp,
    time_t *window_start,
    time_t *window_end,
    MedicationTime *window_slot
) {
  struct tm *local_ptr = localtime(&timestamp);

  if (!local_ptr) {
    return false;
  }

  const struct tm local = *local_ptr;
  const int minute = local.tm_hour * 60 + local.tm_min;

  MedicationTime slot;
  int start_day_offset = 0;
  int end_day_offset = 0;
  uint16_t start_minute;
  uint16_t end_minute;

  if (
    minute >= s_dayparts.morning &&
    minute < s_dayparts.noon
  ) {
    slot = MEDICATION_TIME_MORNING;
    start_minute = s_dayparts.morning;
    end_minute = s_dayparts.noon;
  } else if (
    minute >= s_dayparts.noon &&
    minute < s_dayparts.evening
  ) {
    slot = MEDICATION_TIME_NOON;
    start_minute = s_dayparts.noon;
    end_minute = s_dayparts.evening;
  } else if (
    minute >= s_dayparts.evening &&
    minute < s_dayparts.night
  ) {
    slot = MEDICATION_TIME_EVENING;
    start_minute = s_dayparts.evening;
    end_minute = s_dayparts.night;
  } else if (minute >= s_dayparts.night) {
    slot = MEDICATION_TIME_NIGHT;
    start_minute = s_dayparts.night;
    end_minute = s_dayparts.morning;
    end_day_offset = 1;
  } else {
    slot = MEDICATION_TIME_NIGHT;
    start_minute = s_dayparts.night;
    end_minute = s_dayparts.morning;
    start_day_offset = -1;
  }

  struct tm start_tm = local;
  start_tm.tm_mday += start_day_offset;
  start_tm.tm_hour = start_minute / 60;
  start_tm.tm_min = start_minute % 60;
  start_tm.tm_sec = 0;
  start_tm.tm_isdst = -1;

  struct tm end_tm = local;
  end_tm.tm_mday += end_day_offset;
  end_tm.tm_hour = end_minute / 60;
  end_tm.tm_min = end_minute % 60;
  end_tm.tm_sec = 0;
  end_tm.tm_isdst = -1;

  const time_t start = mktime(&start_tm);
  const time_t end = mktime(&end_tm);

  if (start <= 0 || end <= start) {
    return false;
  }

  if (window_start) {
    *window_start = start;
  }
  if (window_end) {
    *window_end = end;
  }
  if (window_slot) {
    *window_slot = slot;
  }

  return true;
}

static void medication_alarm_states_load(void) {
  if (s_medication_alarm_states_loaded) {
    return;
  }

  memset(
    s_medication_alarm_states,
    0,
    sizeof(s_medication_alarm_states)
  );

  const int state_bytes =
      (int)sizeof(s_medication_alarm_states);

  if (
    persist_exists(
      MEDICATION_ALARM_STATE_PERSIST_KEY
    ) &&
    persist_get_size(
      MEDICATION_ALARM_STATE_PERSIST_KEY
    ) == state_bytes &&
    persist_read_data(
      MEDICATION_ALARM_STATE_PERSIST_KEY,
      s_medication_alarm_states,
      sizeof(s_medication_alarm_states)
    ) == state_bytes
  ) {
    s_medication_alarm_states_loaded = true;
    return;
  }

  /*
   * One-time migration from the split interval/regular state stores used
   * before the alarm core had one state per medication.
   *
   * Persist key 208 previously contained AlarmWindowState. Its old blob is
   * much smaller than this array, so the size check above safely ignores it.
   */
  MedicationAlarmState interval_states[
    MAX_MEDICATIONS
  ];
  MedicationAlarmState regular_states[
    MAX_MEDICATIONS
  ];

  memset(
    interval_states,
    0,
    sizeof(interval_states)
  );
  memset(
    regular_states,
    0,
    sizeof(regular_states)
  );

  const bool interval_loaded =
      persist_exists(
        INTERVAL_ALARM_STATE_PERSIST_KEY
      ) &&
      persist_get_size(
        INTERVAL_ALARM_STATE_PERSIST_KEY
      ) == (int)sizeof(interval_states) &&
      persist_read_data(
        INTERVAL_ALARM_STATE_PERSIST_KEY,
        interval_states,
        sizeof(interval_states)
      ) == (int)sizeof(interval_states);

  const bool regular_loaded =
      persist_exists(
        REGULAR_ALARM_STATE_PERSIST_KEY
      ) &&
      persist_get_size(
        REGULAR_ALARM_STATE_PERSIST_KEY
      ) == (int)sizeof(regular_states) &&
      persist_read_data(
        REGULAR_ALARM_STATE_PERSIST_KEY,
        regular_states,
        sizeof(regular_states)
      ) == (int)sizeof(regular_states);

  for (
    uint8_t index = 0;
    index < s_medication_count;
    index++
  ) {
    if (
      s_medications[index].time ==
          MEDICATION_TIME_INTERVAL
    ) {
      if (interval_loaded) {
        s_medication_alarm_states[index] =
            interval_states[index];
      }
    } else if (regular_loaded) {
      s_medication_alarm_states[index] =
          regular_states[index];
    }
  }

  s_medication_alarm_states_loaded = true;

  if (interval_loaded || regular_loaded) {
    (void)persist_medication_alarm_states();
  }
}

static bool persist_medication_alarm_states(void) {
  medication_alarm_states_load();

  return
      persist_write_data(
        MEDICATION_ALARM_STATE_PERSIST_KEY,
        s_medication_alarm_states,
        sizeof(s_medication_alarm_states)
      ) == (int)sizeof(s_medication_alarm_states);
}

static bool reset_medication_alarm_states(void) {
  memset(
    s_medication_alarm_states,
    0,
    sizeof(s_medication_alarm_states)
  );
  s_medication_alarm_states_loaded = true;

  if (!persist_medication_alarm_states()) {
    return false;
  }

  MedicationAlarmState verified[
    MAX_MEDICATIONS
  ];
  memset(verified, 0, sizeof(verified));

  if (
    persist_read_data(
      MEDICATION_ALARM_STATE_PERSIST_KEY,
      verified,
      sizeof(verified)
    ) != (int)sizeof(verified)
  ) {
    return false;
  }

  return
      memcmp(
        verified,
        s_medication_alarm_states,
        sizeof(verified)
      ) == 0;
}

static MedicationAlarmState *medication_alarm_state_for_occurrence(
    uint8_t medication_index,
    time_t occurrence_start
) {
  if (
    medication_index >= s_medication_count ||
    occurrence_start <= 0
  ) {
    return NULL;
  }

  medication_alarm_states_load();

  MedicationAlarmState *current =
      &s_medication_alarm_states[
        medication_index
      ];

  bool changed = false;

  if (
    current->occurrence_start !=
        (int32_t)occurrence_start
  ) {
    *current = (MedicationAlarmState) {
      .occurrence_start =
          (int32_t)occurrence_start,
      .last_reminder = 0,
      .confirmed = 0,
      .reserved = { 0, 0, 0 }
    };
    changed = true;
  } else if (current->confirmed > 1) {
    current->confirmed = 0;
    changed = true;
  }

  if (changed) {
    (void)persist_medication_alarm_states();
  }

  return current;
}

static bool regular_alarm_timestamp_for_window(
    uint8_t medication_index,
    time_t window_start,
    MedicationTime slot,
    time_t *alarm_timestamp
) {
  if (medication_index >= s_medication_count) {
    return false;
  }

  const MedicationSettings *medication =
      &s_medications[medication_index];

  if (
    !medication->enabled ||
    medication->time ==
        MEDICATION_TIME_INTERVAL ||
    medication->time != (uint8_t)slot
  ) {
    return false;
  }

  struct tm *local_ptr = localtime(
    &window_start
  );

  if (!local_ptr) {
    return false;
  }

  const struct tm schedule_date = *local_ptr;

  if (
    !medication_is_scheduled_on_date(
      medication,
      &schedule_date
    )
  ) {
    return false;
  }

  const uint16_t minute =
      medication_alarm_minute_at(
        medication_index
      );

  if (minute >= DAYPART_MINUTES_PER_DAY) {
    return false;
  }

  const uint16_t window_start_minute =
      (uint16_t)(
        schedule_date.tm_hour * 60 +
        schedule_date.tm_min
      );

  struct tm alarm_tm = schedule_date;

  /*
   * Night can cross midnight. Example:
   * night starts 23:59, Pen effective minute is 00:01.
   */
  if (minute < window_start_minute) {
    alarm_tm.tm_mday += 1;
  }

  alarm_tm.tm_hour = minute / 60;
  alarm_tm.tm_min = minute % 60;
  alarm_tm.tm_sec = 0;
  alarm_tm.tm_isdst = -1;

  const time_t candidate =
      mktime(&alarm_tm);

  if (candidate <= 0) {
    return false;
  }

  if (alarm_timestamp) {
    *alarm_timestamp = candidate;
  }

  return true;
}

static bool medication_occurrence_at(
    uint8_t medication_index,
    time_t timestamp,
    time_t *occurrence_start,
    time_t *occurrence_end
) {
  if (medication_index >= s_medication_count) {
    return false;
  }

  time_t resolved_start = 0;
  time_t resolved_end = 0;

  if (
    s_medications[medication_index].time ==
        MEDICATION_TIME_INTERVAL
  ) {
    if (
      !medication_interval_window_at(
        medication_index,
        timestamp,
        &resolved_start,
        &resolved_end
      )
    ) {
      return false;
    }
  } else {
    time_t window_start = 0;
    MedicationTime slot;
    time_t alarm_time = 0;

    if (
      !alarm_window_bounds_at(
        timestamp,
        &window_start,
        &resolved_end,
        &slot
      ) ||
      !regular_alarm_timestamp_for_window(
        medication_index,
        window_start,
        slot,
        &alarm_time
      ) ||
      timestamp < alarm_time ||
      alarm_time >= resolved_end
    ) {
      return false;
    }

    resolved_start = alarm_time;
  }

  if (occurrence_start) {
    *occurrence_start = resolved_start;
  }
  if (occurrence_end) {
    *occurrence_end = resolved_end;
  }

  return true;
}

static bool medication_alarm_state_at(
    uint8_t medication_index,
    time_t timestamp,
    time_t *occurrence_start,
    time_t *occurrence_end,
    MedicationAlarmState **state
) {
  time_t resolved_start = 0;
  time_t resolved_end = 0;

  if (
    !medication_occurrence_at(
      medication_index,
      timestamp,
      &resolved_start,
      &resolved_end
    )
  ) {
    return false;
  }

  MedicationAlarmState *current =
      medication_alarm_state_for_occurrence(
        medication_index,
        resolved_start
      );

  if (!current) {
    return false;
  }

  if (occurrence_start) {
    *occurrence_start = resolved_start;
  }
  if (occurrence_end) {
    *occurrence_end = resolved_end;
  }
  if (state) {
    *state = current;
  }

  return true;
}

static uint16_t alarm_unconfirmed_medication_mask_at(
    time_t timestamp
) {
  uint16_t mask = 0;

  for (
    uint8_t index = 0;
    index < s_medication_count;
    index++
  ) {
    if (
      alarm_medication_is_unconfirmed_due_at(
        index,
        timestamp
      )
    ) {
      mask |= (uint16_t)(1u << index);
    }
  }

  return mask;
}

static time_t reminder_due_timestamp_for_state(
    time_t occurrence_start,
    time_t occurrence_end,
    const MedicationAlarmState *state
) {
  if (
    !state ||
    state->confirmed ||
    occurrence_start <= 0 ||
    occurrence_end <= occurrence_start
  ) {
    return 0;
  }

  const time_t due =
      state->last_reminder <
          (int32_t)occurrence_start
          ? occurrence_start
          : (time_t)state->last_reminder +
              (time_t)s_alarm_reminder_interval_minutes *
                  60;

  return due < occurrence_end ? due : 0;
}

static uint16_t alarm_event_medication_mask_at(
    time_t timestamp
) {
  uint16_t mask = 0;

  for (
    uint8_t index = 0;
    index < s_medication_count;
    index++
  ) {
    time_t occurrence_start = 0;
    time_t occurrence_end = 0;
    MedicationAlarmState *state = NULL;

    if (
      !medication_alarm_state_at(
        index,
        timestamp,
        &occurrence_start,
        &occurrence_end,
        &state
      ) ||
      !state
    ) {
      continue;
    }

    const time_t due =
        reminder_due_timestamp_for_state(
          occurrence_start,
          occurrence_end,
          state
        );

    if (
      due > 0 &&
      timestamp >= due &&
      timestamp < occurrence_end
    ) {
      mask |= (uint16_t)(1u << index);
    }
  }

  return mask;
}

bool alarm_medication_is_unconfirmed_due_at(
    uint8_t medication_index,
    time_t timestamp
) {
  MedicationAlarmState *state = NULL;

  return
      medication_alarm_state_at(
        medication_index,
        timestamp,
        NULL,
        NULL,
        &state
      ) &&
      state &&
      !state->confirmed;
}

uint8_t alarm_unconfirmed_symbol_mask_at(
    time_t timestamp
) {
  const uint16_t medication_mask =
      alarm_unconfirmed_medication_mask_at(
        timestamp
      );
  uint8_t symbol_mask = 0;

  for (
    uint8_t index = 0;
    index < s_medication_count;
    index++
  ) {
    if (
      (
        medication_mask &
        (uint16_t)(1u << index)
      ) == 0
    ) {
      continue;
    }

    symbol_mask |=
        (uint8_t)(
          1u << s_medications[index].symbol
        );
  }

  return symbol_mask;
}

bool alarm_intake_navigation_lock_required(void) {
  /*
   * "Noch offen" und "jetzt alarmierend" sind zwei verschiedene Dinge.
   *
   * Ein früheres, unquittiertes Ereignis darf nach einem späteren Alarm
   * nicht sofort wieder in die Intake-UI springen. Zwischen zwei echten
   * Reminder-Ereignissen bleibt es lediglich als unbestätigter State
   * gespeichert.
   *
   * Während eines aktiven Alarmereignisses bleibt die Navigation wie
   * bisher verriegelt.
   */
  return
      s_alarm_active &&
      s_alarm_due_medication_mask != 0;
}

static void alarm_event_merge(
    AlarmEvent candidate,
    AlarmEvent *best
) {
  if (
    !best ||
    candidate.timestamp <= 0 ||
    candidate.medication_mask == 0
  ) {
    return;
  }

  if (
    best->timestamp == 0 ||
    candidate.timestamp < best->timestamp
  ) {
    *best = candidate;
  } else if (
    candidate.timestamp == best->timestamp
  ) {
    best->medication_mask |=
        candidate.medication_mask;
  }
}

static AlarmEvent next_scheduled_occurrence_after(
    time_t now
) {
  AlarmEvent best = { 0 };
  struct tm *today_ptr = localtime(&now);
  const bool today_available = today_ptr != NULL;
  struct tm today = { 0 };

  if (today_available) {
    today = *today_ptr;
  }

  const uint16_t starts[] = {
    s_dayparts.morning,
    s_dayparts.noon,
    s_dayparts.evening,
    s_dayparts.night
  };

  for (
    uint8_t index = 0;
    index < s_medication_count;
    index++
  ) {
    const MedicationSettings *medication =
        &s_medications[index];

    if (!medication->enabled) {
      continue;
    }

    const uint16_t bit =
        (uint16_t)(1u << index);

    if (
      medication->time ==
          MEDICATION_TIME_INTERVAL
    ) {
      time_t occurrence_start = 0;
      time_t occurrence_end = 0;

      /*
       * The fixed next occurrence belongs to the schedule itself.
       * It must never depend on confirmation/reminder state.
       */
      if (
        medication_interval_window_at(
          index,
          now,
          &occurrence_start,
          &occurrence_end
        ) &&
        occurrence_end > now
      ) {
        alarm_event_merge(
          (AlarmEvent) {
            .timestamp = occurrence_end,
            .medication_mask = bit
          },
          &best
        );
      }

      continue;
    }

    if (!today_available) {
      continue;
    }

    for (
      int16_t day_offset = -1;
      day_offset <= 370;
      day_offset++
    ) {
      struct tm window_tm = today;
      const uint16_t start_minute =
          starts[medication->time];

      window_tm.tm_mday += day_offset;
      window_tm.tm_hour = start_minute / 60;
      window_tm.tm_min = start_minute % 60;
      window_tm.tm_sec = 0;
      window_tm.tm_isdst = -1;

      const time_t window_start =
          mktime(&window_tm);

      if (window_start <= 0) {
        continue;
      }

      time_t normalized_start = 0;
      time_t window_end = 0;
      MedicationTime slot;

      if (
        !alarm_window_bounds_at(
          window_start,
          &normalized_start,
          &window_end,
          &slot
        ) ||
        normalized_start != window_start ||
        slot !=
            (MedicationTime)medication->time
      ) {
        continue;
      }

      time_t alarm_time = 0;

      if (
        !regular_alarm_timestamp_for_window(
          index,
          window_start,
          slot,
          &alarm_time
        ) ||
        alarm_time <= now ||
        alarm_time >= window_end
      ) {
        continue;
      }

      alarm_event_merge(
        (AlarmEvent) {
          .timestamp = alarm_time,
          .medication_mask = bit
        },
        &best
      );

      break;
    }
  }

  return best;
}

static time_t next_reminder_timestamp_for_state(
    time_t now,
    time_t occurrence_start,
    time_t occurrence_end,
    const MedicationAlarmState *state
) {
  time_t candidate =
      reminder_due_timestamp_for_state(
        occurrence_start,
        occurrence_end,
        state
      );

  if (candidate <= 0) {
    return 0;
  }

  if (candidate <= now) {
    candidate =
        ((now / 60) + 1) * 60;
  }

  return
      candidate > now &&
      candidate < occurrence_end
          ? candidate
          : 0;
}

static AlarmEvent next_open_reminder_after(
    time_t now
) {
  AlarmEvent best = { 0 };

  for (
    uint8_t index = 0;
    index < s_medication_count;
    index++
  ) {
    time_t occurrence_start = 0;
    time_t occurrence_end = 0;
    MedicationAlarmState *state = NULL;

    if (
      !medication_alarm_state_at(
        index,
        now,
        &occurrence_start,
        &occurrence_end,
        &state
      ) ||
      !state
    ) {
      continue;
    }

    const time_t candidate =
        next_reminder_timestamp_for_state(
          now,
          occurrence_start,
          occurrence_end,
          state
        );

    if (candidate <= 0) {
      continue;
    }

    alarm_event_merge(
      (AlarmEvent) {
        .timestamp = candidate,
        .medication_mask =
            (uint16_t)(1u << index)
      },
      &best
    );
  }

  return best;
}

static AlarmEvent next_alarm_event_after(
    time_t now
) {
  AlarmEvent best =
      next_scheduled_occurrence_after(now);

  alarm_event_merge(
    next_open_reminder_after(now),
    &best
  );

  return best;
}

time_t alarm_next_timestamp(void) {
  return
      next_alarm_event_after(
        time(NULL)
      ).timestamp;
}

void schedule_next_alarm_wakeup(void) {
  wakeup_cancel_all();

  const time_t now = time(NULL);
  const AlarmEvent event =
      next_alarm_event_after(now);

  if (
    event.timestamp <= now ||
    event.medication_mask == 0
  ) {
    return;
  }

  const int32_t cookie =
      alarm_event_cookie_encode(
        event.medication_mask
      );

  WakeupId result = E_INTERNAL;
  time_t scheduled = event.timestamp;

  for (
    uint8_t attempt = 0;
    attempt <= 120;
    attempt++, scheduled++
  ) {
    result = wakeup_schedule(
      scheduled,
      cookie,
      true
    );

    if (result != E_RANGE) {
      break;
    }
  }

  if (result < 0) {
    APP_LOG(
      APP_LOG_LEVEL_WARNING,
      "Medication wakeup failed: %ld mask=0x%04x",
      (long)result,
      (unsigned int)event.medication_mask
    );
  } else {
    APP_LOG(
      APP_LOG_LEVEL_INFO,
      "Medication wakeup in %ld sec at=%ld mask=0x%04x",
      (long)(scheduled - now),
      (long)scheduled,
      (unsigned int)event.medication_mask
    );
  }
}

static void alarm_vibrate(void) {
  if (s_alarm_vibration_enabled) {
    vibes_double_pulse();
  }
}

void alarm_stop(void) {
  cancel_timer(&s_alarm_intro_timer);
  cancel_timer(&s_alarm_pulse_timer);
  vibes_cancel();

  s_alarm_visuals_are_paused = false;
  alarm_audio_stop();

  s_alarm_active = false;
  s_alarm_stop_time = 0;
  s_alarm_due_medication_mask = 0;
}

static void alarm_intro_timer_handler(void *context) {
  (void)context;
  s_alarm_intro_timer = NULL;

  if (!s_alarm_active) {
    s_alarm_visuals_are_paused = false;
    return;
  }

  /*
   * The event loop has had time to paint the pattern and the first static
   * medication frame. Start haptics now and keep only haptics repeating.
   */
  alarm_pulse_timer_handler(NULL);

  if (!s_alarm_active) {
    s_alarm_visuals_are_paused = false;
    return;
  }

  /*
   * Exactly one acoustic playback belongs to this reminder event.
   * The repeating vibration pulse never restarts it. A later scheduled
   * reminder creates a new alarm event and may therefore play once again.
   */
  if (!alarm_audio_start()) {
    alarm_release_visuals();
  }
}

static void alarm_pulse_timer_handler(void *context) {
  (void)context;
  s_alarm_pulse_timer = NULL;

  if (!s_alarm_active) {
    return;
  }

  const time_t now = time(NULL);

  if (now >= s_alarm_stop_time) {
    alarm_stop();

    /*
     * The next medication wakeup was already scheduled at alarm start.
     * Exiting Nasu does not cancel that OS wakeup.
     */
    app_exit_to_watchface();
    return;
  }

  /* Vibration may repeat; acoustic playback never does. */
  alarm_vibrate();

  s_alarm_pulse_timer = app_timer_register(
    ALARM_VIBE_INTERVAL_MS,
    alarm_pulse_timer_handler,
    NULL
  );

  if (!s_alarm_pulse_timer) {
    alarm_stop();
  }
}

static void record_alarm_reminder_at(
    time_t timestamp,
    uint16_t medication_mask
) {
  const int32_t minute_timestamp =
      (int32_t)(
        timestamp - (timestamp % 60)
      );

  bool state_changed = false;

  for (
    uint8_t index = 0;
    index < s_medication_count;
    index++
  ) {
    if (
      (
        medication_mask &
        (uint16_t)(1u << index)
      ) == 0
    ) {
      continue;
    }

    MedicationAlarmState *state = NULL;

    if (
      !medication_alarm_state_at(
        index,
        timestamp,
        NULL,
        NULL,
        &state
      ) ||
      !state ||
      state->confirmed
    ) {
      continue;
    }

    state->last_reminder =
        minute_timestamp;
    state_changed = true;
  }

  if (state_changed) {
    (void)persist_medication_alarm_states();
  }
}

static void alarm_start_due_event_at(
    time_t now,
    uint16_t due_medication_mask,
    uint16_t requested_mask
) {
  if (due_medication_mask == 0) {
    refresh_app_screen_state();
    schedule_next_alarm_wakeup();
    return;
  }

  s_alarm_due_medication_mask =
      due_medication_mask;

  APP_LOG(
    APP_LOG_LEVEL_INFO,
    "Alarm start requested=0x%04x active=0x%04x",
    (unsigned int)requested_mask,
    (unsigned int)due_medication_mask
  );

  record_alarm_reminder_at(
    now,
    due_medication_mask
  );

  s_alarm_active = true;
  s_alarm_stop_time =
      now + ALARM_ACTIVE_SECONDS;

  s_alarm_visuals_are_paused = true;
  refresh_app_screen_state();
  medication_ui_begin_alarm_sequence();

  cancel_timer(&s_alarm_intro_timer);
  s_alarm_intro_timer = app_timer_register(
    ALARM_VISUAL_LEAD_IN_MS,
    alarm_intro_timer_handler,
    NULL
  );

  if (!s_alarm_intro_timer) {
    alarm_intro_timer_handler(NULL);
  }
}

static void alarm_start_for_request(
    uint16_t requested_mask
) {
  if (s_alarm_active) {
    return;
  }

  if (s_transfer_screen_active) {
    schedule_next_alarm_wakeup();
    return;
  }

  const time_t now = time(NULL);
  uint16_t due_medication_mask =
      alarm_event_medication_mask_at(now);

  /*
   * A wakeup cookie is only a snapshot of which medications caused the
   * scheduled OS event. It may restrict the live event, but it must never
   * define "due" independently from the alarm core.
   */
  if (requested_mask != 0) {
    due_medication_mask &= requested_mask;
  }

  alarm_start_due_event_at(
    now,
    due_medication_mask,
    requested_mask
  );
}

void alarm_start(void) {
  if (
    s_alarm_active ||
    s_transfer_screen_active
  ) {
    if (s_transfer_screen_active) {
      schedule_next_alarm_wakeup();
    }
    return;
  }

  const uint16_t requested_mask =
      s_launch_wakeup_medication_mask;
  s_launch_wakeup_medication_mask = 0;

  alarm_start_for_request(requested_mask);
}

static void alarm_wakeup_handler(
    WakeupId wakeup_id,
    int32_t cookie
) {
  (void)wakeup_id;

  uint16_t medication_mask = 0;

  if (
    !alarm_event_cookie_decode(
      cookie,
      &medication_mask
    )
  ) {
    if (cookie != ALARM_WAKEUP_COOKIE) {
      return;
    }
  }

  refresh_medication_rows_for_time();
  alarm_start_for_request(medication_mask);
  schedule_next_alarm_wakeup();
}



void alarm_handle_minute_tick(
    const struct tm *tick_time
) {
  if (!tick_time) {
    return;
  }

  if (
    s_alarm_active ||
    s_transfer_screen_active
  ) {
    schedule_next_alarm_wakeup();
    return;
  }

  /*
   * Nasu is already open, so the minute tick is the foreground scheduler.
   * Calculate the live event exactly once and start that same event.
   */
  const time_t now = time(NULL);
  const uint16_t due_medication_mask =
      alarm_event_medication_mask_at(now);

  if (due_medication_mask != 0) {
    alarm_start_due_event_at(
      now,
      due_medication_mask,
      0
    );
  }

  schedule_next_alarm_wakeup();
}

bool alarm_reset_after_settings_save(void) {
  alarm_stop();
  wakeup_cancel_all();

  const time_t now = time(NULL);
  const time_t current_minute =
      now - (now % 60);

  if (
    !alarm_window_bounds_at(
      now,
      NULL,
      NULL,
      NULL
    )
  ) {
    APP_LOG(
      APP_LOG_LEVEL_ERROR,
      "Settings saved, but alarm window reset failed"
    );
    return false;
  }

  const bool state_reset_verified =
      reset_medication_alarm_states();

  /*
   * Saving settings must not resurrect an occurrence whose start minute
   * already lies in the past.
   *
   * Example:
   *   interval every 2 h, start 08:00
   *   settings saved at 11:29
   *
   * The 10:00 occurrence is historical. Mark that current historical
   * occurrence as consumed; the next 12:00 occurrence will create a fresh
   * unconfirmed state automatically.
   *
   * If an alarm time is in the CURRENT minute, it is deliberately left
   * unconfirmed so a just-saved alarm can still fire.
   */
  bool seed_verified = true;

  if (state_reset_verified) {
    for (
      uint8_t index = 0;
      index < s_medication_count;
      index++
    ) {
      if (!s_medications[index].enabled) {
        continue;
      }

      time_t occurrence_start = 0;

      if (
        medication_occurrence_at(
          index,
          now,
          &occurrence_start,
          NULL
        ) &&
        occurrence_start < current_minute
      ) {
        s_medication_alarm_states[index] =
            (MedicationAlarmState) {
              .occurrence_start =
                  (int32_t)occurrence_start,
              .last_reminder = 0,
              .confirmed = 1,
              .reserved = { 0, 0, 0 }
            };
      }
    }

    seed_verified =
        persist_medication_alarm_states();
  }

  const bool reset_verified =
      state_reset_verified &&
      seed_verified;

  if (!reset_verified) {
    APP_LOG(
      APP_LOG_LEVEL_ERROR,
      "Settings saved, but intake reset could not be verified"
    );
    return false;
  }

  if (!s_transfer_screen_active) {
    refresh_app_screen_state();
  }

  schedule_next_alarm_wakeup();

  APP_LOG(
    APP_LOG_LEVEL_INFO,
    "Settings saved: intake state reset and verified"
  );
  return true;
}

void alarm_confirmation_received(
    MedicationSymbol symbol
) {
  const time_t now = time(NULL);

  /*
   * Confirm every currently due medication of the selected symbol.
   * The active alarm mask describes only the reminder event that opened
   * the alarm UI; it must not hide another already due occurrence.
   */
  uint16_t target_mask =
      alarm_unconfirmed_medication_mask_at(
        now
      );

  for (
    uint8_t index = 0;
    index < s_medication_count;
    index++
  ) {
    const uint16_t medication_bit =
        (uint16_t)(1u << index);

    if (
      (
        target_mask &
        medication_bit
      ) == 0 ||
      s_medications[index].symbol !=
          (uint8_t)symbol
    ) {
      target_mask &=
          (uint16_t)~medication_bit;
    }
  }

  if (target_mask == 0) {
    schedule_next_alarm_wakeup();
    return;
  }

  bool state_changed = false;

  for (
    uint8_t index = 0;
    index < s_medication_count;
    index++
  ) {
    const uint16_t medication_bit =
        (uint16_t)(1u << index);

    if (
      (
        target_mask &
        medication_bit
      ) == 0
    ) {
      continue;
    }

    MedicationAlarmState *state = NULL;

    if (
      !medication_alarm_state_at(
        index,
        now,
        NULL,
        NULL,
        &state
      ) ||
      !state ||
      state->confirmed
    ) {
      continue;
    }

    state->confirmed = 1;
    state_changed = true;
  }

  if (state_changed) {
    (void)persist_medication_alarm_states();
  }

  s_alarm_due_medication_mask &=
      (uint16_t)~target_mask;

  if (
    s_alarm_active &&
    s_alarm_due_medication_mask == 0
  ) {
    alarm_stop();
  }

  schedule_next_alarm_wakeup();
}


void medication_alarm_init(void) {
  load_alarm_settings();
  medication_alarm_states_load();
  s_launch_wakeup_medication_mask = 0;

  speaker_set_finish_callback(
    alarm_audio_finish_callback,
    NULL
  );
  wakeup_service_subscribe(alarm_wakeup_handler);

  WakeupId launch_wakeup_id;
  int32_t launch_cookie = 0;
  uint16_t launch_medication_mask = 0;

  bool valid_wakeup_launch =
      launch_reason() == APP_LAUNCH_WAKEUP &&
      wakeup_get_launch_event(
        &launch_wakeup_id,
        &launch_cookie
      );

  if (valid_wakeup_launch) {
    if (
      alarm_event_cookie_decode(
        launch_cookie,
        &launch_medication_mask
      )
    ) {
      s_launch_wakeup_medication_mask =
          launch_medication_mask;
    } else if (
      launch_cookie != ALARM_WAKEUP_COOKIE
    ) {
      valid_wakeup_launch = false;
    }
  }

  s_alarm_launch_pending =
      valid_wakeup_launch;

  schedule_next_alarm_wakeup();
}

void medication_alarm_deinit(void) {
  alarm_stop();
  speaker_set_finish_callback(NULL, NULL);
}
