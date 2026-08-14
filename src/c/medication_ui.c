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

static GColor theme_foreground_color(void);
static bool scroll_input_allowed(void);

static GBitmap *s_alert_pattern_dark_bitmap;
static GBitmap *s_alert_pattern_light_bitmap;
static void apply_effective_theme(bool light_theme);
static void draw_alert_background_pattern(
    GContext *ctx,
    GRect bounds
);
static void draw_swiss_emblem(
    GContext *ctx,
    int32_t scroll_offset_y
);
static void destroy_pill_bitmaps(void);
static void canvas_update_proc(
    Layer *layer,
    GContext *ctx
);
static void cover_scrolled_alert_area(
    GContext *ctx,
    GRect bounds
);
static void draw_alert_page_button_hint(
    GContext *ctx,
    GRect bounds
);
static void band_arrow_update_proc(
    Layer *layer,
    GContext *ctx
);
static void band_update_proc(
    Layer *layer,
    GContext *ctx
);
static void sync_medication_marquee(
    bool advance
);
static void ui_timer_callback(void *context);
static void start_ui_timer(void);
static void scroll_up_handler(
    ClickRecognizerRef recognizer,
    void *context
);
static void scroll_down_handler(
    ClickRecognizerRef recognizer,
    void *context
);
static void click_config_provider(void *context);
static bool load_pill_sheet(void);
static void reset_ui_state(GRect bounds);
static void window_load(Window *window);
static void window_appear(Window *window);
static void window_disappear(Window *window);
static void window_unload(Window *window);

GColor theme_background_color(void) {
  return s_light_theme
      ? GColorWhite
      : GColorBlack;
}

static GColor theme_foreground_color(void) {
  return s_light_theme
      ? GColorBlack
      : GColorWhite;
}

static bool scroll_input_allowed(void) {
  return
      !s_transfer_screen_active &&
      s_confirmation_state == CONFIRM_IDLE;
}

void mark_scene_dirty(void) {
  update_band_animation_target();
  sync_medication_marquee(false);

  if (s_canvas_layer) {
    layer_mark_dirty(s_canvas_layer);
  }

  if (s_band_layer && !layer_get_hidden(s_band_layer)) {
    layer_mark_dirty(s_band_layer);
  }

  if (s_band_arrow_layer && !layer_get_hidden(s_band_arrow_layer)) {
    layer_mark_dirty(s_band_arrow_layer);
  }
}

static void apply_effective_theme(bool light_theme) {
  s_light_theme = light_theme;

  if (s_window) {
    window_set_background_color(
      s_window,
      theme_background_color()
    );
  }

  mark_scene_dirty();

  if (s_confirmation_layer) {
    layer_mark_dirty(
      s_confirmation_layer
    );
  }
}

void apply_theme_mode(
    ThemeMode mode,
    bool save
) {
  if (
    mode > THEME_MODE_LIGHT
  ) {
    mode = THEME_MODE_DARK;
  }

  s_theme_mode = mode;

  apply_effective_theme(
    mode == THEME_MODE_LIGHT
  );

  if (save) {
    persist_write_int(
      THEME_PERSIST_KEY,
      (int)s_theme_mode
    );
  }

  pill_physics_update_activity();
}

void apply_theme(
    bool light_theme,
    bool save
) {
  apply_theme_mode(
    light_theme
        ? THEME_MODE_LIGHT
        : THEME_MODE_DARK,
    save
  );
}

