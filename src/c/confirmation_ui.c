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

static const uint32_t s_impact_vibration_durations[] = {
  50
};

static const VibePattern s_impact_vibration_pattern = {
  .durations = s_impact_vibration_durations,
  .num_segments = ARRAY_LENGTH(s_impact_vibration_durations)
};

#define CONFIRM_IMAGE_WIDTH 200
#define CONFIRM_IMAGE_HEIGHT 228
#define CONFIRM_IMAGE_ROWS_PER_CHUNK 10
#define CONFIRM_IMAGE_CHUNK_BYTES \
  (CONFIRM_IMAGE_WIDTH * CONFIRM_IMAGE_ROWS_PER_CHUNK)

#define CONFIRM_OK_BOUNCE_IN_MS 540
#define CONFIRM_OK_BOUNCE_DOWN_MS 450
#define CONFIRM_OK_RELEASE_SETTLE_Y 52
#define CONFIRM_OK_PROGRESS_MAX 1000
#define CONFIRM_OK_ACCEPT_PULSE_MS 180
#define CONFIRM_OK_SCALE_Q8 256
#define CONFIRM_OK_ACCEPT_EXTRA_SCALE_Q8 64

typedef enum {
  CONFIRM_OK_HIDDEN,
  CONFIRM_OK_BOUNCING_IN,
  CONFIRM_OK_VISIBLE,
  CONFIRM_OK_ACCEPTING,
  CONFIRM_OK_BOUNCING_DOWN
} ConfirmationOkState;

static ResHandle s_confirmation_nasu_resource;
static ResHandle s_confirmation_ok_resource;
static ResHandle s_confirmed_vespa_resource;
static bool s_confirmation_image_active;
static bool s_confirmation_image_error_logged;
static uint8_t
    s_confirmation_image_chunk[CONFIRM_IMAGE_CHUNK_BYTES];

static ConfirmationOkState s_confirmation_ok_state;
static uint16_t s_confirmation_ok_elapsed_ms;
static int16_t s_confirmation_ok_offset_y;
static uint16_t s_confirmation_ok_scale_q8;
static bool s_confirmation_release_pending;

static uint16_t integer_sqrt_u32(uint32_t value) {
  uint32_t remainder = value;
  uint32_t result = 0;
  uint32_t bit = 1u << 30;

  while (bit > remainder) {
    bit >>= 2;
  }

  while (bit != 0) {
    if (remainder >= result + bit) {
      remainder -= result + bit;
      result = (result >> 1) + bit;
    } else {
      result >>= 1;
    }

    bit >>= 2;
  }

  return (uint16_t)result;
}

static void reset_confirmation_image(void) {
  s_confirmation_image_active = false;
  s_confirmation_nasu_resource = NULL;
  s_confirmation_ok_resource = NULL;
  s_confirmation_image_error_logged = false;
  s_confirmation_ok_state = CONFIRM_OK_HIDDEN;
  s_confirmation_ok_elapsed_ms = 0;
  s_confirmation_ok_offset_y = 0;
  s_confirmation_ok_scale_q8 = CONFIRM_OK_SCALE_Q8;
  s_confirmation_release_pending = false;
}

static bool confirmation_resource_valid(
    ResHandle resource,
    const char *name
) {
  if (!resource) {
    APP_LOG(
      APP_LOG_LEVEL_ERROR,
      "Confirmation %s resource missing",
      name
    );
    return false;
  }

  const size_t bytes = resource_size(resource);

  if (
    bytes !=
        CONFIRM_IMAGE_WIDTH *
        CONFIRM_IMAGE_HEIGHT
  ) {
    APP_LOG(
      APP_LOG_LEVEL_ERROR,
      "Confirmation %s wrong size: %lu",
      name,
      (unsigned long)bytes
    );
    return false;
  }

  return true;
}

static bool prepare_confirmation_image(void) {
  s_confirmation_nasu_resource =
      resource_get_handle(
        RESOURCE_ID_RAW_CONFIRM_NASU
      );
  s_confirmation_ok_resource =
      resource_get_handle(
        RESOURCE_ID_RAW_CONFIRM_OK
      );

  if (
    !confirmation_resource_valid(
      s_confirmation_nasu_resource,
      "RAW_CONFIRM_NASU"
    ) ||
    !confirmation_resource_valid(
      s_confirmation_ok_resource,
      "RAW_CONFIRM_OK"
    )
  ) {
    reset_confirmation_image();
    return false;
  }

  s_confirmation_image_active = true;
  s_confirmation_image_error_logged = false;
  s_confirmation_ok_state = CONFIRM_OK_HIDDEN;
  s_confirmation_ok_elapsed_ms = 0;
  s_confirmation_ok_offset_y = 0;
  s_confirmation_ok_scale_q8 = CONFIRM_OK_SCALE_Q8;
  s_confirmation_release_pending = false;
  return true;
}

static void log_confirmation_image_render_error(
    const char *message
) {
  if (s_confirmation_image_error_logged) {
    return;
  }

  s_confirmation_image_error_logged = true;

  APP_LOG(
    APP_LOG_LEVEL_ERROR,
    "%s",
    message
  );
}

