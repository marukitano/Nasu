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

static AppTimer *s_scroll_physics_timer;

static int16_t scroll_canvas_height(void);
static int scroll_min_snap_index(void);
static int scroll_max_snap_index(void);
static int32_t snap_anchor_for_index(int index);
static int clamp_snap_index(int index);
static void band_animation_tick(
    void *context
);
static int32_t scroll_anchor_q8(int index);
static int32_t scroll_top_limit_q8(void);
static int32_t scroll_bottom_limit_q8(void);
static int32_t magnet_force_between_q8(
    int32_t upper_anchor_q8,
    int32_t lower_anchor_q8,
    int32_t position_q8
);
static int32_t edge_magnet_force_q8(
    int32_t anchor_q8,
    int32_t position_q8
);
static int32_t magnet_force_for_position_q8(
    int32_t position_q8
);
static void apply_scroll_edge_limits(void);
static bool edge_bounce_reached_limit(void);
static void scroll_physics_tick(void *context);
static void schedule_scroll_physics(void);
static void start_scroll_snap(
    int target_index,
    bool keep_velocity
);
static void start_edge_bounce(int direction);

#if defined(PBL_TOUCH)
static int nearest_snap_index_for_position_q8(
    int32_t position_q8
);
static bool touch_reached_virtual_edge(void);
static void touch_begin(const TouchEvent *event);
static void choose_touch_pair(void);
static void clamp_touch_target_to_pair(void);
static bool touch_step_reached_threshold(void);
static void keep_touch_active_after_step(void);
static void touch_update(
    const TouchEvent *event
);
static void touch_end(void);
#endif

static int16_t scroll_canvas_height(void) {
  if (!s_canvas_layer) {
    return 228;
  }
  return layer_get_bounds(s_canvas_layer).size.h;
}

static int scroll_min_snap_index(void) {
  return INTAKE_ROW_COUNT > 0 ? 0 : scroll_vespa_snap_index();
}

static int scroll_max_snap_index(void) {
  if (medication_ui_alarm_navigation_locked()) {
    /*
     * Alarmzustand: Pillenanimation -> Intake und dann Schluss.
     * Vespa sowie die komplette Medikamentenliste liegen außerhalb des
     * erlaubten Scrollbereichs.
     */
    return INTAKE_ROW_COUNT;
  }

  return scroll_vespa_snap_index() + LIST_ROW_COUNT;
}

int scroll_vespa_snap_index(void) {
  return INTAKE_ROW_COUNT + 1;
}

static int scroll_all_medications_first_snap_index(void) {
  return scroll_vespa_snap_index() + 1;
}

int scroll_all_medication_row_for_snap(int snap_index) {
  const int row = snap_index - scroll_all_medications_first_snap_index();
  return row >= 0 && row < LIST_ROW_COUNT ? row : -1;
}

int32_t scroll_vespa_page_top_y(void) {
  const int32_t height = scroll_canvas_height();
  if (INTAKE_ROW_COUNT <= 0) {
    return height;
  }
  return 2 * height +
      (INTAKE_ROW_COUNT - 1) *
      (MEDICATION_ROW_HEIGHT + MEDICATION_ROW_GAP);
}

int32_t scroll_all_medications_page_start_y(void) {
  return scroll_vespa_page_top_y() + scroll_canvas_height();
}

static int clamp_snap_index(int index) {
  const int minimum = scroll_min_snap_index();
  const int maximum = scroll_max_snap_index();
  if (index < minimum) return minimum;
  if (index > maximum) return maximum;
  return index;
}

int32_t scroll_snap_anchor_y(int snap_index) {
  const int index = clamp_snap_index(snap_index);
  const int32_t height = scroll_canvas_height();

  if (index <= 0) {
    return CANVAS_START_OFFSET_Y;
  }

  if (index <= INTAKE_ROW_COUNT) {
    return -height -
        (index - 1) *
        (MEDICATION_ROW_HEIGHT + MEDICATION_ROW_GAP);
  }

  if (index == scroll_vespa_snap_index()) {
    return -scroll_vespa_page_top_y();
  }

  const int row = scroll_all_medication_row_for_snap(index);
  if (row >= 0) {
    return -scroll_all_medications_page_start_y() -
        row * (MEDICATION_ROW_HEIGHT + MEDICATION_ROW_GAP);
  }

  return -scroll_vespa_page_top_y();
}

