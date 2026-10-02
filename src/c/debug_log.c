#include "debug_log.h"

#include <string.h>
#include <time.h>

/*
 * Diagnostic storage is intentionally outside Nasu's normal persistence
 * key range.
 *
 * Each event is one small record. We never log physics frames or accel
 * samples, only state transitions.
 */
#define NASU_DEBUG_CURSOR_KEY      0x4E534C00u
#define NASU_DEBUG_SESSION_KEY     0x4E534C01u
#define NASU_DEBUG_SNAPSHOT_KEY    0x4E534C02u
#define NASU_DEBUG_SLOT_KEY_BASE   0x4E534C20u

#define NASU_DEBUG_SLOT_COUNT 32
#define NASU_DEBUG_SNAPSHOT_RECORDS 12
#define NASU_DEBUG_SNAPSHOT_MAGIC 0x4E44u

typedef struct {
  int32_t timestamp;
  uint16_t sequence;
  uint16_t event;
  uint16_t a;
  uint16_t b;
  uint16_t c;
} NasuDebugRecord;

typedef struct {
  uint16_t magic;
  uint16_t count;
  NasuDebugRecord records[
    NASU_DEBUG_SNAPSHOT_RECORDS
  ];
} NasuDebugSnapshot;

static uint16_t s_next_sequence = 1;
static bool s_debug_initialized;

static const char *debug_event_name(
    uint16_t event
) {
  switch ((NasuDebugEvent)event) {
    case NASU_DBG_BOOT:
      return "BOOT";

    case NASU_DBG_REFRESH_BEGIN:
      return "REFRESH_BEGIN";

    case NASU_DBG_ROWS_READY:
      return "ROWS_READY";

    case NASU_DBG_PHYSICS_READY:
      return "PHYSICS_READY";

    case NASU_DBG_ALARM_START:
      return "ALARM_START";

    case NASU_DBG_ALARM_CONFIRM:
      return "ALARM_CONFIRM";

    case NASU_DBG_UI_ALARM_BEGIN:
      return "UI_ALARM_BEGIN";

    case NASU_DBG_UI_TO_PILLS:
      return "UI_TO_PILLS";

    case NASU_DBG_UI_TO_VESPA:
      return "UI_TO_VESPA";

    case NASU_DBG_UI_SETTLED:
      return "UI_SETTLED";

    case NASU_DBG_CLOSE_SCHEDULE:
      return "CLOSE_SCHEDULE";

    case NASU_DBG_CLOSE_FIRE:
      return "CLOSE_FIRE";

    case NASU_DBG_CLEAN_EXIT:
      return "CLEAN_EXIT";

    default:
      return "UNKNOWN";
  }
}

static bool debug_record_before(
    const NasuDebugRecord *a,
    const NasuDebugRecord *b
) {
  if (a->timestamp != b->timestamp) {
    return a->timestamp < b->timestamp;
  }

  return a->sequence < b->sequence;
}

static uint8_t debug_collect_records(
    NasuDebugRecord *records,
    uint8_t capacity
) {
  uint8_t count = 0;

  for (
    uint8_t slot = 0;
    slot < NASU_DEBUG_SLOT_COUNT &&
    count < capacity;
    slot++
  ) {
    const uint32_t key =
        NASU_DEBUG_SLOT_KEY_BASE + slot;

    if (
      !persist_exists(key) ||
      persist_get_size(key) !=
          (int)sizeof(NasuDebugRecord)
    ) {
      continue;
    }

    NasuDebugRecord record;
    memset(&record, 0, sizeof(record));

    if (
      persist_read_data(
        key,
        &record,
        sizeof(record)
      ) != (int)sizeof(record) ||
      record.sequence == 0 ||
      record.event == 0
    ) {
      continue;
    }

    records[count++] = record;
  }

  /* Tiny insertion sort; at most 32 records. */
  for (uint8_t i = 1; i < count; i++) {
    NasuDebugRecord value = records[i];
    uint8_t j = i;

    while (
      j > 0 &&
      debug_record_before(
        &value,
        &records[j - 1]
      )
    ) {
      records[j] = records[j - 1];
      j--;
    }

    records[j] = value;
  }

  return count;
}

static void debug_capture_crash_snapshot(void) {
  NasuDebugRecord records[
    NASU_DEBUG_SLOT_COUNT
  ];

  memset(records, 0, sizeof(records));

  const uint8_t count =
      debug_collect_records(
        records,
        NASU_DEBUG_SLOT_COUNT
      );

  NasuDebugSnapshot snapshot;
  memset(&snapshot, 0, sizeof(snapshot));

  snapshot.magic =
      NASU_DEBUG_SNAPSHOT_MAGIC;

  const uint8_t copy_count =
      count < NASU_DEBUG_SNAPSHOT_RECORDS
          ? count
          : NASU_DEBUG_SNAPSHOT_RECORDS;

  snapshot.count = copy_count;

  const uint8_t first =
      count > copy_count
          ? count - copy_count
          : 0;

  for (
    uint8_t i = 0;
    i < copy_count;
    i++
  ) {
    snapshot.records[i] =
        records[first + i];
  }

  (void)persist_write_data(
    NASU_DEBUG_SNAPSHOT_KEY,
    &snapshot,
    sizeof(snapshot)
  );
}