static bool draw_streamed_confirmation_image_to_framebuffer(
    uint8_t *framebuffer_data,
    int framebuffer_stride,
    GRect bounds,
    ResHandle resource,
    int16_t offset_y,
    bool clip_to_confirm_radius
) {
  if (
    !framebuffer_data ||
    framebuffer_stride < CONFIRM_IMAGE_WIDTH ||
    !resource
  ) {
    return false;
  }

  const int16_t center_x =
      bounds.size.w +
      CONFIRM_CENTER_OUTSIDE_X;
  const int16_t center_y =
      bounds.size.h / 2;

  const uint32_t radius_squared =
      (uint32_t)s_confirm_radius *
      (uint32_t)s_confirm_radius;

  const int16_t visible_top =
      center_y - s_confirm_radius;
  const int16_t visible_bottom =
      center_y + s_confirm_radius;

  for (
    int16_t block_row = 0;
    block_row < CONFIRM_IMAGE_HEIGHT;
    block_row += CONFIRM_IMAGE_ROWS_PER_CHUNK
  ) {
    const int16_t rows_in_block =
        block_row +
                CONFIRM_IMAGE_ROWS_PER_CHUNK <=
            CONFIRM_IMAGE_HEIGHT
            ? CONFIRM_IMAGE_ROWS_PER_CHUNK
            : CONFIRM_IMAGE_HEIGHT -
                  block_row;

    const int16_t destination_block_top =
        block_row + offset_y;
    const int16_t destination_block_bottom =
        destination_block_top +
        rows_in_block - 1;

    /*
     * Do not even touch the resource when this chunk is completely outside
     * the display. This matters especially while OK is entering/leaving.
     */
    if (
      destination_block_bottom < 0 ||
      destination_block_top >= CONFIRM_IMAGE_HEIGHT
    ) {
      continue;
    }

    /* Same optimisation for the circular Nasu reveal. */
    if (
      clip_to_confirm_radius &&
      (
        s_confirm_radius <= 0 ||
        destination_block_bottom < visible_top ||
        destination_block_top > visible_bottom
      )
    ) {
      continue;
    }

    const size_t block_bytes =
        (size_t)rows_in_block *
        CONFIRM_IMAGE_WIDTH;

    const size_t loaded =
        resource_load_byte_range(
          resource,
          (uint32_t)block_row *
              CONFIRM_IMAGE_WIDTH,
          s_confirmation_image_chunk,
          block_bytes
        );

    if (loaded != block_bytes) {
      log_confirmation_image_render_error(
        "Could not stream confirmation image"
      );
      return false;
    }

    for (
      int16_t local_row = 0;
      local_row < rows_in_block;
      local_row++
    ) {
      const int16_t source_y =
          block_row + local_row;
      const int16_t destination_y =
          source_y + offset_y;

      if (
        destination_y < 0 ||
        destination_y >= CONFIRM_IMAGE_HEIGHT
      ) {
        continue;
      }

      int16_t left = 0;
      int16_t right =
          CONFIRM_IMAGE_WIDTH - 1;

      if (clip_to_confirm_radius) {
        const int32_t dy =
            (int32_t)destination_y -
            center_y;

        const int32_t remaining =
            (int32_t)radius_squared -
            dy * dy;

        if (remaining < 0) {
          continue;
        }

        const int16_t half_width =
            (int16_t)integer_sqrt_u32(
              (uint32_t)remaining
            );

        left = center_x - half_width;
        right = center_x + half_width;

        if (
          right < 0 ||
          left >= CONFIRM_IMAGE_WIDTH
        ) {
          continue;
        }

        if (left < 0) {
          left = 0;
        }

        if (right >= CONFIRM_IMAGE_WIDTH) {
          right =
              CONFIRM_IMAGE_WIDTH - 1;
        }
      }

      uint8_t *destination =
          framebuffer_data +
          destination_y * framebuffer_stride;

      const uint8_t *source =
          s_confirmation_image_chunk +
          local_row * CONFIRM_IMAGE_WIDTH;

      /*
       * 0x00 means transparent in the generated RAW files. Instead of
       * assigning every opaque pixel individually, find continuous opaque
       * runs and copy each run with memcpy().
       */
      int16_t x = left;

      while (x <= right) {
        while (
          x <= right &&
          (source[x] & 0xC0) == 0
        ) {
          x++;
        }

        const int16_t run_start = x;

        while (
          x <= right &&
          (source[x] & 0xC0) != 0
        ) {
          x++;
        }

        if (run_start < x) {
          memcpy(
            destination + run_start,
            source + run_start,
            (size_t)(x - run_start)
          );
        }
      }
    }
  }

  return !s_confirmation_image_error_logged;
}


static bool draw_scaled_confirmation_image_to_framebuffer(
    uint8_t *framebuffer_data,
    int framebuffer_stride,
    ResHandle resource,
    int16_t offset_y,
    uint16_t scale_q8
) {
  if (
    !framebuffer_data ||
    framebuffer_stride < CONFIRM_IMAGE_WIDTH ||
    !resource ||
    scale_q8 < CONFIRM_OK_SCALE_Q8
  ) {
    return false;
  }

  const int32_t scaled_width =
      (CONFIRM_IMAGE_WIDTH * (int32_t)scale_q8 +
       CONFIRM_OK_SCALE_Q8 / 2) /
      CONFIRM_OK_SCALE_Q8;
  const int32_t scaled_height =
      (CONFIRM_IMAGE_HEIGHT * (int32_t)scale_q8 +
       CONFIRM_OK_SCALE_Q8 / 2) /
      CONFIRM_OK_SCALE_Q8;

  const int16_t destination_left =
      (int16_t)((CONFIRM_IMAGE_WIDTH - scaled_width) / 2);
  const int16_t destination_top =
      (int16_t)(offset_y +
      (CONFIRM_IMAGE_HEIGHT - scaled_height) / 2);

  for (
    int16_t block_row = 0;
    block_row < CONFIRM_IMAGE_HEIGHT;
    block_row += CONFIRM_IMAGE_ROWS_PER_CHUNK
  ) {
    const int16_t rows_in_block =
        block_row + CONFIRM_IMAGE_ROWS_PER_CHUNK <= CONFIRM_IMAGE_HEIGHT
            ? CONFIRM_IMAGE_ROWS_PER_CHUNK
            : CONFIRM_IMAGE_HEIGHT - block_row;
    const size_t block_bytes =
        (size_t)rows_in_block * CONFIRM_IMAGE_WIDTH;
    const size_t loaded = resource_load_byte_range(
      resource,
      (uint32_t)block_row * CONFIRM_IMAGE_WIDTH,
      s_confirmation_image_chunk,
      block_bytes
    );

    if (loaded != block_bytes) {
      log_confirmation_image_render_error(
        "Could not stream scaled confirmation image"
      );
      return false;
    }

    for (int16_t local_row = 0; local_row < rows_in_block; local_row++) {
      const int16_t source_y = block_row + local_row;
      int32_t destination_y_start = destination_top +
          ((int32_t)source_y * scaled_height) / CONFIRM_IMAGE_HEIGHT;
      int32_t destination_y_end = destination_top +
          ((int32_t)(source_y + 1) * scaled_height) / CONFIRM_IMAGE_HEIGHT - 1;

      if (destination_y_end < destination_y_start) {
        destination_y_end = destination_y_start;
      }
      if (destination_y_end < 0 || destination_y_start >= CONFIRM_IMAGE_HEIGHT) {
        continue;
      }
      if (destination_y_start < 0) {
        destination_y_start = 0;
      }
      if (destination_y_end >= CONFIRM_IMAGE_HEIGHT) {
        destination_y_end = CONFIRM_IMAGE_HEIGHT - 1;
      }

      const uint8_t *source = s_confirmation_image_chunk +
          local_row * CONFIRM_IMAGE_WIDTH;
      const int16_t visible_left = destination_left < 0 ? 0 : destination_left;
      const int32_t scaled_right = destination_left + scaled_width - 1;
      const int16_t visible_right =
          scaled_right >= CONFIRM_IMAGE_WIDTH
              ? CONFIRM_IMAGE_WIDTH - 1
              : (int16_t)scaled_right;

      if (visible_right < visible_left) {
        continue;
      }

      for (int32_t destination_y = destination_y_start;
           destination_y <= destination_y_end;
           destination_y++) {
        uint8_t *destination = framebuffer_data +
            destination_y * framebuffer_stride;

        for (int16_t destination_x = visible_left;
             destination_x <= visible_right;
             destination_x++) {
          const int32_t local_x = destination_x - destination_left;
          int16_t source_x = (int16_t)(
              (local_x * CONFIRM_IMAGE_WIDTH) / scaled_width
          );

          if (source_x < 0) {
            source_x = 0;
          } else if (source_x >= CONFIRM_IMAGE_WIDTH) {
            source_x = CONFIRM_IMAGE_WIDTH - 1;
          }

          const uint8_t pixel = source[source_x];
          if ((pixel & 0xC0) != 0) {
            destination[destination_x] = pixel;
          }
        }
      }
    }
  }

  return true;
}


