#pragma once

#include <pebble.h>
#include <stdint.h>

typedef enum {
  NASU_DBG_BOOT = 1,

  NASU_DBG_REFRESH_BEGIN = 10,
  NASU_DBG_ROWS_READY = 11,
  NASU_DBG_PHYSICS_READY = 12,

  NASU_DBG_ALARM_START = 20,
  NASU_DBG_ALARM_CONFIRM = 21,

  NASU_DBG_UI_ALARM_BEGIN = 30,
  NASU_DBG_UI_TO_PILLS = 31,
  NASU_DBG_UI_TO_VESPA = 32,
  NASU_DBG_UI_SETTLED = 33,

  NASU_DBG_CLOSE_SCHEDULE = 40,
  NASU_DBG_CLOSE_FIRE = 41,

  NASU_DBG_CLEAN_EXIT = 90
} NasuDebugEvent;

void debug_log_init(void);
void debug_log_deinit(void);

void debug_log_event(
    NasuDebugEvent event,
    uint16_t a,
    uint16_t b,
    uint16_t c
);

void debug_log_dump(void);