static void debug_dump_record(
    const char *prefix,
    const NasuDebugRecord *record
) {
  if (!record) {
    return;
  }

  time_t timestamp =
      (time_t)record->timestamp;

  struct tm *local =
      localtime(&timestamp);

  if (local) {
    APP_LOG(
      APP_LOG_LEVEL_INFO,
      "NASUDBG %s %02d:%02d:%02d seq=%u %s a=%04x b=%04x c=%04x",
      prefix,
      local->tm_hour,
      local->tm_min,
      local->tm_sec,
      (unsigned int)record->sequence,
      debug_event_name(record->event),
      (unsigned int)record->a,
      (unsigned int)record->b,
      (unsigned int)record->c
    );
  } else {
    APP_LOG(
      APP_LOG_LEVEL_INFO,
      "NASUDBG %s t=%ld seq=%u %s a=%04x b=%04x c=%04x",
      prefix,
      (long)record->timestamp,
      (unsigned int)record->sequence,
      debug_event_name(record->event),
      (unsigned int)record->a,
      (unsigned int)record->b,
      (unsigned int)record->c
    );
  }
}

void debug_log_init(void) {
  int32_t next_sequence = 1;

  if (persist_exists(NASU_DEBUG_CURSOR_KEY)) {
    next_sequence =
        persist_read_int(
          NASU_DEBUG_CURSOR_KEY
        );
  }

  if (
    next_sequence <= 0 ||
    next_sequence > 65535
  ) {
    next_sequence = 1;
  }

  s_next_sequence =
      (uint16_t)next_sequence;

  /*
   * Session flag still set means the previous app instance never reached
   * debug_log_deinit(): native crash, OS reboot, assert, etc.
   *
   * Preserve its last transitions before normal logging continues.
   */
  if (
    persist_exists(NASU_DEBUG_SESSION_KEY) &&
    persist_read_int(
      NASU_DEBUG_SESSION_KEY
    ) == 1
  ) {
    debug_capture_crash_snapshot();
  }

  (void)persist_write_int(
    NASU_DEBUG_SESSION_KEY,
    1
  );

  s_debug_initialized = true;
}

void debug_log_event(
    NasuDebugEvent event,
    uint16_t a,
    uint16_t b,
    uint16_t c
) {
  if (!s_debug_initialized) {
    return;
  }

  uint16_t sequence =
      s_next_sequence;

  if (sequence == 0) {
    sequence = 1;
  }

  NasuDebugRecord record = {
    .timestamp = (int32_t)time(NULL),
    .sequence = sequence,
    .event = (uint16_t)event,
    .a = a,
    .b = b,
    .c = c
  };

  const uint8_t slot =
      (uint8_t)(
        (sequence - 1) %
        NASU_DEBUG_SLOT_COUNT
      );

  (void)persist_write_data(
    NASU_DEBUG_SLOT_KEY_BASE + slot,
    &record,
    sizeof(record)
  );

  sequence++;

  if (sequence == 0) {
    sequence = 1;
  }

  s_next_sequence = sequence;

  (void)persist_write_int(
    NASU_DEBUG_CURSOR_KEY,
    s_next_sequence
  );
}

void debug_log_dump(void) {
  if (
    persist_exists(
      NASU_DEBUG_SNAPSHOT_KEY
    ) &&
    persist_get_size(
      NASU_DEBUG_SNAPSHOT_KEY
    ) == (int)sizeof(NasuDebugSnapshot)
  ) {
    NasuDebugSnapshot snapshot;
    memset(&snapshot, 0, sizeof(snapshot));

    if (
      persist_read_data(
        NASU_DEBUG_SNAPSHOT_KEY,
        &snapshot,
        sizeof(snapshot)
      ) == (int)sizeof(snapshot) &&
      snapshot.magic ==
          NASU_DEBUG_SNAPSHOT_MAGIC &&
      snapshot.count <=
          NASU_DEBUG_SNAPSHOT_RECORDS
    ) {
      APP_LOG(
        APP_LOG_LEVEL_WARNING,
        "NASUDBG CRASH-SNAPSHOT BEGIN"
      );

      for (
        uint8_t i = 0;
        i < snapshot.count;
        i++
      ) {
        debug_dump_record(
          "CRASH",
          &snapshot.records[i]
        );
      }

      APP_LOG(
        APP_LOG_LEVEL_WARNING,
        "NASUDBG CRASH-SNAPSHOT END"
      );
    }
  }

  NasuDebugRecord records[
    NASU_DEBUG_SLOT_COUNT
  ];

  memset(records, 0, sizeof(records));

  const uint8_t count =
      debug_collect_records(
        records,
        NASU_DEBUG_SLOT_COUNT
      );

  APP_LOG(
    APP_LOG_LEVEL_INFO,
    "NASUDBG CURRENT BEGIN count=%u",
    (unsigned int)count
  );

  for (
    uint8_t i = 0;
    i < count;
    i++
  ) {
    debug_dump_record(
      "LIVE",
      &records[i]
    );
  }

  APP_LOG(
    APP_LOG_LEVEL_INFO,
    "NASUDBG CURRENT END"
  );
}

void debug_log_deinit(void) {
  if (!s_debug_initialized) {
    return;
  }

  debug_log_event(
    NASU_DBG_CLEAN_EXIT,
    0,
    0,
    0
  );

  /*
   * Write this last. If anything crashes before here, the next launch
   * correctly treats the session as abnormal.
   */
  (void)persist_write_int(
    NASU_DEBUG_SESSION_KEY,
    0
  );

  s_debug_initialized = false;
}