static int32_t snap_anchor_for_index(int index) {
  return scroll_snap_anchor_y(index);
}

int32_t visual_canvas_offset_y(void) {
  const int32_t rounding =
      s_scroll.position_q8 >= 0
          ? SCROLL_Q8 / 2
          : -SCROLL_Q8 / 2;

  return
      (
        s_scroll.position_q8 +
        rounding
      ) /
      SCROLL_Q8;
}

void set_band_layer_x_q8(
    int32_t x_q8
) {
  if (!s_band_layer || !s_canvas_layer) {
    return;
  }

  GRect frame = layer_get_frame(s_band_layer);
  const int32_t rounded_x =
      x_q8 >= 0 ? x_q8 + SCROLL_Q8 / 2 : x_q8 - SCROLL_Q8 / 2;
  frame.origin.x = (int16_t)(rounded_x / SCROLL_Q8);

  const GRect canvas_bounds = layer_get_bounds(s_canvas_layer);
  frame.origin.y =
      (canvas_bounds.size.h - MEDICATION_ROW_HEIGHT) / 2;
  layer_set_frame(s_band_layer, frame);

  if (s_band_arrow_layer) {
    GRect arrow_frame = layer_get_frame(s_band_arrow_layer);
    arrow_frame.origin.x = frame.origin.x - BAND_ARROW_WIDTH;
    arrow_frame.origin.y = frame.origin.y;
    layer_set_frame(s_band_arrow_layer, arrow_frame);
  }
}

void set_band_and_arrow_hidden(
    bool hidden
) {
  if (s_band_layer) {
    layer_set_hidden(
      s_band_layer,
      hidden
    );
  }

  if (s_band_arrow_layer) {
    layer_set_hidden(
      s_band_arrow_layer,
      hidden
    );
  }
}

static void band_animation_tick(
    void *context
) {
  s_band_animation_timer = NULL;

  if (
    !s_band.animating ||
    !s_band_layer
  ) {
    return;
  }

  int32_t force_q8 =
      (
        (
          s_band.target_x_q8 -
          s_band.x_q8
        ) *
        SCROLL_SNAP_SPRING_NUM
      ) /
      SCROLL_SNAP_SPRING_DEN;

  force_q8 = clamp_symmetric(
    force_q8,
    SCROLL_SNAP_MAX_FORCE_Q8
  );

  s_band.velocity_q8 +=
      force_q8;

  s_band.velocity_q8 =
      (
        s_band.velocity_q8 *
        SCROLL_SNAP_DAMPING_NUM
      ) /
      SCROLL_SNAP_DAMPING_DEN;

  s_band.x_q8 +=
      s_band.velocity_q8;

  const int32_t minimum_x_q8 =
      -BAND_OVERSHOOT_COVER_PX *
      SCROLL_Q8;

  if (s_band.x_q8 < minimum_x_q8) {
    s_band.x_q8 = minimum_x_q8;

    if (s_band.velocity_q8 < 0) {
      s_band.velocity_q8 = 0;
    }
  }

  set_band_layer_x_q8(
    s_band.x_q8
  );

  if (
    abs_int32(
      s_band.target_x_q8 -
      s_band.x_q8
    ) <= SCROLL_STOP_POSITION_Q8 &&
    abs_int32(
      s_band.velocity_q8
    ) <= SCROLL_STOP_VELOCITY_Q8
  ) {
    s_band.x_q8 =
        s_band.target_x_q8;

    s_band.velocity_q8 = 0;
    s_band.animating = false;

    set_band_layer_x_q8(
      s_band.x_q8
    );

    if (!s_band.target_visible) {
      set_band_and_arrow_hidden(
        true
      );
    }

    return;
  }

  layer_mark_dirty(
    s_band_layer
  );

  schedule_band_animation();
}

