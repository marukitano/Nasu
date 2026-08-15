#include <pebble.h>

#include "message_keys.auto.h"

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

static const MedicationSettings s_default_medication = {
  .name = "Xarelto",
  .dosage = "20 mg",
  .effect = "Blutverdünner",
  .quantity = 1,
  .time = MEDICATION_TIME_MORNING,
  .schedule = MEDICATION_SCHEDULE_DAILY,
  .day = 0,
  .symbol = MEDICATION_SYMBOL_PILL,
  .shape = 2,
  .color = 255,
  .icon_set = 1,
  .enabled = 1
};

static const DaypartSettings s_default_dayparts = {
  .morning = DEFAULT_MORNING_START_MINUTE,
  .noon = DEFAULT_NOON_START_MINUTE,
  .evening = DEFAULT_EVENING_START_MINUTE,
  .night = DEFAULT_NIGHT_START_MINUTE
};

typedef enum {
  SETTINGS_COMMAND_RESET,
  SETTINGS_COMMAND_ITEM,
  SETTINGS_COMMAND_COMMIT
} SettingsCommand;

static uint32_t s_settings_ack_transaction;
static bool s_settings_storage_verified;
static bool s_settings_reset_verified;
static bool s_settings_ack_outbox_pending;
static AppTimer *s_settings_ack_retry_timer;

static MedicationSettings
    s_pending_medications[MAX_MEDICATIONS];
static uint8_t s_pending_count;
static uint16_t s_pending_received_mask;
static MedicationAppearance
    s_pending_medication_appearances[MAX_MEDICATIONS];
static uint8_t s_pending_medication_appearance_count;
static uint16_t s_pending_medication_appearance_mask;

static void clear_settings_ack_state(void);
static void schedule_settings_ack_retry(void);
static void send_settings_ack(void);
static void settings_ack_retry_handler(void *context);
static void settings_outbox_sent(
    DictionaryIterator *iterator,
    void *context
);
static void settings_outbox_failed(
    DictionaryIterator *iterator,
    AppMessageResult reason,
    void *context
);

static bool medication_settings_valid(
    const MedicationSettings *settings
);
static MedicationSettings medication_from_legacy_v1(
    const LegacyMedicationSettingsV1 *legacy
);
static MedicationSettings medication_from_legacy_v2(
    const LegacyMedicationSettingsV2 *legacy
);
static bool migrate_legacy_medication_symbol(
    MedicationSettings *settings
);
static bool daypart_settings_valid(
    const DaypartSettings *settings
);
static void load_daypart_settings(void);
static void apply_daypart_settings(
    const DaypartSettings *settings,
    bool save
);
static void apply_language(
    AppLanguage language,
    bool save
);
static bool medication_list_valid(
    const MedicationSettings *medications,
    uint8_t count
);
static int medication_persist_key(uint8_t index);
static bool persist_scalar_settings(void);
static bool persist_medication_list(void);
static bool apply_medication_list(
    const MedicationSettings *medications,
    uint8_t count,
    bool save
);
static bool load_current_medication_list(void);
static void load_medication_settings(void);
static bool tuple_read_int32(
    Tuple *tuple,
    int32_t *value
);
static bool read_dayparts_from_message(
    DictionaryIterator *iterator,
    DaypartSettings *settings
);
static bool read_medication_from_message(
    DictionaryIterator *iterator,
    MedicationSettings *settings
);
static uint16_t expected_pending_mask(
    uint8_t count
);
static void reset_pending_medications(
    uint8_t count
);
static int medication_appearance_persist_key(uint8_t index);
static void reset_pending_medication_appearances(uint8_t count);
static bool read_medication_appearance_from_message(
    DictionaryIterator *iterator,
    MedicationAppearance *appearance
);
static bool persist_medication_appearances(void);
static void load_medication_appearances(void);
static bool apply_medication_appearances(void);
static void settings_inbox_received(
    DictionaryIterator *iterator,
    void *context
);

static void clear_settings_ack_state(void) {
  cancel_timer(&s_settings_ack_retry_timer);
  s_settings_ack_transaction = 0;
  s_settings_storage_verified = false;
  s_settings_reset_verified = false;
  s_settings_ack_outbox_pending = false;
}

static void schedule_settings_ack_retry(void) {
  if (
    s_settings_ack_retry_timer ||
    s_settings_ack_transaction == 0
  ) {
    return;
  }

  s_settings_ack_retry_timer = app_timer_register(
    SETTINGS_ACK_RETRY_MS,
    settings_ack_retry_handler,
    NULL
  );

  if (!s_settings_ack_retry_timer) {
    APP_LOG(
      APP_LOG_LEVEL_ERROR,
      "Could not schedule settings ACK retry"
    );
  }
}

static void send_settings_ack(void) {
  if (
    s_settings_ack_transaction == 0 ||
    !s_settings_storage_verified ||
    !s_settings_reset_verified ||
    s_settings_ack_outbox_pending
  ) {
    return;
  }

  DictionaryIterator *iterator = NULL;
  const AppMessageResult begin_result =
      app_message_outbox_begin(&iterator);

  if (
    begin_result != APP_MSG_OK ||
    !iterator
  ) {
    APP_LOG(
      APP_LOG_LEVEL_WARNING,
      "Settings ACK outbox unavailable: %d",
      (int)begin_result
    );
    schedule_settings_ack_retry();
    return;
  }

  if (
    dict_write_uint32(
      iterator,
      MESSAGE_KEY_SETTINGS_ACK,
      s_settings_ack_transaction
    ) != DICT_OK
  ) {
    APP_LOG(
      APP_LOG_LEVEL_WARNING,
      "Could not write settings ACK"
    );
    schedule_settings_ack_retry();
    return;
  }

  dict_write_end(iterator);

  const AppMessageResult send_result =
      app_message_outbox_send();

  if (send_result != APP_MSG_OK) {
    APP_LOG(
      APP_LOG_LEVEL_WARNING,
      "Could not send settings ACK: %d",
      (int)send_result
    );
    schedule_settings_ack_retry();
    return;
  }

  s_settings_ack_outbox_pending = true;
}