static int16_t confirmation_ok_bounce_in_offset(
    uint16_t progress
) {
  if (progress < 720) {
    const uint16_t local =
        (uint16_t)(
          ((uint32_t)progress *
           CONFIRM_OK_PROGRESS_MAX) /
          720
        );

    const int32_t inverse =
        CONFIRM_OK_PROGRESS_MAX - local;
    const int32_t eased =
        CONFIRM_OK_PROGRESS_MAX -
        (inverse * inverse) /
            CONFIRM_OK_PROGRESS_MAX;

    return (int16_t)(
      -CONFIRM_IMAGE_HEIGHT +
      ((CONFIRM_IMAGE_HEIGHT + 10) * eased) /
          CONFIRM_OK_PROGRESS_MAX
    );
  }

  if (progress < 870) {
    const uint16_t local =
        (uint16_t)(
          ((uint32_t)(progress - 720) *
           CONFIRM_OK_PROGRESS_MAX) /
          150
        );

    return (int16_t)(
      10 -
      (16 * local) /
          CONFIRM_OK_PROGRESS_MAX
    );
  }

  const uint16_t local =
      (uint16_t)(
        ((uint32_t)(progress - 870) *
         CONFIRM_OK_PROGRESS_MAX) /
        130
      );

  return (int16_t)(
    -6 +
    (6 * local) /
        CONFIRM_OK_PROGRESS_MAX
  );
}

static int16_t confirmation_ok_bounce_down_offset(
    uint16_t progress
) {
  const int16_t floor_y =
      CONFIRM_IMAGE_HEIGHT - 24;
  const int16_t bounce_up_y =
      CONFIRM_IMAGE_HEIGHT - 56;
  const int16_t exit_y =
      CONFIRM_IMAGE_HEIGHT + 16;

  if (progress < 620) {
    const uint16_t local =
        (uint16_t)(
          ((uint32_t)progress *
           CONFIRM_OK_PROGRESS_MAX) /
          620
        );

    const int32_t inverse =
        CONFIRM_OK_PROGRESS_MAX - local;
    const int32_t eased =
        CONFIRM_OK_PROGRESS_MAX -
        (inverse * inverse) /
            CONFIRM_OK_PROGRESS_MAX;

    return (int16_t)(
      ((int32_t)floor_y * eased) /
      CONFIRM_OK_PROGRESS_MAX
    );
  }

  if (progress < 820) {
    const uint16_t local =
        (uint16_t)(
          ((uint32_t)(progress - 620) *
           CONFIRM_OK_PROGRESS_MAX) /
          200
        );

    return (int16_t)(
      floor_y -
      ((int32_t)(floor_y - bounce_up_y) * local) /
          CONFIRM_OK_PROGRESS_MAX
    );
  }

  const uint16_t local =
      (uint16_t)(
        ((uint32_t)(progress - 820) *
         CONFIRM_OK_PROGRESS_MAX) /
        180
      );

  return (int16_t)(
    bounce_up_y +
    ((int32_t)(exit_y - bounce_up_y) * local) /
        CONFIRM_OK_PROGRESS_MAX
  );
}

static void start_confirmation_ok_bounce_in(void) {
  s_confirmation_ok_state =
      CONFIRM_OK_BOUNCING_IN;
  s_confirmation_ok_elapsed_ms = 0;
  s_confirmation_ok_offset_y =
      -CONFIRM_IMAGE_HEIGHT;
  s_confirmation_ok_scale_q8 = CONFIRM_OK_SCALE_Q8;
}

static void start_confirmation_ok_bounce_down(void) {
  s_confirmation_ok_state =
      CONFIRM_OK_BOUNCING_DOWN;
  s_confirmation_ok_elapsed_ms = 0;
  s_confirmation_ok_offset_y = 0;
  s_confirmation_ok_scale_q8 = CONFIRM_OK_SCALE_Q8;
}

static void start_confirmation_ok_accept_feedback(void) {
  if (s_confirmation_ok_state != CONFIRM_OK_VISIBLE) {
    return;
  }

  s_confirmation_ok_state = CONFIRM_OK_ACCEPTING;
  s_confirmation_ok_elapsed_ms = 0;
  s_confirmation_ok_scale_q8 = CONFIRM_OK_SCALE_Q8;

  vibes_enqueue_custom_pattern(
    s_impact_vibration_pattern
  );
}

static void draw_confirmation_circle(
    GContext *ctx,
    GRect bounds
);
static int16_t current_check_stroke_radius(void);
static void draw_round_line_with_radius(
    GContext *ctx,
    GPoint start,
    GPoint end,
    int16_t radius
);
static void draw_checkmark(
    GContext *ctx,
    GRect bounds,
    int16_t size,
    int16_t radius
);
static void draw_static_checkmark(
    GContext *ctx,
    GRect bounds
);
static void draw_confirmation_checkmark(
    GContext *ctx,
    GRect bounds
);
static bool first_unconfirmed_due_symbol(
    MedicationSymbol *symbol
);
static bool selected_confirmation_symbol(
    MedicationSymbol *symbol
);
static bool confirmation_prompt_is_active(
    MedicationSymbol *symbol
);
static int16_t transfer_lerp_int16(
    int16_t from,
    int16_t to,
    uint16_t progress
);
static GPoint transfer_lerp_point(
    GPoint from,
    GPoint to,
    uint16_t progress
);
static void draw_transfer_round_line(
    GContext *ctx,
    GPoint start,
    GPoint end,
    int16_t radius
);
static uint16_t transfer_progress_for_duration(
    uint16_t duration_ms
);
static int16_t transfer_shaft_bounce_offset(
    uint16_t progress
);
static void draw_transfer_icon(
    GContext *ctx,
    GRect bounds
);
static void confirm_medication_group(
    MedicationSymbol symbol
);
static bool confirmation_animation_active(void);
static void finish_confirmed_release(void);
static void update_confirmation_circle(void);
static void update_checkmark(void);
static void update_confirmation_ok(void);
static void confirmation_timer_callback(void *context);
static void schedule_confirmation_timer(void);
static void exit_app(void);
static void transfer_animation_timer_handler(
    void *context
);
static void schedule_transfer_animation_tick(void);
static void start_transfer_animation(void);
static void transfer_close_timer_handler(
    void *context
);

static void draw_confirmation_circle(
    GContext *ctx,
    GRect bounds
) {
  if (s_confirm_radius <= 0) {
    return;
  }

  graphics_context_set_fill_color(ctx, GColorGreen);
  graphics_fill_circle(
    ctx,
    GPoint(
      bounds.origin.x +
          bounds.size.w +
          CONFIRM_CENTER_OUTSIDE_X,
      bounds.origin.y +
          bounds.size.h / 2
    ),
    (uint16_t)s_confirm_radius
  );
}