void schedule_band_animation(void) {
  if (
    s_band_animation_timer ||
    !s_band.animating
  ) {
    return;
  }

  s_band_animation_timer =
      app_timer_register(
        SCROLL_FRAME_MS,
        band_animation_tick,
        NULL
      );
}

void update_band_animation_target(void) {
  if (!s_band_layer || !s_canvas_layer) {
    return;
  }

  if (s_transfer_screen_active) {
    set_band_and_arrow_hidden(true);
    return;
  }

  const int snap = s_scroll.snap_index;
  const bool row_snap =
      (snap >= 1 && snap <= INTAKE_ROW_COUNT) ||
      scroll_all_medication_row_for_snap(snap) >= 0;

  /*
   * Wie im bisherigen Quickscroll: das Seitenband fährt erst herein, wenn
   * die vertikale Feder wirklich am Rastpunkt angekommen ist. Während der
   * Fahrt Vespa -> Intake bleibt der Bildschirm deshalb sauber.
   */
  const bool row_is_settled =
      s_scroll.mode == SCROLL_IDLE &&
      abs_int32(
        s_scroll.position_q8 -
        scroll_anchor_q8(snap)
      ) <= SCROLL_STOP_POSITION_Q8;

  /*
   * Beim Eintritt in eine Liste bleibt das Band bis zum Einrasten
   * draußen und bounced dann herein. Ist es aber bereits sichtbar und
   * wir wechseln nur von einer Listenzeile zur nächsten, bleibt es
   * einfach stehen. Dadurch gibt es keinen unnötigen Out/In-Bounce bei
   * jedem einzelnen Medikament.
   */
  const bool visible =
      row_snap &&
      (
        s_band.target_visible ||
        row_is_settled
      );

  const int32_t target_x_q8 =
      visible ? 0 :
      layer_get_bounds(s_canvas_layer).size.w * SCROLL_Q8;

  if (s_band.target_visible == visible &&
      s_band.target_x_q8 == target_x_q8) {
    return;
  }

  s_band.target_visible = visible;
  s_band.target_x_q8 = target_x_q8;
  s_band.animating = true;
  if (visible) set_band_and_arrow_hidden(false);
  schedule_band_animation();
}

static int32_t scroll_anchor_q8(int index) {
  return
      snap_anchor_for_index(
        clamp_snap_index(index)
      ) *
      SCROLL_Q8;
}

static int32_t scroll_top_limit_q8(void) {
  return
      scroll_anchor_q8(scroll_min_snap_index()) +
      SCROLL_EDGE_HALF_INTERVAL_PX *
      SCROLL_Q8;
}

static int32_t scroll_bottom_limit_q8(void) {
  return
      scroll_anchor_q8(scroll_max_snap_index()) -
      SCROLL_EDGE_HALF_INTERVAL_PX *
      SCROLL_Q8;
}

#if defined(PBL_TOUCH)
static int nearest_snap_index_for_position_q8(
    int32_t position_q8
) {
  const int current =
      clamp_snap_index(
        s_scroll.snap_index
      );

  const int32_t anchor_q8 =
      scroll_anchor_q8(current);

  const int32_t escape_q8 =
      SCROLL_EDGE_HALF_INTERVAL_PX *
      SCROLL_Q8;

  if (
    position_q8 >= anchor_q8 + escape_q8 &&
    current > scroll_min_snap_index()
  ) {
    return current - 1;
  }

  if (
    position_q8 <= anchor_q8 - escape_q8 &&
    current < scroll_max_snap_index()
  ) {
    return current + 1;
  }

  return current;
}
#endif

static int32_t magnet_force_between_q8(
    int32_t upper_anchor_q8,
    int32_t lower_anchor_q8,
    int32_t position_q8
) {
  const int32_t distance_q8 =
      upper_anchor_q8 -
      lower_anchor_q8;

  if (distance_q8 <= 0) {
    return 0;
  }

  const int32_t progress_q8 =
      (
        (
          upper_anchor_q8 -
          position_q8
        ) *
        SCROLL_Q8
      ) /
      distance_q8;

  const int32_t angle =
      (
        progress_q8 *
        TRIG_MAX_ANGLE
      ) /
      SCROLL_Q8;

  const int32_t maximum_force_q8 =
      (
        distance_q8 /
        SCROLL_Q8
      ) *
      SCROLL_MAGNET_ACCEL_PER_PIXEL_Q8;

  return
      (
        maximum_force_q8 *
        sin_lookup(angle)
      ) /
      TRIG_MAX_RATIO;
}