static void draw_alert_background_pattern(
    GContext *ctx,
    GRect bounds
) {
  if (
    !s_show_japanese_pattern ||
    s_confirmed_screen_active ||
    s_transfer_screen_active
  ) {
    return;
  }

  const int32_t page_top =
      bounds.origin.y +
      visual_canvas_offset_y();
  const int32_t page_bottom =
      page_top + bounds.size.h;
  const int32_t screen_bottom =
      bounds.origin.y + bounds.size.h;

  if (
    page_bottom <= bounds.origin.y ||
    page_top >= screen_bottom
  ) {
    return;
  }

  GBitmap *pattern_bitmap =
      s_light_theme
          ? s_alert_pattern_light_bitmap
          : s_alert_pattern_dark_bitmap;

  if (!pattern_bitmap) {
    return;
  }

  /*
   * One opaque full-screen bitmap blit only. No alpha compositing and
   * no tiled bitmap loops during scrolling/animation.
   */
  graphics_context_set_compositing_mode(
    ctx,
    GCompOpAssign
  );
  graphics_draw_bitmap_in_rect(
    ctx,
    pattern_bitmap,
    GRect(
      bounds.origin.x,
      (int16_t)page_top,
      bounds.size.w,
      bounds.size.h
    )
  );
}

/*
 * Exact 13 x 14 Swiss emblem raster from FCK_Gravity.
 * Colors stay GColorRed / GColorWhite. Med Ticker places the same raster at
 * the true display center, matching the middle-button marker at y=114.
 */
static void draw_swiss_emblem(
    GContext *ctx,
    int32_t scroll_offset_y
) {
  if (!s_show_swiss_emblem) {
    return;
  }

  static const char *EMBLEM_ROWS[SWISS_EMBLEM_HEIGHT] = {
    "..RRRRRRRRR..",
    ".RRRRWWWRRRR.",
    ".RRRRWWWRRRR.",
    ".RRRRWWWRRRR.",
    ".RWWWWWWWWWR.",
    ".RWWWWWWWWWR.",
    ".RWWWWWWWWWR.",
    ".RRRRWWWRRRR.",
    ".RRRRWWWRRRR.",
    "..RRRWWWRRR..",
    "..RRRRRRRRR..",
    "....RRRRR....",
    ".....RRR.....",
    "......R......"
  };

  const int16_t left =
      SWISS_EMBLEM_PIVOT_X -
      SWISS_EMBLEM_WIDTH / 2;
  const int16_t top = (int16_t)(
    SWISS_EMBLEM_PIVOT_Y -
    SWISS_EMBLEM_HEIGHT / 2 +
    scroll_offset_y
  );

  for (int16_t row = 0; row < SWISS_EMBLEM_HEIGHT; row++) {
    int16_t run_start = -1;
    char current_symbol = '.';

    for (int16_t column = 0; column <= SWISS_EMBLEM_WIDTH; column++) {
      const char symbol =
          column < SWISS_EMBLEM_WIDTH
              ? EMBLEM_ROWS[row][column]
              : '.';
      const bool drawable =
          symbol == 'W' || symbol == 'R';

      if (drawable && run_start < 0) {
        run_start = column;
        current_symbol = symbol;
        continue;
      }

      if (
        drawable &&
        run_start >= 0 &&
        symbol == current_symbol
      ) {
        continue;
      }

      if (run_start >= 0) {
        graphics_context_set_fill_color(
          ctx,
          current_symbol == 'W'
              ? GColorWhite
              : GColorRed
        );
        graphics_fill_rect(
          ctx,
          GRect(
            left + run_start,
            top + row,
            column - run_start,
            1
          ),
          0,
          GCornerNone
        );
        run_start = -1;
        current_symbol = '.';
      }

      if (drawable) {
        run_start = column;
        current_symbol = symbol;
      }
    }
  }
}

static void destroy_pill_bitmaps(void) {
  for (uint8_t index = 0; index < FRAME_COUNT; index++) {
    if (s_frames[index]) {
      gbitmap_destroy(s_frames[index]);
      s_frames[index] = NULL;
    }
  }

  if (s_sheet) {
    gbitmap_destroy(s_sheet);
    s_sheet = NULL;
  }
}

int32_t pill_arena_origin_y(void) {
  if (!s_canvas_layer) {
    return 0;
  }

  const GRect bounds =
      layer_get_bounds(
        s_canvas_layer
      );

  return
      (
        (
          int32_t
        )bounds.size.h -
        s_frame_height
      ) /
      2 +
      CANVAS_START_OFFSET_Y;
}