static void settings_ack_retry_handler(void *context) {
  (void)context;
  s_settings_ack_retry_timer = NULL;

  if (s_settings_ack_transaction == 0) {
    return;
  }

  if (!s_settings_storage_verified) {
    s_settings_storage_verified =
        persist_scalar_settings() &&
        persist_medication_list() &&
        persist_medication_appearances();

    if (!s_settings_storage_verified) {
      schedule_settings_ack_retry();
      return;
    }
  }

  if (!s_settings_reset_verified) {
    s_settings_reset_verified =
        alarm_reset_after_settings_save();

    if (!s_settings_reset_verified) {
      schedule_settings_ack_retry();
      return;
    }
  }

  send_settings_ack();
}

static uint32_t settings_ack_value(
    DictionaryIterator *iterator
) {
  Tuple *ack_tuple = dict_find(
    iterator,
    MESSAGE_KEY_SETTINGS_ACK
  );

  if (
    !ack_tuple ||
    (
      ack_tuple->type != TUPLE_INT &&
      ack_tuple->type != TUPLE_UINT
    )
  ) {
    return 0;
  }

  return (uint32_t)ack_tuple->value->int32;
}

static void settings_outbox_sent(
    DictionaryIterator *iterator,
    void *context
) {
  (void)context;
  s_settings_ack_outbox_pending = false;

  const uint32_t acknowledged_transaction =
      settings_ack_value(iterator);

  if (
    acknowledged_transaction == 0 ||
    acknowledged_transaction !=
        s_settings_ack_transaction
  ) {
    if (s_settings_ack_transaction != 0) {
      schedule_settings_ack_retry();
    }
    return;
  }

  APP_LOG(
    APP_LOG_LEVEL_INFO,
    "Settings ACK %lu delivered to phone",
    (unsigned long)acknowledged_transaction
  );

  cancel_timer(&s_settings_ack_retry_timer);
  s_settings_ack_transaction = 0;
  s_settings_storage_verified = false;
  s_settings_reset_verified = false;
  reset_pending_medications(0);
  reset_pending_medication_appearances(0);

  /* The watch closes only after the phone transport accepted the ACK. */
  schedule_transfer_close();
}

static void settings_outbox_failed(
    DictionaryIterator *iterator,
    AppMessageResult reason,
    void *context
) {
  (void)iterator;
  (void)context;
  s_settings_ack_outbox_pending = false;

  APP_LOG(
    APP_LOG_LEVEL_WARNING,
    "Settings ACK delivery failed: %d",
    (int)reason
  );

  schedule_settings_ack_retry();
}

static bool medication_settings_valid(
    const MedicationSettings *settings
) {
  if (
    !settings ||
    settings->name[0] == '\0' ||
    settings->name[
      MEDICATION_NAME_LENGTH - 1
    ] != '\0' ||
    settings->dosage[
      MEDICATION_DOSAGE_LENGTH - 1
    ] != '\0' ||
    settings->effect[
      MEDICATION_EFFECT_LENGTH - 1
    ] != '\0' ||
    settings->quantity < 1 ||
    settings->quantity > 20 ||
    settings->time > MEDICATION_TIME_NIGHT ||
    settings->schedule >
        MEDICATION_SCHEDULE_MONTHLY ||
    settings->symbol >
        MEDICATION_SYMBOL_PEN ||
    settings->shape > 3 ||
    settings->color < 192 ||
    settings->icon_set > 1 ||
    settings->enabled > 1 ||
    (settings->enabled && !settings->icon_set)
  ) {
    return false;
  }

  if (
    settings->schedule ==
        MEDICATION_SCHEDULE_DAILY
  ) {
    return settings->day == 0;
  }

  if (
    settings->schedule ==
        MEDICATION_SCHEDULE_WEEKLY
  ) {
    return settings->day <= 6;
  }

  return
      settings->day >= 1 &&
      settings->day <= 31;
}

static MedicationSettings medication_from_legacy_v1(
    const LegacyMedicationSettingsV1 *legacy
) {
  MedicationSettings medication =
      s_default_medication;

  if (!legacy) {
    return medication;
  }

  memcpy(
    medication.name,
    legacy->name,
    sizeof(medication.name)
  );

  medication.dosage[0] = '\0';
  medication.effect[0] = '\0';
  medication.quantity = legacy->quantity;
  medication.time = legacy->time;
  medication.schedule = legacy->schedule;
  medication.day = legacy->day;
  medication.symbol = legacy->symbol;
  medication.enabled = legacy->enabled;
  medication.shape = 2;
  medication.color = 255;
  medication.icon_set = 1;

  return medication;
}

static MedicationSettings medication_from_legacy_v2(
    const LegacyMedicationSettingsV2 *legacy
) {
  MedicationSettings medication =
      s_default_medication;

  if (!legacy) {
    return medication;
  }

  memcpy(
    medication.name,
    legacy->name,
    sizeof(medication.name)
  );

  medication.dosage[0] = '\0';
  medication.effect[0] = '\0';
  medication.quantity = legacy->quantity;
  medication.time = legacy->time;
  medication.schedule = legacy->schedule;
  medication.day = legacy->day;
  medication.symbol = legacy->symbol;
  medication.shape = legacy->shape;
  medication.color = legacy->color;
  medication.icon_set = legacy->icon_set;
  medication.enabled = legacy->enabled;

  return medication;
}

static bool migrate_legacy_medication_symbol(
    MedicationSettings *settings
) {
  if (
    settings &&
    settings->symbol ==
        LEGACY_MEDICATION_SYMBOL_TUBE
  ) {
    settings->symbol =
        MEDICATION_SYMBOL_PILL;
    return true;
  }

  return false;
}

static bool daypart_settings_valid(
    const DaypartSettings *settings
) {
  return
      settings &&
      settings->morning < settings->noon &&
      settings->noon < settings->evening &&
      settings->evening < settings->night &&
      settings->night < DAYPART_MINUTES_PER_DAY;
}