static int32_t edge_magnet_force_q8(
    int32_t anchor_q8,
    int32_t position_q8
) {
  const int32_t delta_q8 =
      position_q8 -
      anchor_q8;

  int32_t progress_q8 =
      (
        abs_int32(delta_q8) *
        SCROLL_Q8
      ) /
      (
        SCROLL_SNAP_REFERENCE_PX *
        SCROLL_Q8
      );

  if (progress_q8 > SCROLL_Q8 / 2) {
    progress_q8 = SCROLL_Q8 / 2;
  }

  const int32_t angle =
      (
        progress_q8 *
        TRIG_MAX_ANGLE
      ) /
      SCROLL_Q8;

  const int32_t magnitude_q8 =
      (
        SCROLL_SNAP_REFERENCE_PX *
        SCROLL_MAGNET_ACCEL_PER_PIXEL_Q8 *
        sin_lookup(angle)
      ) /
      TRIG_MAX_RATIO;

  return
      delta_q8 >= 0
          ? -magnitude_q8
          : magnitude_q8;
}

static int32_t magnet_force_for_position_q8(
    int32_t position_q8
) {
  const int32_t top_anchor_q8 =
      scroll_anchor_q8(
        scroll_min_snap_index()
      );

  const int32_t bottom_anchor_q8 =
      scroll_anchor_q8(
        scroll_max_snap_index()
      );

  if (position_q8 >= top_anchor_q8) {
    return edge_magnet_force_q8(
      top_anchor_q8,
      position_q8
    );
  }

  if (position_q8 <= bottom_anchor_q8) {
    return edge_magnet_force_q8(
      bottom_anchor_q8,
      position_q8
    );
  }

  for (
    int index = scroll_min_snap_index();
    index < scroll_max_snap_index();
    index++
  ) {
    const int32_t upper_anchor_q8 =
        scroll_anchor_q8(index);

    const int32_t lower_anchor_q8 =
        scroll_anchor_q8(index + 1);

    if (
      position_q8 <= upper_anchor_q8 &&
      position_q8 >= lower_anchor_q8
    ) {
      return magnet_force_between_q8(
        upper_anchor_q8,
        lower_anchor_q8,
        position_q8
      );
    }
  }

  return 0;
}

void cancel_scroll_physics(void) {
  cancel_timer(&s_scroll_physics_timer);

  s_scroll.mode = SCROLL_IDLE;
  s_scroll.velocity_q8 = 0;
  s_scroll.breakaway_locked = false;
}

static void apply_scroll_edge_limits(void) {
  const int32_t top_limit_q8 =
      scroll_top_limit_q8();

  const int32_t bottom_limit_q8 =
      scroll_bottom_limit_q8();

  if (s_scroll.position_q8 > top_limit_q8) {
    s_scroll.position_q8 = top_limit_q8;

    if (s_scroll.velocity_q8 > 0) {
      s_scroll.velocity_q8 = 0;
    }
  }

  if (s_scroll.position_q8 < bottom_limit_q8) {
    s_scroll.position_q8 = bottom_limit_q8;

    if (s_scroll.velocity_q8 < 0) {
      s_scroll.velocity_q8 = 0;
    }
  }
}

static bool edge_bounce_reached_limit(void) {
  if (s_scroll.mode != SCROLL_EDGE_BOUNCE) {
    return false;
  }

  return s_scroll.snap_index == scroll_min_snap_index()
      ? s_scroll.position_q8 >= scroll_top_limit_q8()
      : s_scroll.position_q8 <= scroll_bottom_limit_q8();
}