int32_t current_pill_y(void) {
  return
      pill_arena_origin_y() +
      visual_canvas_offset_y();
}

static void cover_scrolled_alert_area(
    GContext *ctx,
    GRect bounds
) {
  const int32_t alert_bottom =
      (int32_t)bounds.size.h +
      visual_canvas_offset_y();

  if (alert_bottom >= bounds.size.h) {
    return;
  }

  const int16_t cover_y =
      alert_bottom > 0
          ? (int16_t)alert_bottom
          : 0;

  graphics_context_set_fill_color(
    ctx,
    theme_background_color()
  );
  graphics_fill_rect(
    ctx,
    GRect(
      bounds.origin.x,
      cover_y,
      bounds.size.w,
      bounds.size.h - cover_y
    ),
    0,
    GCornerNone
  );
}

static void draw_alert_page_button_hint(
    GContext *ctx,
    GRect bounds
) {
  if (
    s_scroll.snap_index != 0 ||
    s_confirmation_state != CONFIRM_IDLE ||
    !unconfirmed_medication_group_is_due()
  ) {
    return;
  }

  /*
   * Kleiner Hinweis auf die mittlere Seitentaste.
   * x == bounds.size.w liegt genau einen Pixel rechts
   * vom letzten sichtbaren Pixel. Radius 4 ergibt auf
   * Pebble einen Kreis von praktisch 9 px Durchmesser.
   */
  graphics_context_set_fill_color(
    ctx,
    GColorWhite
  );
  graphics_fill_circle(
    ctx,
    GPoint(
      bounds.origin.x + bounds.size.w,
      bounds.origin.y + bounds.size.h / 2
    ),
    8
  );
}

static void canvas_update_proc(
    Layer *layer,
    GContext *ctx
) {
  const GRect bounds = layer_get_bounds(layer);
  const int32_t scroll_offset_y =
      visual_canvas_offset_y();

  graphics_context_set_fill_color(
    ctx,
    theme_background_color()
  );
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);

  draw_alert_background_pattern(ctx, bounds);

  if (s_confirmed_screen_active) {
    const int32_t confirmed_page_top =
        bounds.origin.y +
        scroll_offset_y;

    /*
     * Beim Overscroll nach unten liegt oberhalb der Vespa-Seite ein kurzer
     * freigelegter Bereich. Dort bleibt jetzt der normale Theme-Hintergrund.
     */
    if (confirmed_page_top > bounds.origin.y) {
      const int16_t overscroll_height = (int16_t)(
        confirmed_page_top - bounds.origin.y > bounds.size.h
            ? bounds.size.h
            : confirmed_page_top - bounds.origin.y
      );

      graphics_context_set_fill_color(
        ctx,
        theme_background_color()
      );
      graphics_fill_rect(
        ctx,
        GRect(
          bounds.origin.x,
          bounds.origin.y,
          bounds.size.w,
          overscroll_height
        ),
        0,
        GCornerNone
      );
    }

    /* Do not render the Vespa page once it is completely off-screen. */
    if (
      confirmed_page_top < bounds.size.h &&
      confirmed_page_top + bounds.size.h > 0
    ) {
      draw_confirmed_page(
        ctx,
        GRect(
          bounds.origin.x,
          (int16_t)confirmed_page_top,
          bounds.size.w,
          bounds.size.h
        )
      );
    }

    draw_medications(
      ctx,
      bounds,
      scroll_offset_y,
      theme_foreground_color(),
      theme_background_color()
    );
    return;
  }

  if (!s_sheet) {
    return;
  }

  const int32_t pill_y = current_pill_y();
  MedicationSymbol active_symbol;
  const bool pen_alert_active =
      active_medication_symbol(&active_symbol) &&
      active_symbol == MEDICATION_SYMBOL_PEN;

  if (pen_alert_active) {
    draw_pen_alert_animation(
      ctx,
      bounds,
      scroll_offset_y,
      (uint8_t)(
        s_animation_tick / PILL_TICKS_PER_FRAME
      ),
      theme_foreground_color()
    );
  } else {
    draw_physics_pills(ctx, bounds, pill_y);
  }

  /* Fixed foreground emblem; pills physically collide with it. */
  draw_swiss_emblem(
    ctx,
    scroll_offset_y
  );

  cover_scrolled_alert_area(ctx, bounds);
  draw_alert_page_button_hint(ctx, bounds);
  draw_medications(
    ctx,
    bounds,
    scroll_offset_y,
    theme_foreground_color(),
    theme_background_color()
  );
}