static void load_daypart_settings(void) {
  s_dayparts = s_default_dayparts;

  if (
    !persist_exists(DAYPART_PERSIST_KEY) ||
    persist_get_size(DAYPART_PERSIST_KEY) !=
        (int)sizeof(DaypartSettings)
  ) {
    return;
  }

  DaypartSettings stored;

  if (
    persist_read_data(
      DAYPART_PERSIST_KEY,
      &stored,
      sizeof(stored)
    ) == (int)sizeof(stored) &&
    daypart_settings_valid(&stored)
  ) {
    const bool legacy_defaults =
        stored.morning ==
            LEGACY_DEFAULT_MORNING_START_MINUTE &&
        stored.noon ==
            LEGACY_DEFAULT_NOON_START_MINUTE &&
        stored.evening ==
            LEGACY_DEFAULT_EVENING_START_MINUTE &&
        stored.night ==
            LEGACY_DEFAULT_NIGHT_START_MINUTE;

    if (legacy_defaults) {
      persist_write_data(
        DAYPART_PERSIST_KEY,
        &s_dayparts,
        sizeof(s_dayparts)
      );
    } else {
      s_dayparts = stored;
    }
  }
}

static void apply_daypart_settings(
    const DaypartSettings *settings,
    bool save
) {
  if (!daypart_settings_valid(settings)) {
    return;
  }

  s_dayparts = *settings;

  if (save) {
    persist_write_data(
      DAYPART_PERSIST_KEY,
      &s_dayparts,
      sizeof(s_dayparts)
    );
  }

  s_visible_medication_time_set = false;
  refresh_medication_rows_for_time();
}

static void apply_language(
    AppLanguage language,
    bool save
) {
  if (
    language > APP_LANGUAGE_ENGLISH
  ) {
    language = APP_LANGUAGE_GERMAN;
  }

  s_language = language;

  if (save) {
    persist_write_int(
      LANGUAGE_PERSIST_KEY,
      (int)s_language
    );
  }

  mark_scene_dirty();
}

static bool medication_list_valid(
    const MedicationSettings *medications,
    uint8_t count
) {
  if (count > MAX_MEDICATIONS) {
    return false;
  }

  for (
    uint8_t index = 0;
    index < count;
    index++
  ) {
    if (
      !medication_settings_valid(
        &medications[index]
      )
    ) {
      return false;
    }
  }

  return true;
}

static int medication_persist_key(uint8_t index) {
  return MEDICATION_ITEM_PERSIST_KEY_BASE + index;
}

static bool persist_scalar_settings(void) {
  const int theme_written = persist_write_int(
    THEME_PERSIST_KEY,
    (int)s_theme_mode
  );
  const int language_written = persist_write_int(
    LANGUAGE_PERSIST_KEY,
    (int)s_language
  );
  const int pattern_written = persist_write_int(
    SHOW_JAPANESE_PATTERN_PERSIST_KEY,
    s_show_japanese_pattern ? 1 : 0
  );
  const int dayparts_written = persist_write_data(
    DAYPART_PERSIST_KEY,
    &s_dayparts,
    sizeof(s_dayparts)
  );
  const int volume_written = persist_write_int(
    ALARM_AUDIO_VOLUME_PERSIST_KEY,
    s_alarm_audio_volume
  );
  const int vibration_written = persist_write_int(
    ALARM_VIBRATION_PERSIST_KEY,
    s_alarm_vibration_enabled ? 1 : 0
  );
  const int interval_written = persist_write_int(
    ALARM_INTERVAL_PERSIST_KEY,
    s_alarm_reminder_interval_minutes
  );

  DaypartSettings verified_dayparts = { 0 };
  const int dayparts_read = persist_read_data(
    DAYPART_PERSIST_KEY,
    &verified_dayparts,
    sizeof(verified_dayparts)
  );

  const bool verified =
      theme_written == (int)sizeof(int32_t) &&
      language_written == (int)sizeof(int32_t) &&
      pattern_written == (int)sizeof(int32_t) &&
      dayparts_written == (int)sizeof(s_dayparts) &&
      volume_written == (int)sizeof(int32_t) &&
      vibration_written == (int)sizeof(int32_t) &&
      interval_written == (int)sizeof(int32_t) &&
      persist_read_int(THEME_PERSIST_KEY) ==
          (int)s_theme_mode &&
      persist_read_int(LANGUAGE_PERSIST_KEY) ==
          (int)s_language &&
      persist_read_int(SHOW_JAPANESE_PATTERN_PERSIST_KEY) ==
          (s_show_japanese_pattern ? 1 : 0) &&
      dayparts_read == (int)sizeof(verified_dayparts) &&
      memcmp(
        &verified_dayparts,
        &s_dayparts,
        sizeof(s_dayparts)
      ) == 0 &&
      persist_read_int(ALARM_AUDIO_VOLUME_PERSIST_KEY) ==
          s_alarm_audio_volume &&
      persist_read_int(ALARM_VIBRATION_PERSIST_KEY) ==
          (s_alarm_vibration_enabled ? 1 : 0) &&
      persist_read_int(ALARM_INTERVAL_PERSIST_KEY) ==
          s_alarm_reminder_interval_minutes;

  if (!verified) {
    APP_LOG(
      APP_LOG_LEVEL_ERROR,
      "Scalar settings persistence verification failed"
    );
  }

  return verified;
}

static bool persist_medication_list(void) {
  if (
    !medication_list_valid(
      s_medications,
      s_medication_count
    )
  ) {
    return false;
  }

  for (
    uint8_t index = 0;
    index < s_medication_count;
    index++
  ) {
    const int key = medication_persist_key(index);
    const int written = persist_write_data(
      key,
      &s_medications[index],
      sizeof(MedicationSettings)
    );

    MedicationSettings verified;
    memset(&verified, 0, sizeof(verified));

    const int read = persist_read_data(
      key,
      &verified,
      sizeof(verified)
    );

    if (
      written != (int)sizeof(MedicationSettings) ||
      read != (int)sizeof(MedicationSettings) ||
      memcmp(
        &verified,
        &s_medications[index],
        sizeof(MedicationSettings)
      ) != 0
    ) {
      APP_LOG(
        APP_LOG_LEVEL_ERROR,
        "Medication %u persistence verification failed",
        (unsigned int)index
      );
      return false;
    }
  }

  for (
    uint8_t index = s_medication_count;
    index < MAX_MEDICATIONS;
    index++
  ) {
    const int key = medication_persist_key(index);

    if (persist_exists(key)) {
      persist_delete(key);
    }
  }

  const int count_written = persist_write_int(
    MEDICATION_COUNT_PERSIST_KEY,
    s_medication_count
  );

  if (
    count_written != (int)sizeof(int32_t) ||
    persist_read_int(MEDICATION_COUNT_PERSIST_KEY) !=
        s_medication_count
  ) {
    APP_LOG(
      APP_LOG_LEVEL_ERROR,
      "Medication count persistence verification failed"
    );
    return false;
  }

  /* Remove the former aggregate blob after the itemized copy is verified. */
  if (persist_exists(MEDICATION_LIST_PERSIST_KEY)) {
    persist_delete(MEDICATION_LIST_PERSIST_KEY);
  }

  return true;
}