#if defined(PBL_TOUCH)
static bool touch_reached_virtual_edge(void) {
  if (
    s_scroll.mode != SCROLL_TOUCH ||
    !s_touch.pair_selected ||
    s_touch.edge_consumed ||
    s_touch.neighbor_index !=
        s_touch.start_index
  ) {
    return false;
  }

  if (
    s_touch.start_index == scroll_min_snap_index() &&
    s_touch.pair_direction < 0
  ) {
    return
        s_scroll.position_q8 >=
        scroll_top_limit_q8();
  }

  if (
    s_touch.start_index ==
        scroll_max_snap_index() &&
    s_touch.pair_direction > 0
  ) {
    return
        s_scroll.position_q8 <=
        scroll_bottom_limit_q8();
  }

  return false;
}
#endif

static void scroll_physics_tick(void *context) {
  s_scroll_physics_timer = NULL;

  if (s_scroll.mode == SCROLL_IDLE) {
    return;
  }

#if defined(PBL_TOUCH)
  /* Defensive invariant: SCROLL_TOUCH is valid only while a finger is down. */
  if (
    s_scroll.mode == SCROLL_TOUCH &&
    !s_touch.dragging
  ) {
    s_scroll.velocity_q8 = 0;

    start_scroll_snap(
      nearest_snap_index_for_position_q8(
        s_scroll.position_q8
      ),
      false
    );
    return;
  }
#endif

  int32_t force_q8;

  if (s_scroll.mode == SCROLL_TOUCH) {
    force_q8 =
        (
          (
            s_scroll.target_q8 -
            s_scroll.position_q8
          ) *
          SCROLL_FINGER_SPRING_NUM
        ) /
        SCROLL_FINGER_SPRING_DEN;

    if (s_scroll.breakaway_locked) {
      if (
        abs_int32(force_q8) <
        SCROLL_BREAKAWAY_FORCE_Q8
      ) {
        s_scroll.position_q8 =
            s_scroll.breakaway_anchor_q8;

        s_scroll.velocity_q8 = 0;
        schedule_scroll_physics();
        return;
      }

      s_scroll.breakaway_locked = false;
      s_scroll.position_q8 =
          s_scroll.breakaway_anchor_q8;

      s_scroll.velocity_q8 = 0;
    }

    force_q8 +=
        magnet_force_for_position_q8(
          s_scroll.position_q8
        );

    s_scroll.velocity_q8 += force_q8;

    s_scroll.velocity_q8 =
        (
          s_scroll.velocity_q8 *
          SCROLL_FINGER_DAMPING_NUM
        ) /
        SCROLL_FINGER_DAMPING_DEN;
  } else {
    force_q8 =
        (
          (
            s_scroll.target_q8 -
            s_scroll.position_q8
          ) *
          SCROLL_SNAP_SPRING_NUM
        ) /
        SCROLL_SNAP_SPRING_DEN;

    force_q8 =
        clamp_symmetric(
          force_q8,
          SCROLL_SNAP_MAX_FORCE_Q8
        );

    force_q8 +=
        magnet_force_for_position_q8(
          s_scroll.position_q8
        );

    s_scroll.velocity_q8 += force_q8;

    s_scroll.velocity_q8 =
        (
          s_scroll.velocity_q8 *
          SCROLL_SNAP_DAMPING_NUM
        ) /
        SCROLL_SNAP_DAMPING_DEN;
  }

  s_scroll.velocity_q8 =
      clamp_symmetric(
        s_scroll.velocity_q8,
        SCROLL_MAX_VELOCITY_Q8
      );

  s_scroll.position_q8 +=
      s_scroll.velocity_q8;

#if defined(PBL_TOUCH)
  if (touch_step_reached_threshold()) {
    const int direction =
        s_touch.pair_direction;

    step_snap_index(
      direction
    );

    keep_touch_active_after_step();
    mark_scene_dirty();
    return;
  }
#endif

  apply_scroll_edge_limits();

  if (edge_bounce_reached_limit()) {
    start_scroll_snap(
      s_scroll.snap_index,
      false
    );

    mark_scene_dirty();
    return;
  }

#if defined(PBL_TOUCH)
  if (touch_reached_virtual_edge()) {
    s_touch.edge_consumed = true;

    start_scroll_snap(
      s_touch.start_index,
      true
    );

    mark_scene_dirty();
    return;
  }
#endif

  if (
    s_scroll.mode == SCROLL_SNAP &&
    abs_int32(
      s_scroll.target_q8 -
      s_scroll.position_q8
    ) <= SCROLL_STOP_POSITION_Q8 &&
    abs_int32(
      s_scroll.velocity_q8
    ) <= SCROLL_STOP_VELOCITY_Q8
  ) {
    s_scroll.position_q8 =
        s_scroll.target_q8;

    s_scroll.velocity_q8 = 0;
    s_scroll.mode = SCROLL_IDLE;
    medication_ui_scroll_settled();
  }

  mark_scene_dirty();

  if (s_scroll.mode != SCROLL_IDLE) {
    schedule_scroll_physics();
  }
}