static void band_arrow_update_proc(
    Layer *layer,
    GContext *ctx
) {
  const GRect bounds =
      layer_get_bounds(layer);

  const int16_t center_y =
      bounds.size.h / 2;

  graphics_context_set_stroke_color(
    ctx,
    theme_foreground_color()
  );

  for (
    int16_t y = 0;
    y < bounds.size.h;
    y++
  ) {
    const int16_t distance =
        y <= center_y
            ? center_y - y
            : y - center_y;

    const int16_t start_x =
        (
          (
            int32_t
          )distance *
          (bounds.size.w - 1)
        ) /
        center_y;

    graphics_draw_line(
      ctx,
      GPoint(
        start_x,
        y
      ),
      GPoint(
        bounds.size.w - 1,
        y
      )
    );
  }
}

static void band_update_proc(
    Layer *layer,
    GContext *ctx
) {
  if (!s_canvas_layer) {
    return;
  }

  const GRect layer_bounds = layer_get_bounds(layer);
  const GRect frame = layer_get_frame(layer);
  const GRect canvas_bounds = layer_get_bounds(s_canvas_layer);

  /* Die versetzte Textkopie invertiert nur den überdeckten Bereich. */
  const GRect content_bounds = GRect(
    -frame.origin.x,
    0,
    canvas_bounds.size.w,
    layer_bounds.size.h
  );

  graphics_context_set_fill_color(
    ctx,
    theme_foreground_color()
  );
  graphics_fill_rect(ctx, layer_bounds, 0, GCornerNone);

  draw_medications(
    ctx,
    content_bounds,
    visual_canvas_offset_y() - frame.origin.y,
    theme_background_color(),
    theme_foreground_color()
  );

  draw_taken_button_hint(
    ctx,
    layer_bounds,
    frame,
    canvas_bounds
  );
}

static void sync_medication_marquee(
    bool advance
) {
  int8_t active_row = -1;

  if (
    s_scroll.mode == SCROLL_IDLE &&
    !s_band.animating &&
    s_band.target_visible &&
    s_scroll.snap_index > 0
  ) {
    const int row_index =
        s_scroll.snap_index - 1;

    if (
      row_index >= 0 &&
      row_index < LIST_ROW_COUNT &&
      s_row_kinds[row_index] ==
          MEDICATION_ROW_ITEM
    ) {
      active_row = (int8_t)row_index;
    }
  }

  if (active_row != s_medication_marquee_row) {
    s_medication_marquee_row = active_row;
    s_medication_marquee_tick = 0;
    return;
  }

  if (
    advance &&
    active_row >= 0
  ) {
    s_medication_marquee_tick++;
  }
}

static void ui_timer_callback(void *context) {
  s_ui_timer = NULL;

  if (
    !s_canvas_layer ||
    s_transfer_screen_active
  ) {
    return;
  }

  const bool visuals_paused =
      alarm_visuals_paused();

  if (!visuals_paused) {
    if (!s_confirmed_screen_active) {
      s_animation_tick =
          (s_animation_tick + 1) %
          (FRAME_COUNT * PILL_TICKS_PER_FRAME);

      update_taken_button_hint_pulse();
    }

    sync_medication_marquee(true);
    mark_scene_dirty();
  } else {
    /*
     * During the one-shot acoustic intro the first alert frame stays
     * completely static. This keeps redraw work away from PCM streaming.
     */
    sync_medication_marquee(false);
  }

  s_ui_timer = app_timer_register(
    UI_TICK_MS,
    ui_timer_callback,
    NULL
  );
}

