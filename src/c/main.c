#include <pebble.h>

#include "watch_settings.h"
#include "medication_alarm.h"
#include "pill_physics.h"
#include "medication_ui.h"
#include "debug_log.h"

int main(void) {
  debug_log_init();

  debug_log_event(
    NASU_DBG_BOOT,
    (uint16_t)launch_reason(),
    0,
    0
  );

  debug_log_dump();

  watch_settings_init();
  medication_alarm_init();
  pill_physics_init();
  medication_ui_init();

  app_event_loop();

  /*
   * Nasu always returns to the watchface when its UI is finished.
   * Set this as late as possible so Pebble OS receives the intended
   * destination during actual app termination.
   */
  exit_reason_set(
    APP_EXIT_ACTION_PERFORMED_SUCCESSFULLY
  );

  pill_physics_deinit();
  medication_alarm_deinit();
  medication_ui_deinit();
  watch_settings_deinit();
  debug_log_deinit();
}