static bool apply_medication_list(
    const MedicationSettings *medications,
    uint8_t count,
    bool save
) {
  if (
    !medication_list_valid(
      medications,
      count
    )
  ) {
    return false;
  }

  memset(
    s_medications,
    0,
    sizeof(s_medications)
  );

  if (count > 0) {
    memcpy(
      s_medications,
      medications,
      sizeof(MedicationSettings) * count
    );
  }

  s_medication_count = count;

  const bool saved =
      !save ||
      persist_medication_list();

  refresh_app_screen_state();
  return saved;
}

static bool load_itemized_medication_list(
    uint8_t count,
    MedicationSettings *stored
) {
  if (!stored) {
    return false;
  }

  for (
    uint8_t index = 0;
    index < count;
    index++
  ) {
    const int key = medication_persist_key(index);

    if (
      !persist_exists(key) ||
      persist_get_size(key) !=
          (int)sizeof(MedicationSettings) ||
      persist_read_data(
        key,
        &stored[index],
        sizeof(MedicationSettings)
      ) != (int)sizeof(MedicationSettings)
    ) {
      return false;
    }
  }

  return medication_list_valid(stored, count);
}

static bool load_current_medication_list(void) {
  if (
    !persist_exists(
      MEDICATION_COUNT_PERSIST_KEY
    )
  ) {
    return false;
  }

  const int stored_count =
      persist_read_int(
        MEDICATION_COUNT_PERSIST_KEY
      );

  if (
    stored_count < 0 ||
    stored_count > MAX_MEDICATIONS
  ) {
    return false;
  }

  if (stored_count == 0) {
    s_medication_count = 0;
    rebuild_medication_rows();
    return true;
  }

  MedicationSettings stored[
    MAX_MEDICATIONS
  ];

  memset(stored, 0, sizeof(stored));

  bool needs_persist = false;

  if (
    !load_itemized_medication_list(
      (uint8_t)stored_count,
      stored
    )
  ) {
    if (
      !persist_exists(
        MEDICATION_LIST_PERSIST_KEY
      )
    ) {
      return false;
    }

    const int stored_size = persist_get_size(
      MEDICATION_LIST_PERSIST_KEY
    );

    const int current_size =
        (int)(
          sizeof(MedicationSettings) *
          stored_count
        );

    const int legacy_v2_size =
        (int)(
          sizeof(LegacyMedicationSettingsV2) *
          stored_count
        );

    const int legacy_v1_size =
        (int)(
          sizeof(LegacyMedicationSettingsV1) *
          stored_count
        );

    if (stored_size == current_size) {
      if (
        persist_read_data(
          MEDICATION_LIST_PERSIST_KEY,
          stored,
          current_size
        ) != current_size
      ) {
        return false;
      }
    } else if (stored_size == legacy_v2_size) {
      LegacyMedicationSettingsV2 legacy[
        MAX_MEDICATIONS
      ];

      memset(legacy, 0, sizeof(legacy));

      if (
        persist_read_data(
          MEDICATION_LIST_PERSIST_KEY,
          legacy,
          legacy_v2_size
        ) != legacy_v2_size
      ) {
        return false;
      }

      for (
        uint8_t index = 0;
        index < (uint8_t)stored_count;
        index++
      ) {
        stored[index] =
            medication_from_legacy_v2(
              &legacy[index]
            );
      }
    } else if (stored_size == legacy_v1_size) {
      LegacyMedicationSettingsV1 legacy[
        MAX_MEDICATIONS
      ];

      memset(legacy, 0, sizeof(legacy));

      if (
        persist_read_data(
          MEDICATION_LIST_PERSIST_KEY,
          legacy,
          legacy_v1_size
        ) != legacy_v1_size
      ) {
        return false;
      }

      for (
        uint8_t index = 0;
        index < (uint8_t)stored_count;
        index++
      ) {
        stored[index] =
            medication_from_legacy_v1(
              &legacy[index]
            );
      }
    } else {
      return false;
    }

    needs_persist = true;
  }

  for (
    uint8_t index = 0;
    index < (uint8_t)stored_count;
    index++
  ) {
    if (
      migrate_legacy_medication_symbol(
        &stored[index]
      )
    ) {
      needs_persist = true;
    }
  }

  if (
    !medication_list_valid(
      stored,
      (uint8_t)stored_count
    )
  ) {
    return false;
  }

  memcpy(
    s_medications,
    stored,
    sizeof(MedicationSettings) *
        stored_count
  );

  s_medication_count =
      (uint8_t)stored_count;

  reset_medication_confirmations();
  rebuild_medication_rows();

  if (
    needs_persist &&
    !persist_medication_list()
  ) {
    APP_LOG(
      APP_LOG_LEVEL_WARNING,
      "Medication migration could not be persisted"
    );
  }

  return true;
}