static void schedule_scroll_physics(void) {
  if (s_scroll_physics_timer) {
    return;
  }

  s_scroll_physics_timer = app_timer_register(
    SCROLL_FRAME_MS,
    scroll_physics_tick,
    NULL
  );

  if (
    !s_scroll_physics_timer &&
    s_scroll.mode == SCROLL_SNAP
  ) {
    s_scroll.position_q8 =
        s_scroll.target_q8;

    s_scroll.velocity_q8 = 0;
    s_scroll.mode = SCROLL_IDLE;
    medication_ui_scroll_settled();
    mark_scene_dirty();
  }
}

static void start_scroll_snap(
    int target_index,
    bool keep_velocity
) {
  target_index =
      clamp_snap_index(target_index);

#if defined(PBL_TOUCH)
  s_touch.dragging = false;
#endif

  s_scroll.mode = SCROLL_SNAP;
  s_scroll.breakaway_locked = false;
  s_scroll.target_q8 =
      scroll_anchor_q8(target_index);

  if (!keep_velocity) {
    s_scroll.velocity_q8 = 0;
  }

  s_scroll.snap_index = target_index;
  mark_scene_dirty();

  if (
    s_scroll.position_q8 ==
        s_scroll.target_q8 &&
    abs_int32(
      s_scroll.velocity_q8
    ) <= SCROLL_STOP_VELOCITY_Q8
  ) {
    s_scroll.velocity_q8 = 0;
    s_scroll.mode = SCROLL_IDLE;
    medication_ui_scroll_settled();
    return;
  }

  schedule_scroll_physics();
}

void scroll_to_snap_index(int target_index) {
  start_scroll_snap(target_index, false);
}

static void start_edge_bounce(int direction) {
  cancel_timer(&s_scroll_physics_timer);

#if defined(PBL_TOUCH)
  s_touch.dragging = false;
#endif

  s_scroll.mode = SCROLL_EDGE_BOUNCE;
  s_scroll.breakaway_locked = false;
  s_scroll.velocity_q8 = 0;

  s_scroll.target_q8 =
      scroll_anchor_q8(s_scroll.snap_index) +
      (direction < 0 ? 2 : -2) *
          SCROLL_EDGE_HALF_INTERVAL_PX *
          SCROLL_Q8;

  schedule_scroll_physics();
}

bool step_snap_index(int direction) {
  const int current_index =
      s_scroll.snap_index;

  const int next_index =
      clamp_snap_index(
        current_index +
        direction
      );

  /*
   * Auto-Close nur bei einem echten Wechsel Vespa -> Medikamentenliste
   * abbrechen.
   *
   * Hardware: Down-Taste => direction +1.
   * Touch: Finger nach oben => total_delta_y < 0 => direction +1.
   *
   * Finger nach unten auf der Vespa ergibt direction -1 und nur den
   * normalen Edge-Bounce. Dieser Fall darf den Timer NICHT abbrechen.
   */
  if (
    direction > 0 &&
    current_index == scroll_vespa_snap_index() &&
    next_index > current_index
  ) {
    medication_ui_cancel_post_confirmation_close();
  }

  if (next_index == current_index) {
    const bool valid_edge =
        (
          s_scroll.snap_index ==
              scroll_min_snap_index() &&
          direction < 0
        ) ||
        (
          s_scroll.snap_index ==
              scroll_max_snap_index() &&
          direction > 0
        );

    if (valid_edge) {
      start_edge_bounce(direction);
    }

    return valid_edge;
  }

  start_scroll_snap(
    next_index,
    true
  );

  return true;
}