static void start_ui_timer(void) {
  cancel_timer(&s_ui_timer);

  if (s_transfer_screen_active) {
    return;
  }

  s_ui_timer = app_timer_register(
    UI_TICK_MS,
    ui_timer_callback,
    NULL
  );
}

static void scroll_up_handler(
    ClickRecognizerRef recognizer,
    void *context
) {
  if (scroll_input_allowed()) {
    step_snap_index(-1);
  }
}

static void scroll_down_handler(
    ClickRecognizerRef recognizer,
    void *context
) {
  if (scroll_input_allowed()) {
    step_snap_index(1);
  }
}

static void click_config_provider(void *context) {
  window_single_click_subscribe(
    BUTTON_ID_UP,
    scroll_up_handler
  );

  window_raw_click_subscribe(
    BUTTON_ID_SELECT,
    select_button_down,
    select_button_up,
    NULL
  );

  window_single_click_subscribe(
    BUTTON_ID_DOWN,
    scroll_down_handler
  );

  window_single_click_subscribe(
    BUTTON_ID_BACK,
    back_button_handler
  );
}

static bool load_pill_sheet(void) {
  s_sheet = gbitmap_create_with_resource(
    RESOURCE_ID_IMAGE_PILL_SHEET
  );

  if (!s_sheet) {
    APP_LOG(
      APP_LOG_LEVEL_ERROR,
      "Spritesheet could not be loaded"
    );
    return false;
  }

  const GRect bounds = gbitmap_get_bounds(s_sheet);

  if (bounds.size.w % FRAME_COUNT != 0) {
    APP_LOG(
      APP_LOG_LEVEL_ERROR,
      "Spritesheet width is invalid"
    );
    destroy_pill_bitmaps();
    return false;
  }

  s_frame_width = bounds.size.w / FRAME_COUNT;
  s_frame_height = bounds.size.h;

  for (uint8_t index = 0; index < FRAME_COUNT; index++) {
    s_frames[index] = gbitmap_create_as_sub_bitmap(
      s_sheet,
      GRect(
        index * s_frame_width,
        0,
        s_frame_width,
        s_frame_height
      )
    );

    if (!s_frames[index]) {
      APP_LOG(
        APP_LOG_LEVEL_ERROR,
        "Pill frame could not be created"
      );
      destroy_pill_bitmaps();
      return false;
    }
  }

  return true;
}

static void reset_ui_state(GRect bounds) {
  s_animation_tick = 0;
  s_medication_marquee_tick = 0;
  s_medication_marquee_row = -1;
  s_taken_hint_phase = -1;

  const int32_t initial_position_q8 =
      CANVAS_START_OFFSET_Y *
      SCROLL_Q8;

  s_scroll = (ScrollState) {
    .position_q8 = initial_position_q8,
    .target_q8 = initial_position_q8,
    .breakaway_anchor_q8 =
        initial_position_q8,
    .snap_index = 0,
    .mode = SCROLL_IDLE
  };

#if defined(PBL_TOUCH)
  s_touch = (ScrollTouchState) { 0 };
#endif

  cancel_timer(
    &s_band_animation_timer
  );

  s_band = (BandAnimationState) {
    .x_q8 =
        bounds.size.w *
        SCROLL_Q8,
    .target_x_q8 =
        bounds.size.w *
        SCROLL_Q8,
    .target_visible = false,
    .animating = false
  };

  if (s_band_layer) {
    set_band_layer_x_q8(
      s_band.x_q8
    );

    set_band_and_arrow_hidden(
      true
    );
  }

  s_confirm_radius = 0;
  s_confirm_max_radius =
      bounds.size.w +
      CONFIRM_CENTER_OUTSIDE_X +
      bounds.size.h / 2;
  s_confirmation_state = CONFIRM_IDLE;
  s_confirmation_symbol_set = false;

  s_check_size = 0;
  s_check_state = CHECK_HIDDEN;
}