static void load_medication_settings(void) {
  memset(
    s_medications,
    0,
    sizeof(s_medications)
  );

  if (load_current_medication_list()) {
    return;
  }

  MedicationSettings migrated =
      s_default_medication;

  if (
    persist_exists(
      LEGACY_MEDICATION_PERSIST_KEY
    )
  ) {
    const int legacy_size =
        persist_get_size(
          LEGACY_MEDICATION_PERSIST_KEY
        );

    MedicationSettings stored =
        s_default_medication;

    bool read_successfully = false;

    if (
      legacy_size ==
          (int)sizeof(MedicationSettings)
    ) {
      read_successfully =
          persist_read_data(
            LEGACY_MEDICATION_PERSIST_KEY,
            &stored,
            sizeof(stored)
          ) == (int)sizeof(stored);
    } else if (
      legacy_size ==
          (int)sizeof(LegacyMedicationSettingsV2)
    ) {
      LegacyMedicationSettingsV2 legacy;

      if (
        persist_read_data(
          LEGACY_MEDICATION_PERSIST_KEY,
          &legacy,
          sizeof(legacy)
        ) == (int)sizeof(legacy)
      ) {
        stored = medication_from_legacy_v2(&legacy);
        read_successfully = true;
      }
    } else if (
      legacy_size ==
          (int)sizeof(LegacyMedicationSettingsV1)
    ) {
      LegacyMedicationSettingsV1 legacy;

      if (
        persist_read_data(
          LEGACY_MEDICATION_PERSIST_KEY,
          &legacy,
          sizeof(legacy)
        ) == (int)sizeof(legacy)
      ) {
        stored = medication_from_legacy_v1(&legacy);
        read_successfully = true;
      }
    }

    if (read_successfully) {
      migrate_legacy_medication_symbol(
        &stored
      );

      if (medication_settings_valid(&stored)) {
        migrated = stored;
      }
    }
  }

  s_medications[0] = migrated;
  s_medication_count = 1;
  rebuild_medication_rows();
  (void)persist_medication_list();
}

static bool tuple_read_int32(
    Tuple *tuple,
    int32_t *value
) {
  if (
    !tuple ||
    !value ||
    (
      tuple->type != TUPLE_INT &&
      tuple->type != TUPLE_UINT
    )
  ) {
    return false;
  }

  *value = tuple->value->int32;
  return true;
}

static bool read_dayparts_from_message(
    DictionaryIterator *iterator,
    DaypartSettings *settings
) {
  Tuple *morning_tuple = dict_find(
    iterator,
    MESSAGE_KEY_DAYPART_MORNING
  );

  Tuple *noon_tuple = dict_find(
    iterator,
    MESSAGE_KEY_DAYPART_NOON
  );

  Tuple *evening_tuple = dict_find(
    iterator,
    MESSAGE_KEY_DAYPART_EVENING
  );

  Tuple *night_tuple = dict_find(
    iterator,
    MESSAGE_KEY_DAYPART_NIGHT
  );

  int32_t morning;
  int32_t noon;
  int32_t evening;
  int32_t night;

  if (
    !tuple_read_int32(
      morning_tuple,
      &morning
    ) ||
    !tuple_read_int32(
      noon_tuple,
      &noon
    ) ||
    !tuple_read_int32(
      evening_tuple,
      &evening
    ) ||
    !tuple_read_int32(
      night_tuple,
      &night
    ) ||
    morning < 0 ||
    noon < 0 ||
    evening < 0 ||
    night < 0 ||
    morning >= DAYPART_MINUTES_PER_DAY ||
    noon >= DAYPART_MINUTES_PER_DAY ||
    evening >= DAYPART_MINUTES_PER_DAY ||
    night >= DAYPART_MINUTES_PER_DAY
  ) {
    return false;
  }

  const DaypartSettings parsed = {
    .morning = (uint16_t)morning,
    .noon = (uint16_t)noon,
    .evening = (uint16_t)evening,
    .night = (uint16_t)night
  };

  if (!daypart_settings_valid(&parsed)) {
    return false;
  }

  *settings = parsed;
  return true;
}

static bool read_medication_from_message(
    DictionaryIterator *iterator,
    MedicationSettings *settings
) {
  Tuple *name_tuple = dict_find(
    iterator,
    MESSAGE_KEY_MED_NAME
  );

  Tuple *dosage_tuple = dict_find(
    iterator,
    MESSAGE_KEY_MED_DOSAGE
  );

  Tuple *effect_tuple = dict_find(
    iterator,
    MESSAGE_KEY_MED_EFFECT
  );

  Tuple *quantity_tuple = dict_find(
    iterator,
    MESSAGE_KEY_MED_QUANTITY
  );

  Tuple *time_tuple = dict_find(
    iterator,
    MESSAGE_KEY_MED_TIME
  );

  Tuple *schedule_tuple = dict_find(
    iterator,
    MESSAGE_KEY_MED_SCHEDULE
  );

  Tuple *day_tuple = dict_find(
    iterator,
    MESSAGE_KEY_MED_DAY
  );

  Tuple *symbol_tuple = dict_find(
    iterator,
    MESSAGE_KEY_MED_SYMBOL
  );

  Tuple *shape_tuple = dict_find(
    iterator,
    MESSAGE_KEY_MED_SHAPE
  );

  Tuple *color_tuple = dict_find(
    iterator,
    MESSAGE_KEY_MED_COLOR
  );

  Tuple *icon_set_tuple = dict_find(
    iterator,
    MESSAGE_KEY_MED_ICON_SET
  );

  Tuple *enabled_tuple = dict_find(
    iterator,
    MESSAGE_KEY_MED_ENABLED
  );

  if (
    !name_tuple ||
    name_tuple->type != TUPLE_CSTRING ||
    !dosage_tuple ||
    dosage_tuple->type != TUPLE_CSTRING ||
    !effect_tuple ||
    effect_tuple->type != TUPLE_CSTRING
  ) {
    return false;
  }

  const char *name =
      name_tuple->value->cstring;
  const char *dosage =
      dosage_tuple->value->cstring;
  const char *effect =
      effect_tuple->value->cstring;
  const size_t name_length = strlen(name);
  const size_t dosage_length = strlen(dosage);
  const size_t effect_length = strlen(effect);

  int32_t quantity;
  int32_t time;
  int32_t schedule;
  int32_t day;
  int32_t symbol;
  int32_t shape;
  int32_t color;
  int32_t icon_set;
  int32_t enabled;

  if (
    name_length == 0 ||
    name_length >= MEDICATION_NAME_LENGTH ||
    dosage_length >= MEDICATION_DOSAGE_LENGTH ||
    effect_length >= MEDICATION_EFFECT_LENGTH ||
    !tuple_read_int32(
      quantity_tuple,
      &quantity
    ) ||
    !tuple_read_int32(
      time_tuple,
      &time
    ) ||
    !tuple_read_int32(
      schedule_tuple,
      &schedule
    ) ||
    !tuple_read_int32(
      day_tuple,
      &day
    ) ||
    !tuple_read_int32(
      symbol_tuple,
      &symbol
    ) ||
    !tuple_read_int32(
      shape_tuple,
      &shape
    ) ||
    !tuple_read_int32(
      color_tuple,
      &color
    ) ||
    !tuple_read_int32(
      icon_set_tuple,
      &icon_set
    ) ||
    !tuple_read_int32(
      enabled_tuple,
      &enabled
    ) ||
    quantity < 1 ||
    quantity > 20 ||
    time < MEDICATION_TIME_MORNING ||
    time > MEDICATION_TIME_NIGHT ||
    schedule < MEDICATION_SCHEDULE_DAILY ||
    schedule > MEDICATION_SCHEDULE_MONTHLY ||
    symbol < MEDICATION_SYMBOL_PILL ||
    symbol > MEDICATION_SYMBOL_PEN ||
    shape < 0 ||
    shape > 4 ||
    color < 192 ||
    color > 255 ||
    icon_set < 0 ||
    icon_set > 1 ||
    enabled < 0 ||
    enabled > 1 ||
    (enabled && !icon_set)
  ) {
    return false;
  }

  if (
    (
      schedule == MEDICATION_SCHEDULE_DAILY &&
      day != 0
    ) ||
    (
      schedule == MEDICATION_SCHEDULE_WEEKLY &&
      (day < 0 || day > 6)
    ) ||
    (
      schedule == MEDICATION_SCHEDULE_MONTHLY &&
      (day < 1 || day > 31)
    )
  ) {
    return false;
  }

  MedicationSettings parsed = {
    .quantity = (uint8_t)quantity,
    .time = (uint8_t)time,
    .schedule = (uint8_t)schedule,
    .day = (uint8_t)day,
    .symbol = (uint8_t)symbol,
    .shape = (uint8_t)(shape <= 3 ? shape : 2),
    .color = (uint8_t)color,
    .icon_set = (uint8_t)icon_set,
    .enabled = (uint8_t)enabled
  };

  memcpy(
    parsed.name,
    name,
    name_length + 1
  );
  memcpy(
    parsed.dosage,
    dosage,
    dosage_length + 1
  );
  memcpy(
    parsed.effect,
    effect,
    effect_length + 1
  );

  if (!medication_settings_valid(&parsed)) {
    return false;
  }

  *settings = parsed;
  return true;
}