#if defined(PBL_TOUCH)
static void touch_begin(const TouchEvent *event) {
  if (
    s_transfer_screen_active ||
    medication_ui_alarm_transitioning_to_pills() ||
    s_confirmation_state != CONFIRM_IDLE
  ) {
    return;
  }

  cancel_timer(&s_scroll_physics_timer);

  s_touch = (ScrollTouchState) {
    .last_y = event->y,
    .start_time_ms = current_time_ms(),
    .start_index =
        clamp_snap_index(
          s_scroll.snap_index
        ),
    .neighbor_index =
        clamp_snap_index(
          s_scroll.snap_index
        ),
    .dragging = true
  };

  s_scroll.mode = SCROLL_TOUCH;
  s_scroll.target_q8 =
      s_scroll.position_q8;

  s_scroll.breakaway_anchor_q8 =
      scroll_anchor_q8(
        s_touch.start_index
      );

  s_scroll.breakaway_locked =
      abs_int32(
        s_scroll.position_q8 -
        s_scroll.breakaway_anchor_q8
      ) <= SCROLL_Q8;

  s_scroll.velocity_q8 = 0;

  schedule_scroll_physics();
}
#endif

#if defined(PBL_TOUCH)
static void choose_touch_pair(void) {
  if (
    s_touch.pair_selected ||
    abs_int32(
      s_touch.total_delta_y
    ) < SCROLL_BREAKAWAY_PX
  ) {
    return;
  }

  s_touch.pair_direction =
      s_touch.total_delta_y < 0
          ? 1
          : -1;

  s_touch.neighbor_index =
      clamp_snap_index(
        s_touch.start_index +
        s_touch.pair_direction
      );

  s_touch.pair_selected = true;
}
#endif

#if defined(PBL_TOUCH)
static void clamp_touch_target_to_pair(void) {
  if (!s_touch.pair_selected) {
    return;
  }

  const int32_t start_q8 =
      scroll_anchor_q8(
        s_touch.start_index
      );

  if (
    s_touch.neighbor_index !=
    s_touch.start_index
  ) {
    const int32_t neighbor_q8 =
        scroll_anchor_q8(
          s_touch.neighbor_index
        );

    const int32_t upper_q8 =
        start_q8 > neighbor_q8
            ? start_q8
            : neighbor_q8;

    const int32_t lower_q8 =
        start_q8 < neighbor_q8
            ? start_q8
            : neighbor_q8;

    if (s_scroll.target_q8 > upper_q8) {
      s_scroll.target_q8 = upper_q8;
    }

    if (s_scroll.target_q8 < lower_q8) {
      s_scroll.target_q8 = lower_q8;
    }

    return;
  }

  if (
    s_touch.pair_direction > 0 &&
    s_scroll.target_q8 > start_q8
  ) {
    s_scroll.target_q8 = start_q8;
  }

  if (
    s_touch.pair_direction < 0 &&
    s_scroll.target_q8 < start_q8
  ) {
    s_scroll.target_q8 = start_q8;
  }
}
#endif

#if defined(PBL_TOUCH)
static bool touch_step_reached_threshold(void) {
  if (
    s_scroll.mode != SCROLL_TOUCH ||
    !s_touch.dragging ||
    !s_touch.pair_selected ||
    s_touch.neighbor_index ==
        s_touch.start_index
  ) {
    return false;
  }

  const int32_t start_q8 =
      scroll_anchor_q8(
        s_touch.start_index
      );

  const int32_t threshold_q8 =
      start_q8 -
      (
        int32_t
      )s_touch.pair_direction *
      SCROLL_EDGE_HALF_INTERVAL_PX *
      SCROLL_Q8;

  if (s_touch.pair_direction > 0) {
    return
        s_scroll.position_q8 <=
        threshold_q8;
  }

  return
      s_scroll.position_q8 >=
      threshold_q8;
}
#endif