static int16_t current_check_stroke_radius(void) {
  const int16_t radius =
      ((int32_t)CHECK_STROKE_RADIUS * s_check_size +
       CHECK_POP_SETTLE_SIZE / 2) /
      CHECK_POP_SETTLE_SIZE;

  return radius < 1 ? 1 : radius;
}

static void draw_round_line_with_radius(
    GContext *ctx,
    GPoint start,
    GPoint end,
    int16_t radius
) {
  const int16_t dx = end.x - start.x;
  const int16_t dy = end.y - start.y;
  const int16_t abs_dx = dx < 0 ? -dx : dx;
  const int16_t abs_dy = dy < 0 ? -dy : dy;
  const int16_t steps = abs_dx > abs_dy ? abs_dx : abs_dy;

  if (steps <= 0) {
    graphics_fill_circle(ctx, start, radius);
    return;
  }

  for (int16_t step = 0; step <= steps; step++) {
    graphics_fill_circle(
      ctx,
      GPoint(
        start.x + ((int32_t)dx * step) / steps,
        start.y + ((int32_t)dy * step) / steps
      ),
      radius
    );
  }
}

static void draw_checkmark(
    GContext *ctx,
    GRect bounds,
    int16_t size,
    int16_t radius
) {
  const int16_t center_x =
      bounds.origin.x +
      bounds.size.w / 2;
  const int16_t center_y =
      bounds.origin.y +
      bounds.size.h / 2;

  const GPoint start = GPoint(
    center_x - (size * 42) / 100,
    center_y
  );

  const GPoint middle = GPoint(
    center_x - (size * 10) / 100,
    center_y + (size * 28) / 100
  );

  const GPoint end = GPoint(
    center_x + (size * 45) / 100,
    center_y - (size * 30) / 100
  );

  graphics_context_set_fill_color(ctx, GColorWhite);
  draw_round_line_with_radius(
    ctx,
    start,
    middle,
    radius
  );
  draw_round_line_with_radius(
    ctx,
    middle,
    end,
    radius
  );
}

static void draw_static_checkmark(
    GContext *ctx,
    GRect bounds
) {
  const int16_t size = CHECK_POP_SETTLE_SIZE;
  const int16_t radius = CHECK_STROKE_RADIUS;
  const int16_t center_x =
      bounds.origin.x +
      bounds.size.w / 2;
  const int16_t center_y =
      bounds.origin.y +
      bounds.size.h / 2;

  const GPoint start = GPoint(
    center_x - (size * 42) / 100,
    center_y
  );
  const GPoint middle = GPoint(
    center_x - (size * 10) / 100,
    center_y + (size * 28) / 100
  );
  const GPoint end = GPoint(
    center_x + (size * 45) / 100,
    center_y - (size * 30) / 100
  );

  /*
   * The animated checkmark is rasterized as many overlapping circles.
   * That is acceptable for a short confirmation animation, but far too
   * expensive for the 16 ms touch-scroll redraw loop. The static page uses
   * two thick strokes and only three circles for rounded joins and caps.
   */
  graphics_context_set_stroke_color(ctx, GColorWhite);
  graphics_context_set_stroke_width(
    ctx,
    (uint8_t)(radius * 2)
  );
  graphics_draw_line(ctx, start, middle);
  graphics_draw_line(ctx, middle, end);

  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_circle(ctx, start, radius);
  graphics_fill_circle(ctx, middle, radius);
  graphics_fill_circle(ctx, end, radius);

  graphics_context_set_stroke_width(ctx, 1);
}

static void draw_confirmation_checkmark(
    GContext *ctx,
    GRect bounds
) {
  if (s_check_state == CHECK_HIDDEN || s_check_size <= 0) {
    return;
  }

  draw_checkmark(
    ctx,
    bounds,
    s_check_size,
    current_check_stroke_radius()
  );
}

void draw_confirmed_page(
    GContext *ctx,
    GRect bounds
) {
  graphics_context_set_fill_color(
    ctx,
    theme_background_color()
  );
  graphics_fill_rect(
    ctx,
    bounds,
    0,
    GCornerNone
  );

  if (!s_confirmed_vespa_resource) {
    s_confirmed_vespa_resource =
        resource_get_handle(
          RESOURCE_ID_RAW_CONFIRMED_VESPA
        );
  }

  if (!s_confirmed_vespa_resource) {
    log_confirmation_image_render_error(
      "Confirmed Vespa RAW resource missing"
    );
    return;
  }

  if (
    resource_size(s_confirmed_vespa_resource) !=
        CONFIRM_IMAGE_WIDTH *
        CONFIRM_IMAGE_HEIGHT
  ) {
    log_confirmation_image_render_error(
      "Confirmed Vespa RAW resource has wrong size"
    );
    return;
  }

  GBitmap *framebuffer =
      graphics_capture_frame_buffer(ctx);

  if (!framebuffer) {
    log_confirmation_image_render_error(
      "Could not capture framebuffer for Vespa"
    );
    return;
  }

  bool framebuffer_ok =
      gbitmap_get_format(framebuffer) ==
          GBitmapFormat8Bit;

  uint8_t *framebuffer_data = NULL;
  int framebuffer_stride = 0;

  if (framebuffer_ok) {
    framebuffer_data =
        gbitmap_get_data(framebuffer);
    framebuffer_stride =
        gbitmap_get_bytes_per_row(framebuffer);

    framebuffer_ok =
        framebuffer_data &&
        framebuffer_stride >=
            CONFIRM_IMAGE_WIDTH;
  }

  if (framebuffer_ok) {
    draw_streamed_confirmation_image_to_framebuffer(
      framebuffer_data,
      framebuffer_stride,
      GRect(0, 0, CONFIRM_IMAGE_WIDTH, CONFIRM_IMAGE_HEIGHT),
      s_confirmed_vespa_resource,
      bounds.origin.y,
      false
    );
  } else {
    log_confirmation_image_render_error(
      "Confirmed Vespa framebuffer unavailable"
    );
  }

  graphics_release_frame_buffer(
    ctx,
    framebuffer
  );
}

static bool first_unconfirmed_due_symbol(
    MedicationSymbol *symbol
) {
  return active_medication_symbol(symbol);
}

static bool selected_confirmation_symbol(
    MedicationSymbol *symbol
) {
  /*
   * Snap 0 ist die Pillenanimation.
   * Snap 1..N sind die fälligen Intake-Zeilen.
   *
   * Während eines Alarms muss die Mitteltaste auf beiden Ebenen dieselbe
   * aktuell fällige Medikamentengruppe bestätigen können.
   */
  if (
    s_scroll.snap_index > INTAKE_ROW_COUNT ||
    !s_intake_symbol_set
  ) {
    return false;
  }

  if (symbol) {
    *symbol = s_intake_symbol;
  }

  return true;
}

