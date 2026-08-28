#include "app_util.h"

void cancel_timer(AppTimer **timer) {
  if (!*timer) {
    return;
  }

  app_timer_cancel(*timer);
  *timer = NULL;
}

int32_t abs_int32(int32_t value) {
  return value < 0 ? -value : value;
}

int32_t clamp_symmetric(int32_t value, int32_t limit) {
  if (value > limit) {
    return limit;
  }

  if (value < -limit) {
    return -limit;
  }

  return value;
}

uint32_t current_time_ms(void) {
  time_t seconds;
  uint16_t milliseconds;

  time_ms(
    &seconds,
    &milliseconds
  );

  return
      (uint32_t)seconds * 1000u +
      milliseconds;
}

void app_exit_to_watchface(void) {
  exit_reason_set(
    APP_EXIT_ACTION_PERFORMED_SUCCESSFULLY
  );
  window_stack_pop_all(false);
}