static uint16_t expected_pending_mask(
    uint8_t count
) {
  if (count == 0) {
    return 0;
  }

  return
      (uint16_t)((1u << count) - 1u);
}

static void reset_pending_medications(
    uint8_t count
) {
  memset(
    s_pending_medications,
    0,
    sizeof(s_pending_medications)
  );

  s_pending_count = count;
  s_pending_received_mask = 0;
}

static int medication_appearance_persist_key(uint8_t index) {
  return MEDICATION_APPEARANCE_PERSIST_KEY_BASE + index;
}

static void reset_pending_medication_appearances(uint8_t count) {
  s_pending_medication_appearance_count = count;
  s_pending_medication_appearance_mask = 0;
  memset(
    s_pending_medication_appearances,
    0,
    sizeof(s_pending_medication_appearances)
  );
}

static bool read_medication_appearance_from_message(
    DictionaryIterator *iterator,
    MedicationAppearance *appearance
) {
  if (!iterator || !appearance) {
    return false;
  }

  Tuple *shape_tuple = dict_find(
    iterator,
    MESSAGE_KEY_MED_SHAPE
  );
  Tuple *primary_color_tuple = dict_find(
    iterator,
    MESSAGE_KEY_MED_COLOR
  );
  Tuple *secondary_color_tuple = dict_find(
    iterator,
    MESSAGE_KEY_MED_COLOR2
  );
  Tuple *size_tuple = dict_find(
    iterator,
    MESSAGE_KEY_MED_SIZE
  );
  Tuple *imprint_tuple = dict_find(
    iterator,
    MESSAGE_KEY_MED_IMPRINT
  );

  int32_t shape;
  int32_t primary_color;
  int32_t secondary_color;
  int32_t size;

  if (
    !tuple_read_int32(shape_tuple, &shape) ||
    !tuple_read_int32(primary_color_tuple, &primary_color) ||
    !tuple_read_int32(secondary_color_tuple, &secondary_color) ||
    !tuple_read_int32(size_tuple, &size) ||
    !imprint_tuple ||
    imprint_tuple->type != TUPLE_CSTRING ||
    shape < 0 ||
    shape > 4 ||
    primary_color < 192 ||
    primary_color > 255 ||
    secondary_color < 192 ||
    secondary_color > 255 ||
    size < 60 ||
    size > 140
  ) {
    return false;
  }

  const char *imprint = imprint_tuple->value->cstring;
  const size_t imprint_length = strlen(imprint);

  if (imprint_length >= MEDICATION_APPEARANCE_IMPRINT_LENGTH) {
    return false;
  }

  for (size_t index = 0; index < imprint_length; index++) {
    const unsigned char character = (unsigned char)imprint[index];

    if (character < 32 || character > 126) {
      return false;
    }
  }

  MedicationAppearance parsed = {
    .valid = true,
    .shape = (uint8_t)shape,
    .primary_color = (uint8_t)primary_color,
    .secondary_color = (uint8_t)secondary_color,
    .size = (uint8_t)size,
    .imprint = { 0 }
  };

  memcpy(
    parsed.imprint,
    imprint,
    imprint_length + 1
  );

  *appearance = parsed;
  return true;
}