static bool confirmation_prompt_is_active(
    MedicationSymbol *symbol
) {
  if (
    s_transfer_screen_active ||
    !selected_confirmation_symbol(symbol) ||
    s_scroll.mode != SCROLL_IDLE ||
    s_band.animating
  ) {
    return false;
  }
  return true;
}

void update_taken_button_hint_pulse(void) {
  if (!confirmation_prompt_is_active(NULL)) {
    s_taken_hint_phase = -1;
    return;
  }

  if (s_taken_hint_phase < 0) {
    s_taken_hint_phase = 0;
  } else if (
    s_taken_hint_phase + 1 <
    (int)ARRAY_LENGTH(s_hint_offsets)
  ) {
    s_taken_hint_phase++;
  }
}

void draw_taken_button_hint(
    GContext *ctx,
    GRect layer_bounds,
    GRect frame,
    GRect canvas_bounds
) {
  if (!confirmation_prompt_is_active(NULL)) {
    return;
  }

  const uint8_t phase =
      s_taken_hint_phase < 0
          ? 0
          : (uint8_t)s_taken_hint_phase;

  const int16_t radius =
      TAKEN_HINT_MIN_RADIUS + s_hint_offsets[phase];

  /* Der Mittelpunkt am Displayrand erzeugt den sichtbaren Halbkreis. */
  const int16_t local_screen_edge_x =
      canvas_bounds.size.w -
      frame.origin.x;

  graphics_context_set_fill_color(
    ctx,
    theme_background_color()
  );

  graphics_fill_circle(
    ctx,
    GPoint(
      local_screen_edge_x,
      layer_bounds.size.h / 2
    ),
    (uint16_t)radius
  );
}

static int16_t transfer_lerp_int16(
    int16_t from,
    int16_t to,
    uint16_t progress
) {
  return (int16_t)(
    from +
    ((int32_t)(to - from) * progress) /
        TRANSFER_PROGRESS_MAX
  );
}

static GPoint transfer_lerp_point(
    GPoint from,
    GPoint to,
    uint16_t progress
) {
  return GPoint(
    transfer_lerp_int16(
      from.x,
      to.x,
      progress
    ),
    transfer_lerp_int16(
      from.y,
      to.y,
      progress
    )
  );
}

static void draw_transfer_round_line(
    GContext *ctx,
    GPoint start,
    GPoint end,
    int16_t radius
) {
  const int16_t dx = end.x - start.x;
  const int16_t dy = end.y - start.y;
  const int16_t abs_dx = dx < 0 ? -dx : dx;
  const int16_t abs_dy = dy < 0 ? -dy : dy;
  const int16_t steps = abs_dx > abs_dy ? abs_dx : abs_dy;

  if (steps <= 0) {
    graphics_fill_circle(ctx, start, (uint16_t)radius);
    return;
  }

  for (int16_t step = 0; step <= steps; step++) {
    graphics_fill_circle(
      ctx,
      GPoint(
        start.x + ((int32_t)dx * step) / steps,
        start.y + ((int32_t)dy * step) / steps
      ),
      (uint16_t)radius
    );
  }
}

static uint16_t transfer_progress_for_duration(
    uint16_t duration_ms
) {
  if (
    duration_ms == 0 ||
    s_transfer_animation_elapsed_ms >= duration_ms
  ) {
    return TRANSFER_PROGRESS_MAX;
  }

  return (uint16_t)(
    ((uint32_t)s_transfer_animation_elapsed_ms *
     TRANSFER_PROGRESS_MAX) /
    duration_ms
  );
}

static int16_t transfer_shaft_bounce_offset(
    uint16_t progress
) {
  if (progress < 760) {
    const uint16_t local_progress =
        (uint16_t)(
          ((uint32_t)progress *
           TRANSFER_PROGRESS_MAX) /
          760
        );
    const int32_t inverse =
        TRANSFER_PROGRESS_MAX -
        local_progress;
    const int32_t eased =
        TRANSFER_PROGRESS_MAX -
        (inverse * inverse) /
            TRANSFER_PROGRESS_MAX;

    return (int16_t)(
      -170 +
      (176 * eased) /
          TRANSFER_PROGRESS_MAX
    );
  }

  if (progress < 890) {
    const uint16_t local_progress =
        (uint16_t)(
          ((uint32_t)(progress - 760) *
           TRANSFER_PROGRESS_MAX) /
          130
        );

    return (int16_t)(
      6 -
      (10 * local_progress) /
          TRANSFER_PROGRESS_MAX
    );
  }

  const uint16_t local_progress =
      (uint16_t)(
        ((uint32_t)(progress - 890) *
         TRANSFER_PROGRESS_MAX) /
        110
      );

  return (int16_t)(
    -4 +
    (4 * local_progress) /
        TRANSFER_PROGRESS_MAX
  );
}

static void draw_transfer_icon(
    GContext *ctx,
    GRect bounds
) {
  const int16_t center_x =
      bounds.size.w / 2;
  const int16_t center_y =
      bounds.size.h / 2;
  const int16_t radius = CHECK_STROKE_RADIUS;
  const int16_t fall_offset =
      s_transfer_animation_state ==
          TRANSFER_ANIMATION_FALLING
          ? s_transfer_fall_offset
          : 0;

  const int16_t check_size =
      CHECK_POP_SETTLE_SIZE;

  const GPoint check_start = GPoint(
    center_x - (check_size * 42) / 100,
    center_y
  );
  const GPoint check_middle = GPoint(
    center_x - (check_size * 10) / 100,
    center_y + (check_size * 28) / 100
  );
  const GPoint check_end = GPoint(
    center_x + (check_size * 45) / 100,
    center_y - (check_size * 30) / 100
  );

  const GPoint arrow_left = GPoint(
    center_x - 24,
    center_y
  );
  const GPoint arrow_tip = GPoint(
    center_x,
    center_y + 28
  );
  const GPoint arrow_right = GPoint(
    center_x + 24,
    center_y
  );

  uint16_t morph_progress =
      TRANSFER_PROGRESS_MAX;

  if (
    s_transfer_animation_state ==
        TRANSFER_ANIMATION_MORPHING
  ) {
    morph_progress =
        transfer_progress_for_duration(
          TRANSFER_MORPH_DURATION_MS
        );
  }

  GPoint left = transfer_lerp_point(
    check_start,
    arrow_left,
    morph_progress
  );
  GPoint tip = transfer_lerp_point(
    check_middle,
    arrow_tip,
    morph_progress
  );
  GPoint right = transfer_lerp_point(
    check_end,
    arrow_right,
    morph_progress
  );

  left.y += fall_offset;
  tip.y += fall_offset;
  right.y += fall_offset;

  graphics_context_set_fill_color(
    ctx,
    GColorWhite
  );

  draw_transfer_round_line(
    ctx,
    left,
    tip,
    radius
  );
  draw_transfer_round_line(
    ctx,
    tip,
    right,
    radius
  );

  if (
    s_transfer_animation_state ==
        TRANSFER_ANIMATION_MORPHING
  ) {
    return;
  }

  uint16_t drop_progress =
      TRANSFER_PROGRESS_MAX;

  if (
    s_transfer_animation_state ==
        TRANSFER_ANIMATION_SHAFT_DROP
  ) {
    drop_progress =
        transfer_progress_for_duration(
          TRANSFER_SHAFT_DURATION_MS
        );
  }

  const int16_t shaft_offset =
      transfer_shaft_bounce_offset(
        drop_progress
      );

  const GPoint shaft_top = GPoint(
    center_x,
    center_y - 48 +
        shaft_offset +
        fall_offset
  );
  const GPoint shaft_bottom = GPoint(
    center_x,
    center_y + 26 +
        shaft_offset +
        fall_offset
  );

  draw_transfer_round_line(
    ctx,
    shaft_top,
    shaft_bottom,
    radius
  );

  const int16_t base_half_width =
      (int16_t)(
        (30 * drop_progress) /
        TRANSFER_PROGRESS_MAX
      );

  const GPoint base_left = GPoint(
    center_x - base_half_width,
    center_y + 49 + fall_offset
  );
  const GPoint base_right = GPoint(
    center_x + base_half_width,
    center_y + 49 + fall_offset
  );

  draw_transfer_round_line(
    ctx,
    base_left,
    base_right,
    radius
  );
}

