#pragma once

#include "app_types.h"

/* Small shared runtime helpers. */
int32_t abs_int32(int32_t value);
void cancel_timer(AppTimer **timer);
int32_t clamp_symmetric(int32_t value, int32_t limit);
uint32_t current_time_ms(void);
void app_exit_to_watchface(void);