static bool persist_medication_appearances(void) {
  for (uint8_t index = 0; index < MAX_MEDICATIONS; index++) {
    const int key = medication_appearance_persist_key(index);

    if (
      index < s_medication_appearance_count &&
      s_medication_appearances[index].valid
    ) {
      const int written = persist_write_data(
        key,
        &s_medication_appearances[index],
        sizeof(MedicationAppearance)
      );
      MedicationAppearance verified = { 0 };
      const int read = persist_read_data(
        key,
        &verified,
        sizeof(verified)
      );

      if (
        written != (int)sizeof(MedicationAppearance) ||
        read != (int)sizeof(MedicationAppearance) ||
        memcmp(
          &verified,
          &s_medication_appearances[index],
          sizeof(MedicationAppearance)
        ) != 0
      ) {
        APP_LOG(
          APP_LOG_LEVEL_ERROR,
          "Medication appearance %u persistence verification failed",
          (unsigned int)index
        );
        return false;
      }
    } else if (persist_exists(key)) {
      persist_delete(key);
    }
  }

  const int count_written = persist_write_int(
    MEDICATION_APPEARANCE_COUNT_PERSIST_KEY,
    s_medication_appearance_count
  );

  const bool verified =
      count_written == (int)sizeof(int32_t) &&
      persist_read_int(
        MEDICATION_APPEARANCE_COUNT_PERSIST_KEY
      ) == s_medication_appearance_count;

  if (!verified) {
    APP_LOG(
      APP_LOG_LEVEL_ERROR,
      "Medication appearance count persistence verification failed"
    );
  }

  return verified;
}

static void load_medication_appearances(void) {
  memset(
    s_medication_appearances,
    0,
    sizeof(s_medication_appearances)
  );
  s_medication_appearance_count = 0;

  if (!persist_exists(MEDICATION_APPEARANCE_COUNT_PERSIST_KEY)) {
    return;
  }

  const int stored_count = persist_read_int(
    MEDICATION_APPEARANCE_COUNT_PERSIST_KEY
  );

  if (stored_count < 0 || stored_count > MAX_MEDICATIONS) {
    return;
  }

  s_medication_appearance_count = (uint8_t)stored_count;

  for (uint8_t index = 0; index < s_medication_appearance_count; index++) {
    const int key = medication_appearance_persist_key(index);

    if (
      !persist_exists(key) ||
      persist_get_size(key) != (int)sizeof(MedicationAppearance) ||
      persist_read_data(
        key,
        &s_medication_appearances[index],
        sizeof(MedicationAppearance)
      ) != (int)sizeof(MedicationAppearance) ||
      !s_medication_appearances[index].valid ||
      s_medication_appearances[index].shape > 4 ||
      s_medication_appearances[index].primary_color < 192 ||
      s_medication_appearances[index].secondary_color < 192 ||
      s_medication_appearances[index].size < 60 ||
      s_medication_appearances[index].size > 140 ||
      s_medication_appearances[index].imprint[
        MEDICATION_APPEARANCE_IMPRINT_LENGTH - 1
      ] != '\0'
    ) {
      memset(
        &s_medication_appearances[index],
        0,
        sizeof(MedicationAppearance)
      );
    }
  }
}

static bool apply_medication_appearances(void) {
  memcpy(
    s_medication_appearances,
    s_pending_medication_appearances,
    sizeof(s_medication_appearances)
  );
  s_medication_appearance_count =
      s_pending_medication_appearance_count;
  const bool saved =
      persist_medication_appearances();
  pill_physics_rebuild();

  if (s_canvas_layer) {
    layer_mark_dirty(s_canvas_layer);
  }

  return saved;
}