void confirmation_update_proc(
    Layer *layer,
    GContext *ctx
) {
  const GRect bounds =
      layer_get_bounds(layer);

  if (s_transfer_screen_active) {
    graphics_context_set_fill_color(
      ctx,
      GColorGreen
    );
    graphics_fill_rect(
      ctx,
      bounds,
      0,
      GCornerNone
    );
    draw_transfer_icon(ctx, bounds);
    return;
  }

  if (s_confirmation_image_active) {
    if (
      bounds.origin.x != 0 ||
      bounds.origin.y != 0 ||
      bounds.size.w != CONFIRM_IMAGE_WIDTH ||
      bounds.size.h != CONFIRM_IMAGE_HEIGHT
    ) {
      log_confirmation_image_render_error(
        "Confirmation layer geometry unexpected"
      );
      return;
    }

    GBitmap *framebuffer =
        graphics_capture_frame_buffer(ctx);

    if (!framebuffer) {
      log_confirmation_image_render_error(
        "Could not capture framebuffer"
      );
      return;
    }

    bool framebuffer_ok = true;

    if (
      gbitmap_get_format(framebuffer) !=
          GBitmapFormat8Bit
    ) {
      log_confirmation_image_render_error(
        "Framebuffer is not 8-bit"
      );
      framebuffer_ok = false;
    }

    uint8_t *framebuffer_data = NULL;
    int framebuffer_stride = 0;

    if (framebuffer_ok) {
      framebuffer_data =
          gbitmap_get_data(framebuffer);
      framebuffer_stride =
          gbitmap_get_bytes_per_row(framebuffer);

      if (
        !framebuffer_data ||
        framebuffer_stride <
            CONFIRM_IMAGE_WIDTH
      ) {
        log_confirmation_image_render_error(
          "Framebuffer data unavailable"
        );
        framebuffer_ok = false;
      }
    }

    if (
      framebuffer_ok &&
      s_confirmation_ok_state !=
          CONFIRM_OK_HIDDEN
    ) {
      if (
        s_confirmation_ok_scale_q8 ==
            CONFIRM_OK_SCALE_Q8
      ) {
        draw_streamed_confirmation_image_to_framebuffer(
          framebuffer_data,
          framebuffer_stride,
          bounds,
          s_confirmation_ok_resource,
          s_confirmation_ok_offset_y,
          false
        );
      } else {
        draw_scaled_confirmation_image_to_framebuffer(
          framebuffer_data,
          framebuffer_stride,
          s_confirmation_ok_resource,
          s_confirmation_ok_offset_y,
          s_confirmation_ok_scale_q8
        );
      }
    }

    graphics_release_frame_buffer(
      ctx,
      framebuffer
    );
    return;
  }

  draw_confirmation_circle(
    ctx,
    bounds
  );

  draw_confirmation_checkmark(
    ctx,
    bounds
  );
}

static void confirm_medication_group(
    MedicationSymbol symbol
) {
  mark_medication_group_confirmed(symbol);
  alarm_confirmation_received(symbol);

  /*
   * TODO: Den späteren Wiederholungs-Wakeup nur
   * für diese Gruppe abbrechen:
   * Tabletten oder Pen.
   */
}

static void finish_confirmed_release(void) {
  if (s_confirmation_image_active) {
    reset_confirmation_image();
    s_confirm_radius = 0;
    s_confirmation_state = CONFIRM_IDLE;
    s_confirmation_symbol_set = false;
    s_check_size = 0;
    s_check_state = CHECK_HIDDEN;

    if (s_confirmation_layer) {
      layer_mark_dirty(s_confirmation_layer);
    }

    medication_ui_return_to_vespa_after_confirmation();
    return;
  }

  if (unconfirmed_medication_group_is_due()) {
    reset_confirmation_image();
    refresh_app_screen_state();
    return;
  }

  if (s_confirmation_symbol_set) {
    exit_app();
    return;
  }

  refresh_app_screen_state();
}

static bool confirmation_animation_active(void) {
  return
      s_confirmation_state == CONFIRM_GROWING ||
      s_confirmation_state == CONFIRM_SHRINKING ||
      s_check_state == CHECK_POPPING_OUT ||
      s_check_state == CHECK_AT_PEAK ||
      s_check_state == CHECK_SETTLING ||
      s_confirmation_ok_state == CONFIRM_OK_BOUNCING_IN ||
      s_confirmation_ok_state == CONFIRM_OK_ACCEPTING ||
      s_confirmation_ok_state == CONFIRM_OK_BOUNCING_DOWN;
}

static void update_confirmation_circle(void) {
  if (s_confirmation_state == CONFIRM_GROWING) {
    s_confirm_radius += CONFIRM_GROW_STEP;

    if (s_confirm_radius < s_confirm_max_radius) {
      return;
    }

    s_confirm_radius = s_confirm_max_radius;
    s_confirmation_state = CONFIRM_COMPLETE;

    if (s_confirmation_image_active) {
      s_check_size = 0;
      s_check_state = CHECK_HIDDEN;

      if (
        s_confirmation_ok_state ==
            CONFIRM_OK_VISIBLE
      ) {
        start_confirmation_ok_accept_feedback();
      }
    } else {
      s_check_size = 8;
      s_check_state = CHECK_POPPING_OUT;
    }

    cancel_timer(&s_ui_timer);

    if (s_confirmation_symbol_set) {
      confirm_medication_group(
        s_confirmation_symbol
      );
    }

    return;
  }

  if (s_confirmation_state != CONFIRM_SHRINKING) {
    return;
  }

  int16_t shrink_step =
      s_confirm_radius / CONFIRM_SHRINK_DIVISOR;

  if (shrink_step < CONFIRM_SHRINK_MIN_STEP) {
    shrink_step = CONFIRM_SHRINK_MIN_STEP;
  }

  s_confirm_radius -= shrink_step;

  if (s_confirm_radius <= 0) {
    s_confirm_radius = 0;
    s_confirmation_state = CONFIRM_IDLE;
    s_confirmation_symbol_set = false;
    reset_confirmation_image();
  }
}

