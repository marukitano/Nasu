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

static AppTimer *s_alarm_intro_timer;
static bool s_alarm_visuals_are_paused;

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
static bool medication_due_on_date(
    const MedicationSettings *medication,
    const struct tm *local_date
);
static bool alarm_window_bounds_at(
    time_t timestamp,
    time_t *window_start,
    time_t *window_end,
    MedicationTime *window_slot
);
static void persist_alarm_window_state(void);
static uint8_t alarm_symbol_mask_for_window(
    time_t window_start,
    MedicationTime slot
);
static time_t next_due_window_start_after(time_t now);
static time_t next_alarm_timestamp_after(time_t now);
static void alarm_vibrate(void);
static void alarm_pulse_timer_handler(void *context);
static void alarm_wakeup_handler(
    WakeupId wakeup_id,
    int32_t cookie
);
static bool minute_is_daypart_start(int minute);

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

static bool medication_due_on_date(
    const MedicationSettings *medication,
    const struct tm *local_date
) {
  if (!medication || !local_date || !medication->enabled) {
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

    return medication->day == monday_based_weekday;
  }

  if (
    medication->schedule ==
        MEDICATION_SCHEDULE_MONTHLY
  ) {
    return medication->day == local_date->tm_mday;
  }

  return false;
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

static void persist_alarm_window_state(void) {
  persist_write_data(
    ALARM_WINDOW_STATE_PERSIST_KEY,
    &s_alarm_window_state,
    sizeof(s_alarm_window_state)
  );
}

void alarm_refresh_window_state(void) {
  time_t window_start;

  if (
    !alarm_window_bounds_at(
      time(NULL),
      &window_start,
      NULL,
      NULL
    )
  ) {
    return;
  }

  if (!s_alarm_window_state_loaded) {
    memset(
      &s_alarm_window_state,
      0,
      sizeof(s_alarm_window_state)
    );

    if (
      persist_exists(ALARM_WINDOW_STATE_PERSIST_KEY) &&
      persist_get_size(ALARM_WINDOW_STATE_PERSIST_KEY) ==
          (int)sizeof(AlarmWindowState)
    ) {
      (void)persist_read_data(
        ALARM_WINDOW_STATE_PERSIST_KEY,
        &s_alarm_window_state,
        sizeof(s_alarm_window_state)
      );
    }

    s_alarm_window_state_loaded = true;
  }

  if (
    s_alarm_window_state.window_start !=
        (int32_t)window_start
  ) {
    s_alarm_window_state = (AlarmWindowState) {
      .window_start = (int32_t)window_start,
      .last_reminder = 0,
      .confirmed_mask = 0
    };

    persist_alarm_window_state();
  }
}

static uint8_t alarm_symbol_mask_for_window(
    time_t window_start,
    MedicationTime slot
) {
  struct tm *local_ptr = localtime(&window_start);

  if (!local_ptr) {
    return 0;
  }

  const struct tm schedule_date = *local_ptr;
  uint8_t mask = 0;

  for (
    uint8_t index = 0;
    index < s_medication_count;
    index++
  ) {
    const MedicationSettings *medication =
        &s_medications[index];

    if (
      medication->time != (uint8_t)slot ||
      !medication_due_on_date(
        medication,
        &schedule_date
      )
    ) {
      continue;
    }

    mask |= (uint8_t)(1u << medication->symbol);
  }

  return mask;
}

uint8_t alarm_unconfirmed_symbol_mask_at(
    time_t timestamp
) {
  time_t window_start;
  MedicationTime slot;

  if (
    !alarm_window_bounds_at(
      timestamp,
      &window_start,
      NULL,
      &slot
    )
  ) {
    return 0;
  }

  alarm_refresh_window_state();

  return
      alarm_symbol_mask_for_window(
        window_start,
        slot
      ) &
      (uint8_t)~s_alarm_window_state.confirmed_mask;
}

static time_t next_due_window_start_after(time_t now) {
  struct tm *today_ptr = localtime(&now);

  if (!today_ptr) {
    return 0;
  }

  const struct tm today = *today_ptr;
  const uint16_t starts[] = {
    s_dayparts.morning,
    s_dayparts.noon,
    s_dayparts.evening,
    s_dayparts.night
  };

  for (uint16_t day_offset = 0; day_offset <= 370; day_offset++) {
    for (uint8_t slot = 0; slot < ARRAY_LENGTH(starts); slot++) {
      struct tm candidate_tm = today;
      candidate_tm.tm_mday += day_offset;
      candidate_tm.tm_hour = starts[slot] / 60;
      candidate_tm.tm_min = starts[slot] % 60;
      candidate_tm.tm_sec = 0;
      candidate_tm.tm_isdst = -1;

      const time_t candidate = mktime(&candidate_tm);

      if (
        candidate <= now ||
        alarm_symbol_mask_for_window(
          candidate,
          (MedicationTime)slot
        ) == 0
      ) {
        continue;
      }

      return candidate;
    }
  }

  return 0;
}

static time_t next_alarm_timestamp_after(time_t now) {
  time_t window_start;
  time_t window_end;

  if (
    alarm_window_bounds_at(
      now,
      &window_start,
      &window_end,
      NULL
    ) &&
    alarm_unconfirmed_symbol_mask_at(now) != 0
  ) {
    time_t candidate;

    if (
      s_alarm_window_state.last_reminder <
          (int32_t)window_start
    ) {
      candidate = now + 60;
    } else {
      candidate =
          (time_t)s_alarm_window_state.last_reminder +
          (time_t)s_alarm_reminder_interval_minutes * 60;

      if (candidate <= now) {
        candidate = now + 60;
      }
    }

    if (candidate < window_end) {
      return candidate;
    }
  }

  return next_due_window_start_after(now);
}

time_t alarm_next_timestamp(void) {
  return next_alarm_timestamp_after(time(NULL));
}

void schedule_next_alarm_wakeup(void) {
  wakeup_cancel_all();

  const time_t now = time(NULL);
  const time_t candidate =
      next_alarm_timestamp_after(now);

  if (candidate <= now) {
    return;
  }

  WakeupId result = E_INTERNAL;
  time_t scheduled = candidate;

  for (
    uint8_t attempt = 0;
    attempt <= 120;
    attempt++, scheduled++
  ) {
    result = wakeup_schedule(
      scheduled,
      ALARM_WAKEUP_COOKIE,
      true
    );

    if (result != E_RANGE) {
      break;
    }
  }

  if (result < 0) {
    APP_LOG(
      APP_LOG_LEVEL_WARNING,
      "Medication wakeup failed: %ld",
      (long)result
    );
  } else {
    APP_LOG(
      APP_LOG_LEVEL_INFO,
      "Medication wakeup scheduled in %ld seconds",
      (long)(scheduled - now)
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
  s_alarm_due_symbol_mask = 0;
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

void alarm_start(void) {
  if (s_alarm_active) {
    return;
  }

  if (s_transfer_screen_active) {
    schedule_next_alarm_wakeup();
    return;
  }

  const time_t now = time(NULL);
  const uint8_t due_mask =
      alarm_unconfirmed_symbol_mask_at(now);

  if (due_mask == 0) {
    refresh_app_screen_state();
    schedule_next_alarm_wakeup();
    return;
  }

  alarm_refresh_window_state();
  s_alarm_window_state.last_reminder =
      (int32_t)now;
  persist_alarm_window_state();

  s_alarm_due_symbol_mask = due_mask;
  s_alarm_active = true;
  s_alarm_stop_time = now + ALARM_ACTIVE_SECONDS;

  /*
   * Freeze all dynamic alert work before rebuilding the screen. The first
   * frame therefore contains the final pattern and rendered medication, but
   * no pill physics or pen animation yet.
   */
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

static void alarm_wakeup_handler(
    WakeupId wakeup_id,
    int32_t cookie
) {
  (void)wakeup_id;

  if (cookie != ALARM_WAKEUP_COOKIE) {
    return;
  }

  refresh_medication_rows_for_time();
  alarm_start();
  schedule_next_alarm_wakeup();
}

static bool minute_is_daypart_start(int minute) {
  return
      minute == s_dayparts.morning ||
      minute == s_dayparts.noon ||
      minute == s_dayparts.evening ||
      minute == s_dayparts.night;
}

void alarm_handle_minute_tick(
    const struct tm *tick_time
) {
  if (!tick_time) {
    return;
  }

  const int minute =
      tick_time->tm_hour * 60 +
      tick_time->tm_min;

  if (minute_is_daypart_start(minute)) {
    alarm_refresh_window_state();
    alarm_start();
  }

  schedule_next_alarm_wakeup();
}

bool alarm_reset_after_settings_save(void) {
  alarm_stop();
  wakeup_cancel_all();

  time_t window_start = 0;

  if (
    !alarm_window_bounds_at(
      time(NULL),
      &window_start,
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

  s_alarm_window_state = (AlarmWindowState) {
    .window_start = (int32_t)window_start,
    .last_reminder = 0,
    .confirmed_mask = 0
  };
  s_alarm_window_state_loaded = true;

  /* Keep the UI mirrors in sync with the persisted reset immediately. */
  s_pills_confirmed = false;
  s_pen_confirmed = false;

  const int bytes_written = persist_write_data(
    ALARM_WINDOW_STATE_PERSIST_KEY,
    &s_alarm_window_state,
    sizeof(s_alarm_window_state)
  );

  AlarmWindowState verified = { 0 };
  const int bytes_read =
      persist_read_data(
        ALARM_WINDOW_STATE_PERSIST_KEY,
        &verified,
        sizeof(verified)
      );

  const bool reset_verified =
      bytes_written ==
          (int)sizeof(s_alarm_window_state) &&
      bytes_read == (int)sizeof(verified) &&
      verified.window_start ==
          s_alarm_window_state.window_start &&
      verified.last_reminder == 0 &&
      verified.confirmed_mask == 0;

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
  alarm_refresh_window_state();

  const uint8_t symbol_mask =
      (uint8_t)(1u << symbol);

  s_alarm_window_state.confirmed_mask |=
      symbol_mask;
  persist_alarm_window_state();

  s_alarm_due_symbol_mask &=
      (uint8_t)~symbol_mask;

  if (
    s_alarm_active &&
    s_alarm_due_symbol_mask == 0
  ) {
    alarm_stop();
  }

  schedule_next_alarm_wakeup();
}


void medication_alarm_init(void) {
  load_alarm_settings();

  speaker_set_finish_callback(
    alarm_audio_finish_callback,
    NULL
  );
  wakeup_service_subscribe(alarm_wakeup_handler);

  WakeupId launch_wakeup_id;
  int32_t launch_cookie;

  s_alarm_launch_pending =
      launch_reason() == APP_LAUNCH_WAKEUP &&
      wakeup_get_launch_event(
        &launch_wakeup_id,
        &launch_cookie
      ) &&
      launch_cookie == ALARM_WAKEUP_COOKIE;

  schedule_next_alarm_wakeup();
}

void medication_alarm_deinit(void) {
  alarm_stop();
  speaker_set_finish_callback(NULL, NULL);
}