static void settings_inbox_received(
    DictionaryIterator *iterator,
    void *context
) {
  (void)context;

  Tuple *command_tuple = dict_find(
    iterator,
    MESSAGE_KEY_MED_COMMAND
  );

  int32_t command;

  if (
    !tuple_read_int32(
      command_tuple,
      &command
    )
  ) {
    return;
  }

  Tuple *count_tuple = dict_find(
    iterator,
    MESSAGE_KEY_MED_COUNT
  );

  int32_t count;

  if (
    !tuple_read_int32(
      count_tuple,
      &count
    ) ||
    count < 0 ||
    count > MAX_MEDICATIONS
  ) {
    return;
  }

  if (command == SETTINGS_COMMAND_RESET) {
    clear_settings_ack_state();
    reset_pending_medications(
      (uint8_t)count
    );
    reset_pending_medication_appearances(
      (uint8_t)count
    );
    show_transfer_screen();
    return;
  }

  if (
    count != s_pending_count ||
    count != s_pending_medication_appearance_count
  ) {
    return;
  }

  if (command == SETTINGS_COMMAND_ITEM) {
    Tuple *index_tuple = dict_find(
      iterator,
      MESSAGE_KEY_MED_INDEX
    );

    int32_t index;

    if (
      !tuple_read_int32(
        index_tuple,
        &index
      ) ||
      index < 0 ||
      index >= count
    ) {
      return;
    }

    MedicationSettings medication;
    MedicationAppearance appearance;

    if (
      !read_medication_from_message(
        iterator,
        &medication
      ) ||
      !read_medication_appearance_from_message(
        iterator,
        &appearance
      )
    ) {
      APP_LOG(
        APP_LOG_LEVEL_WARNING,
        "Incomplete medication item %ld ignored",
        (long)index
      );
      return;
    }

    s_pending_medications[index] = medication;
    s_pending_medication_appearances[index] = appearance;

    s_pending_received_mask |=
        (uint16_t)(1u << index);
    s_pending_medication_appearance_mask |=
        (uint16_t)(1u << index);

    APP_LOG(
      APP_LOG_LEVEL_INFO,
      "Medication item %ld complete: shape=%u size=%u imprint=%s",
      (long)index,
      (unsigned int)appearance.shape,
      (unsigned int)appearance.size,
      appearance.imprint
    );
    return;
  }

  if (
    command != SETTINGS_COMMAND_COMMIT ||
    s_pending_received_mask !=
        expected_pending_mask(
          s_pending_count
        ) ||
    s_pending_medication_appearance_mask !=
        expected_pending_mask(
          s_pending_medication_appearance_count
        )
  ) {
    return;
  }

  Tuple *theme_tuple = dict_find(
    iterator,
    MESSAGE_KEY_THEME
  );
  Tuple *language_tuple = dict_find(
    iterator,
    MESSAGE_KEY_LANGUAGE
  );
  Tuple *show_pattern_tuple = dict_find(
    iterator,
    MESSAGE_KEY_SHOW_JAPANESE_PATTERN
  );
  Tuple *audio_volume_tuple = dict_find(
    iterator,
    MESSAGE_KEY_AUDIO_VOLUME
  );
  Tuple *vibration_tuple = dict_find(
    iterator,
    MESSAGE_KEY_VIBRATION_ENABLED
  );
  Tuple *reminder_interval_tuple = dict_find(
    iterator,
    MESSAGE_KEY_REMINDER_INTERVAL
  );
  Tuple *transaction_tuple = dict_find(
    iterator,
    MESSAGE_KEY_SETTINGS_TRANSACTION
  );

  int32_t theme_value;
  int32_t language_value;
  int32_t show_pattern;
  int32_t audio_volume;
  int32_t vibration_enabled;
  int32_t reminder_interval;
  int32_t transaction;
  DaypartSettings dayparts;

  if (
    !tuple_read_int32(
      theme_tuple,
      &theme_value
    ) ||
    theme_value < THEME_MODE_DARK ||
    theme_value > THEME_MODE_LIGHT ||
    !tuple_read_int32(
      language_tuple,
      &language_value
    ) ||
    language_value < APP_LANGUAGE_GERMAN ||
    language_value > APP_LANGUAGE_ENGLISH ||
    !tuple_read_int32(
      show_pattern_tuple,
      &show_pattern
    ) ||
    (show_pattern != 0 && show_pattern != 1) ||
    !read_dayparts_from_message(
      iterator,
      &dayparts
    ) ||
    !tuple_read_int32(
      audio_volume_tuple,
      &audio_volume
    ) ||
    audio_volume < 0 ||
    audio_volume > 100 ||
    !tuple_read_int32(
      vibration_tuple,
      &vibration_enabled
    ) ||
    (vibration_enabled != 0 && vibration_enabled != 1) ||
    !tuple_read_int32(
      reminder_interval_tuple,
      &reminder_interval
    ) ||
    !alarm_reminder_interval_valid(
      reminder_interval
    ) ||
    !tuple_read_int32(
      transaction_tuple,
      &transaction
    ) ||
    transaction <= 0
  ) {
    APP_LOG(
      APP_LOG_LEVEL_WARNING,
      "Incomplete settings commit ignored"
    );
    return;
  }

  apply_theme_mode(
    (ThemeMode)theme_value,
    true
  );
  apply_language(
    (AppLanguage)language_value,
    true
  );

  s_show_japanese_pattern =
      show_pattern == 1;

  apply_daypart_settings(
    &dayparts,
    true
  );
  apply_alarm_settings(
    (uint8_t)audio_volume,
    vibration_enabled == 1,
    (uint8_t)reminder_interval,
    true
  );
  const bool medication_list_saved =
      apply_medication_list(
        s_pending_medications,
        s_pending_count,
        true
      );
  const bool appearances_saved =
      apply_medication_appearances();

  s_settings_ack_transaction =
      (uint32_t)transaction;
  s_settings_storage_verified =
      persist_scalar_settings() &&
      medication_list_saved &&
      appearances_saved;
  s_settings_reset_verified =
      s_settings_storage_verified &&
      alarm_reset_after_settings_save();

  APP_LOG(
    APP_LOG_LEVEL_INFO,
    "Settings transaction %lu received; storage=%d reset=%d",
    (unsigned long)s_settings_ack_transaction,
    s_settings_storage_verified ? 1 : 0,
    s_settings_reset_verified ? 1 : 0
  );

  if (
    s_settings_storage_verified &&
    s_settings_reset_verified
  ) {
    send_settings_ack();
  } else {
    schedule_settings_ack_retry();
  }
}

void watch_settings_init(void) {
  int stored_theme = THEME_MODE_DARK;

  if (persist_exists(THEME_PERSIST_KEY)) {
    stored_theme = persist_read_int(
      THEME_PERSIST_KEY
    );
  }

  if (
    stored_theme < THEME_MODE_DARK ||
    stored_theme > THEME_MODE_LIGHT
  ) {
    stored_theme = THEME_MODE_DARK;
  }

  s_theme_mode = (ThemeMode)stored_theme;

  s_light_theme =
      s_theme_mode == THEME_MODE_LIGHT;

  if (persist_exists(LANGUAGE_PERSIST_KEY)) {
    const int stored_language = persist_read_int(
      LANGUAGE_PERSIST_KEY
    );

    s_language =
        stored_language == APP_LANGUAGE_ENGLISH
            ? APP_LANGUAGE_ENGLISH
            : APP_LANGUAGE_GERMAN;
  } else {
    /*
     * On a fresh installation follow the Pebble system language.
     * German locales use German; all other locales use English.
     * Once the user saves a language choice, the persisted value wins.
     */
    const char *system_locale =
        i18n_get_system_locale();

    s_language =
        system_locale &&
        system_locale[0] == 'd' &&
        system_locale[1] == 'e'
            ? APP_LANGUAGE_GERMAN
            : APP_LANGUAGE_ENGLISH;
  }

  s_show_japanese_pattern =
      !persist_exists(
        SHOW_JAPANESE_PATTERN_PERSIST_KEY
      ) ||
      persist_read_int(
        SHOW_JAPANESE_PATTERN_PERSIST_KEY
      ) != 0;

  load_daypart_settings();
  load_medication_settings();
  load_medication_appearances();
  reset_pending_medications(0);
  reset_pending_medication_appearances(0);

  clear_settings_ack_state();

  app_message_register_inbox_received(
    settings_inbox_received
  );
  app_message_register_outbox_sent(
    settings_outbox_sent
  );
  app_message_register_outbox_failed(
    settings_outbox_failed
  );

  const AppMessageResult result =
      app_message_open(
        SETTINGS_MESSAGE_BUFFER_SIZE,
        SETTINGS_ACK_OUTBOX_SIZE
      );

  if (result != APP_MSG_OK) {
    APP_LOG(
      APP_LOG_LEVEL_ERROR,
      "AppMessage open failed: %d",
      (int)result
    );
  }
}

void watch_settings_deinit(void) {
  cancel_timer(&s_settings_ack_retry_timer);
  app_message_deregister_callbacks();
}