#if defined(PBL_TOUCH)
static void keep_touch_active_after_step(void) {
  /* Das beim Aufsetzen gewählte Rastpunktpaar bleibt gesperrt. */
  const int8_t previous_index =
      s_touch.start_index;

  const int8_t current_index =
      clamp_snap_index(
        s_scroll.snap_index
      );

  s_touch.dragging = true;
  s_touch.edge_consumed = true;
  s_touch.total_delta_y = 0;
  s_touch.start_time_ms =
      current_time_ms();

  s_touch.start_index =
      current_index;

  s_touch.neighbor_index =
      previous_index;

  s_touch.pair_direction =
      previous_index >
          current_index
          ? 1
          : -1;

  s_touch.pair_selected = true;
}
#endif

#if defined(PBL_TOUCH)
static void touch_update(
    const TouchEvent *event
) {
  if (!s_touch.dragging) {
    return;
  }

  const int16_t delta_y =
      event->y -
      s_touch.last_y;

  s_touch.last_y = event->y;

  if (delta_y == 0) {
    return;
  }

  if (s_touch.edge_consumed) {
    /* Einen laufenden Snap nicht durch Fingerbewegung verändern. */
    if (s_scroll.mode != SCROLL_IDLE &&
        s_scroll.mode != SCROLL_TOUCH) {
      return;
    }

    if (s_scroll.mode == SCROLL_IDLE) {
      s_scroll.mode = SCROLL_TOUCH;
      s_scroll.target_q8 =
          s_scroll.position_q8;

      s_scroll.breakaway_anchor_q8 =
          scroll_anchor_q8(
            s_touch.start_index
          );

      s_scroll.breakaway_locked =
          abs_int32(
            s_scroll.position_q8 -
            s_scroll.breakaway_anchor_q8
          ) <= SCROLL_Q8;

      s_scroll.velocity_q8 = 0;
    }

    s_touch.total_delta_y +=
        delta_y;

    s_scroll.target_q8 +=
        (int32_t)delta_y *
        SCROLL_Q8;

    clamp_touch_target_to_pair();
    schedule_scroll_physics();
    return;
  }

  s_touch.total_delta_y +=
      delta_y;

  choose_touch_pair();

  s_scroll.target_q8 +=
      (int32_t)delta_y *
      SCROLL_Q8;

  clamp_touch_target_to_pair();
  schedule_scroll_physics();
}
#endif

#if defined(PBL_TOUCH)
static void touch_end(void) {
  if (!s_touch.dragging) {
    return;
  }

  s_touch.dragging = false;
  s_scroll.breakaway_locked = false;

  if (s_touch.edge_consumed) {
    /*
     * A position update can restart SCROLL_TOUCH after the automatic
     * one-step snap has already finished. Liftoff must never leave that
     * mode running without an active finger, otherwise the 16 ms timer
     * redraws forever and can trip the Pebble watchdog.
     */
    if (s_scroll.mode == SCROLL_TOUCH) {
      s_scroll.velocity_q8 = 0;

      start_scroll_snap(
        nearest_snap_index_for_position_q8(
          s_scroll.position_q8
        ),
        false
      );
    }

    return;
  }

  const bool quick_swipe =
      current_time_ms() -
          s_touch.start_time_ms <=
          SCROLL_QUICK_SWIPE_MAX_MS &&
      abs_int32(
        s_touch.total_delta_y
      ) >= SCROLL_QUICK_SWIPE_MIN_PX;

  if (quick_swipe) {
    const int direction =
        s_touch.total_delta_y < 0
            ? 1
            : -1;

    start_scroll_snap(
      s_scroll.snap_index +
          direction,
      true
    );
    return;
  }

  s_scroll.velocity_q8 = 0;

  start_scroll_snap(
    nearest_snap_index_for_position_q8(
      s_scroll.position_q8
    ),
    false
  );
}
#endif

#if defined(PBL_TOUCH)
void touch_handler(
    const TouchEvent *event,
    void *context
) {
  switch (event->type) {
    case TouchEvent_Touchdown:
      touch_begin(event);
      break;

    case TouchEvent_PositionUpdate:
      touch_update(event);
      break;

    case TouchEvent_Liftoff:
      touch_end();
      break;
  }
}
#endif