static void update_checkmark(void) {
  switch (s_check_state) {
    case CHECK_POPPING_OUT:
      s_check_size += CHECK_POP_GROW_STEP;

      if (s_check_size >= CHECK_POP_OVERSHOOT_SIZE) {
        s_check_size = CHECK_POP_OVERSHOOT_SIZE;
        s_check_state = CHECK_AT_PEAK;
      }
      break;

    case CHECK_AT_PEAK:
      vibes_enqueue_custom_pattern(
        s_impact_vibration_pattern
      );
      s_check_state = CHECK_SETTLING;
      break;

    case CHECK_SETTLING:
      s_check_size -= CHECK_POP_SHRINK_STEP;

      if (s_check_size <= CHECK_POP_SETTLE_SIZE) {
        s_check_size = CHECK_POP_SETTLE_SIZE;
        s_check_state = CHECK_VISIBLE;
      }
      break;

    case CHECK_HIDDEN:
    case CHECK_VISIBLE:
      break;
  }
}

static void update_confirmation_ok(void) {
  if (
    s_confirmation_ok_state != CONFIRM_OK_BOUNCING_IN &&
    s_confirmation_ok_state != CONFIRM_OK_ACCEPTING &&
    s_confirmation_ok_state != CONFIRM_OK_BOUNCING_DOWN
  ) {
    return;
  }

  s_confirmation_ok_elapsed_ms += CONFIRM_ANIMATION_INTERVAL_MS;

  if (s_confirmation_ok_state == CONFIRM_OK_BOUNCING_IN) {
    uint16_t progress = CONFIRM_OK_PROGRESS_MAX;

    if (s_confirmation_ok_elapsed_ms < CONFIRM_OK_BOUNCE_IN_MS) {
      progress = (uint16_t)(
        ((uint32_t)s_confirmation_ok_elapsed_ms * CONFIRM_OK_PROGRESS_MAX) /
        CONFIRM_OK_BOUNCE_IN_MS
      );
    }

    s_confirmation_ok_offset_y = confirmation_ok_bounce_in_offset(progress);

    if (progress >= CONFIRM_OK_PROGRESS_MAX) {
      s_confirmation_ok_offset_y = 0;
      s_confirmation_ok_state = CONFIRM_OK_VISIBLE;
      s_confirmation_ok_elapsed_ms = 0;
      s_confirmation_ok_scale_q8 = CONFIRM_OK_SCALE_Q8;

      if (s_confirmation_state == CONFIRM_COMPLETE) {
        start_confirmation_ok_accept_feedback();
      } else if (s_confirmation_release_pending) {
        start_confirmation_ok_bounce_down();
      }
    }

    return;
  }

  if (s_confirmation_ok_state == CONFIRM_OK_ACCEPTING) {
    uint16_t progress = CONFIRM_OK_PROGRESS_MAX;

    if (s_confirmation_ok_elapsed_ms < CONFIRM_OK_ACCEPT_PULSE_MS) {
      progress = (uint16_t)(
        ((uint32_t)s_confirmation_ok_elapsed_ms * CONFIRM_OK_PROGRESS_MAX) /
        CONFIRM_OK_ACCEPT_PULSE_MS
      );
    }

    uint16_t extra_scale;
    if (progress <= 500) {
      extra_scale = (uint16_t)(
        ((uint32_t)CONFIRM_OK_ACCEPT_EXTRA_SCALE_Q8 * progress) / 500
      );
    } else {
      extra_scale = (uint16_t)(
        ((uint32_t)CONFIRM_OK_ACCEPT_EXTRA_SCALE_Q8 *
         (CONFIRM_OK_PROGRESS_MAX - progress)) / 500
      );
    }

    s_confirmation_ok_scale_q8 = CONFIRM_OK_SCALE_Q8 + extra_scale;

    if (progress >= CONFIRM_OK_PROGRESS_MAX) {
      s_confirmation_ok_scale_q8 = CONFIRM_OK_SCALE_Q8;
      s_confirmation_ok_state = CONFIRM_OK_VISIBLE;
      s_confirmation_ok_elapsed_ms = 0;

      if (s_confirmation_release_pending) {
        start_confirmation_ok_bounce_down();
      }
    }

    return;
  }

  uint16_t progress = CONFIRM_OK_PROGRESS_MAX;

  if (s_confirmation_ok_elapsed_ms < CONFIRM_OK_BOUNCE_DOWN_MS) {
    progress = (uint16_t)(
      ((uint32_t)s_confirmation_ok_elapsed_ms * CONFIRM_OK_PROGRESS_MAX) /
      CONFIRM_OK_BOUNCE_DOWN_MS
    );
  }

  s_confirmation_ok_offset_y = confirmation_ok_bounce_down_offset(progress);

  if (progress >= CONFIRM_OK_PROGRESS_MAX) {
    s_confirmation_ok_offset_y = CONFIRM_IMAGE_HEIGHT + 16;
    s_confirmation_ok_state = CONFIRM_OK_HIDDEN;
    s_confirmation_ok_elapsed_ms = 0;
    s_confirmation_ok_scale_q8 = CONFIRM_OK_SCALE_Q8;
    s_confirmation_release_pending = false;
    finish_confirmed_release();
  }
}

static void confirmation_timer_callback(void *context) {
  s_confirmation_timer = NULL;

  update_confirmation_circle();
  update_checkmark();
  update_confirmation_ok();

  if (s_confirmation_layer) {
    layer_mark_dirty(s_confirmation_layer);
  }

  if (confirmation_animation_active()) {
    schedule_confirmation_timer();
  }
}

static void schedule_confirmation_timer(void) {
  if (s_confirmation_timer) {
    return;
  }

  s_confirmation_timer = app_timer_register(
    CONFIRM_ANIMATION_INTERVAL_MS,
    confirmation_timer_callback,
    NULL
  );
}

void select_button_down(
    ClickRecognizerRef recognizer,
    void *context
) {
  MedicationSymbol symbol;

  if (
    s_confirmation_state != CONFIRM_IDLE ||
    !confirmation_prompt_is_active(&symbol)
  ) {
    return;
  }

  prepare_confirmation_image();

  if (s_confirmation_image_active) {
    start_confirmation_ok_bounce_in();
  }

  s_confirmation_symbol = symbol;
  s_confirmation_symbol_set = true;
  s_confirmation_state = CONFIRM_GROWING;
  s_check_size = 0;
  s_check_state = CHECK_HIDDEN;
  schedule_confirmation_timer();
}