void refresh_app_screen_state(void) {
  if (s_transfer_screen_active) {
    return;
  }

  reset_medication_confirmations();

  const bool show_confirmed_screen =
      alarm_unconfirmed_symbol_mask_at(
        time(NULL)
      ) == 0;
  const bool state_changed =
      s_confirmed_screen_active !=
          show_confirmed_screen;

  s_confirmed_screen_active =
      show_confirmed_screen;

  if (show_confirmed_screen) {
    rebuild_all_medication_rows();
  } else {
    rebuild_medication_rows();
  }

  pill_physics_rebuild();

  if (!s_canvas_layer) {
    return;
  }

  cancel_timer(&s_ui_timer);
  cancel_timer(&s_band_animation_timer);
  cancel_scroll_physics();

#if defined(PBL_TOUCH)
  s_touch.dragging = false;
#endif

  layer_set_hidden(
    s_canvas_layer,
    false
  );

  reset_ui_state(
    layer_get_bounds(s_canvas_layer)
  );

  if (s_confirmation_layer) {
    layer_set_hidden(
      s_confirmation_layer,
      show_confirmed_screen
    );
  }

  /*
   * Der grüne Haken ist nur der Inhalt von Seite 0.
   * Der Interaktionszustand bleibt exakt derselbe wie
   * auf der normalen Pillenseite: CONFIRM_IDLE.
   */

  start_ui_timer();
  mark_scene_dirty();

  pill_physics_update_activity();

  if (state_changed) {
    APP_LOG(
      APP_LOG_LEVEL_INFO,
      "App screen: %s",
      show_confirmed_screen
          ? "confirmed with all medications"
          : "alert"
    );
  }
}

static void window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  const GRect bounds = layer_get_bounds(root);

  s_medication_font =
      fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
  s_medication_detail_font =
      fonts_get_system_font(FONT_KEY_GOTHIC_18);
  s_header_font =
      fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);

  if (!load_pill_sheet()) {
    return;
  }

  s_alert_pattern_dark_bitmap =
      gbitmap_create_with_resource(
        RESOURCE_ID_IMAGE_SEIGAIHA_DARK
      );
  s_alert_pattern_light_bitmap =
      gbitmap_create_with_resource(
        RESOURCE_ID_IMAGE_SEIGAIHA_LIGHT
      );

  if (
    !s_alert_pattern_dark_bitmap ||
    !s_alert_pattern_light_bitmap
  ) {
    APP_LOG(
      APP_LOG_LEVEL_WARNING,
      "Seigaiha backgrounds could not be loaded"
    );
  }

  s_canvas_layer = layer_create(bounds);
  layer_set_update_proc(
    s_canvas_layer,
    canvas_update_proc
  );
  layer_add_child(root, s_canvas_layer);

  const GRect band_frame = GRect(
    bounds.size.w,
    (
      bounds.size.h -
      MEDICATION_ROW_HEIGHT
    ) /
    2,
    bounds.size.w +
        BAND_OVERSHOOT_COVER_PX,
    MEDICATION_ROW_HEIGHT
  );

  s_band_layer =
      layer_create(band_frame);

  layer_set_clips(
    s_band_layer,
    true
  );

  set_band_and_arrow_hidden(
    true
  );

  layer_set_update_proc(
    s_band_layer,
    band_update_proc
  );

  layer_add_child(
    root,
    s_band_layer
  );

  s_band_arrow_layer =
      layer_create(
        GRect(
          bounds.size.w -
              BAND_ARROW_WIDTH,
          (
            bounds.size.h -
            MEDICATION_ROW_HEIGHT
          ) / 2,
          BAND_ARROW_WIDTH,
          MEDICATION_ROW_HEIGHT
        )
      );

  if (!s_band_arrow_layer) {
    layer_destroy(
      s_band_layer
    );
    s_band_layer = NULL;
    return;
  }

  layer_set_update_proc(
    s_band_arrow_layer,
    band_arrow_update_proc
  );

  layer_add_child(
    root,
    s_band_arrow_layer
  );

  s_confirmation_layer =
      layer_create(bounds);

  layer_set_update_proc(
    s_confirmation_layer,
    confirmation_update_proc
  );

  layer_add_child(
    root,
    s_confirmation_layer
  );

  reset_ui_state(bounds);
}