static void exit_app(void) {
  cancel_timer(&s_transfer_close_timer);
  cancel_timer(&s_transfer_animation_timer);
  reset_confirmation_image();
  s_transfer_screen_active = false;
  s_transfer_animation_state =
      TRANSFER_ANIMATION_IDLE;
  alarm_stop();
  window_stack_pop_all(true);
}

static void transfer_animation_timer_handler(
    void *context
) {
  (void)context;
  s_transfer_animation_timer = NULL;

  if (!s_transfer_screen_active) {
    return;
  }

  s_transfer_animation_elapsed_ms +=
      TRANSFER_ANIMATION_INTERVAL_MS;

  if (
    s_transfer_animation_state ==
        TRANSFER_ANIMATION_MORPHING &&
    s_transfer_animation_elapsed_ms >=
        TRANSFER_MORPH_DURATION_MS
  ) {
    s_transfer_animation_state =
        TRANSFER_ANIMATION_SHAFT_DROP;
    s_transfer_animation_elapsed_ms = 0;
  } else if (
    s_transfer_animation_state ==
        TRANSFER_ANIMATION_SHAFT_DROP &&
    s_transfer_animation_elapsed_ms >=
        TRANSFER_SHAFT_DURATION_MS
  ) {
    s_transfer_animation_state =
        TRANSFER_ANIMATION_READY;
    s_transfer_animation_elapsed_ms = 0;
  } else if (
    s_transfer_animation_state ==
        TRANSFER_ANIMATION_FALLING
  ) {
    uint16_t progress =
        transfer_progress_for_duration(
          TRANSFER_FALL_DURATION_MS
        );

    const int32_t eased =
        ((int32_t)progress * progress) /
        TRANSFER_PROGRESS_MAX;

    int16_t travel = 320;

    if (s_confirmation_layer) {
      travel =
          layer_get_bounds(
            s_confirmation_layer
          ).size.h + 120;
    }

    s_transfer_fall_offset =
        (int16_t)(
          ((int32_t)travel * eased) /
          TRANSFER_PROGRESS_MAX
        );

    if (progress >= TRANSFER_PROGRESS_MAX) {
      APP_LOG(
        APP_LOG_LEVEL_INFO,
        "Settings transfer complete: icon left screen"
      );
      exit_app();
      return;
    }
  }

  if (s_confirmation_layer) {
    layer_mark_dirty(
      s_confirmation_layer
    );
  }

  if (
    s_transfer_animation_state ==
        TRANSFER_ANIMATION_MORPHING ||
    s_transfer_animation_state ==
        TRANSFER_ANIMATION_SHAFT_DROP ||
    s_transfer_animation_state ==
        TRANSFER_ANIMATION_FALLING
  ) {
    schedule_transfer_animation_tick();
  }
}

static void schedule_transfer_animation_tick(void) {
  if (
    s_transfer_animation_timer ||
    !s_transfer_screen_active
  ) {
    return;
  }

  s_transfer_animation_timer = app_timer_register(
    TRANSFER_ANIMATION_INTERVAL_MS,
    transfer_animation_timer_handler,
    NULL
  );

  if (!s_transfer_animation_timer) {
    APP_LOG(
      APP_LOG_LEVEL_ERROR,
      "Could not schedule transfer animation"
    );
  }
}

static void start_transfer_animation(void) {
  cancel_timer(&s_transfer_animation_timer);

  s_transfer_animation_state =
      TRANSFER_ANIMATION_MORPHING;
  s_transfer_animation_elapsed_ms = 0;
  s_transfer_fall_offset = 0;

  if (s_confirmation_layer) {
    layer_mark_dirty(
      s_confirmation_layer
    );
  }

  schedule_transfer_animation_tick();
}

static void transfer_close_timer_handler(
    void *context
) {
  (void)context;
  s_transfer_close_timer = NULL;

  if (!s_transfer_screen_active) {
    return;
  }

  APP_LOG(
    APP_LOG_LEVEL_INFO,
    "Settings transfer complete: dropping icon"
  );

  cancel_timer(&s_transfer_animation_timer);
  s_transfer_animation_state =
      TRANSFER_ANIMATION_FALLING;
  s_transfer_animation_elapsed_ms = 0;
  s_transfer_fall_offset = 0;
  schedule_transfer_animation_tick();
}

void schedule_transfer_close(void) {
  cancel_timer(&s_transfer_close_timer);

  if (!s_transfer_screen_active) {
    return;
  }

  s_transfer_close_timer = app_timer_register(
    TRANSFER_CLOSE_DELAY_MS,
    transfer_close_timer_handler,
    NULL
  );

  if (!s_transfer_close_timer) {
    APP_LOG(
      APP_LOG_LEVEL_ERROR,
      "Could not schedule transfer close"
    );
  }
}

void select_button_up(
    ClickRecognizerRef recognizer,
    void *context
) {
  if (
    s_confirmed_screen_active &&
    !s_transfer_screen_active
  ) {
    return;
  }

  if (s_confirmation_state == CONFIRM_COMPLETE) {
    if (s_confirmation_image_active) {
      s_confirmation_release_pending = true;

      if (
        s_confirmation_ok_state ==
            CONFIRM_OK_VISIBLE
      ) {
        start_confirmation_ok_bounce_down();
      }

      schedule_confirmation_timer();
      return;
    }

    finish_confirmed_release();
    return;
  }

  if (s_confirm_radius <= 0) {
    s_confirmation_state = CONFIRM_IDLE;
    s_confirmation_symbol_set = false;
    return;
  }

  s_confirmation_state = CONFIRM_SHRINKING;
  schedule_confirmation_timer();
}

void back_button_handler(
    ClickRecognizerRef recognizer,
    void *context
) {
  exit_app();
}

void show_transfer_screen(void) {
  cancel_timer(&s_transfer_close_timer);
  alarm_stop();

  s_transfer_screen_active = true;
  s_confirmed_screen_active = false;

  if (s_confirmation_layer) {
    GRect frame =
        layer_get_frame(s_confirmation_layer);
    frame.origin.x = 0;
    frame.origin.y = 0;
    layer_set_frame(
      s_confirmation_layer,
      frame
    );
  }

  pill_physics_update_activity();

  cancel_timer(&s_ui_timer);
  cancel_timer(&s_confirmation_timer);
  cancel_timer(&s_band_animation_timer);
  cancel_scroll_physics();

#if defined(PBL_TOUCH)
  s_touch.dragging = false;
#endif

  if (s_canvas_layer) {
    layer_set_hidden(
      s_canvas_layer,
      true
    );
  }

  set_band_and_arrow_hidden(true);

  s_confirmation_state = CONFIRM_IDLE;
  s_confirmation_symbol_set = false;
  s_confirm_radius = 0;
  s_check_size = 0;
  s_check_state = CHECK_HIDDEN;

  start_transfer_animation();

  if (s_confirmation_layer) {
    layer_set_hidden(
      s_confirmation_layer,
      false
    );
    layer_mark_dirty(
      s_confirmation_layer
    );
  }

  APP_LOG(
    APP_LOG_LEVEL_INFO,
    "App screen: transfer"
  );
}