static void window_appear(Window *window) {
  (void)window;
  s_pill_physics_window_visible = true;

  refresh_medication_rows_for_time();

  if (s_alarm_launch_pending) {
    s_alarm_launch_pending = false;
    alarm_start();
    schedule_next_alarm_wakeup();
  } else {
    refresh_app_screen_state();
  }

  if (
    !s_transfer_screen_active &&
    s_band.animating
  ) {
    schedule_band_animation();
  }

#if defined(PBL_TOUCH)
  touch_service_subscribe(touch_handler, NULL);
#endif
}

static void window_disappear(Window *window) {
  s_pill_physics_window_visible = false;
  pill_physics_update_activity();

  cancel_timer(&s_ui_timer);
  cancel_timer(&s_confirmation_timer);
  cancel_timer(&s_band_animation_timer);
  cancel_scroll_physics();

#if defined(PBL_TOUCH)
  s_touch.dragging = false;
  touch_service_unsubscribe();
#endif
}

static void window_unload(Window *window) {
  pill_physics_stop();
  cancel_timer(&s_transfer_close_timer);
  cancel_timer(&s_transfer_animation_timer);
  cancel_timer(&s_ui_timer);
  cancel_timer(&s_confirmation_timer);
  cancel_timer(&s_band_animation_timer);
  cancel_scroll_physics();

  if (s_alert_pattern_dark_bitmap) {
    gbitmap_destroy(
      s_alert_pattern_dark_bitmap
    );
    s_alert_pattern_dark_bitmap = NULL;
  }

  if (s_alert_pattern_light_bitmap) {
    gbitmap_destroy(
      s_alert_pattern_light_bitmap
    );
    s_alert_pattern_light_bitmap = NULL;
  }

  destroy_pill_bitmaps();

  if (s_confirmation_layer) {
    layer_destroy(s_confirmation_layer);
    s_confirmation_layer = NULL;
  }

  if (s_band_arrow_layer) {
    layer_destroy(s_band_arrow_layer);
    s_band_arrow_layer = NULL;
  }

  if (s_band_layer) {
    layer_destroy(s_band_layer);
    s_band_layer = NULL;
  }

  if (s_canvas_layer) {
    layer_destroy(s_canvas_layer);
    s_canvas_layer = NULL;
  }
}


void medication_ui_init(void) {
  tick_timer_service_subscribe(
    MINUTE_UNIT,
    daypart_tick_handler
  );

  s_window = window_create();
  window_set_background_color(
    s_window,
    theme_background_color()
  );

  window_set_click_config_provider(
    s_window,
    click_config_provider
  );

  window_set_window_handlers(
    s_window,
    (WindowHandlers) {
      .load = window_load,
      .appear = window_appear,
      .disappear = window_disappear,
      .unload = window_unload
    }
  );

  window_stack_push(s_window, true);
}

void medication_ui_deinit(void) {
  cancel_timer(&s_transfer_close_timer);
  cancel_timer(&s_transfer_animation_timer);
  cancel_timer(&s_ui_timer);
  cancel_timer(&s_confirmation_timer);
  cancel_timer(&s_band_animation_timer);
  tick_timer_service_unsubscribe();

  if (s_window) {
    window_destroy(s_window);
    s_window = NULL;
  }
}
