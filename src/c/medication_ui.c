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

#define PILL_SELECT_MARKER_WIDTH 5
#define PILL_SELECT_MARKER_HEIGHT 52
#define PILL_SELECT_MARKER_ANIM_MS 28
#define PILL_SELECT_MARKER_PRESS_OFFSET 2
#define PILL_SELECT_MARKER_PRESSED_WIDTH   (PILL_SELECT_MARKER_WIDTH + PILL_SELECT_MARKER_PRESS_OFFSET)

static GColor theme_foreground_color(void);
static bool scroll_input_allowed(void);

static bool pill_select_marker_page_active(void);
static void reset_pill_select_marker_animation(void);
static void pill_select_marker_animation_tick(void *context);
static void draw_pill_select_marker(
    GContext *ctx,
    GRect bounds
);
static GBitmap *s_alert_pattern_dark_bitmap;
static GBitmap *s_alert_pattern_light_bitmap;
static AppTimer *s_alarm_screen_timer;
static bool s_refresh_after_vespa_scroll;
static bool s_alarm_transitioning_to_pills;
static bool s_alarm_navigation_locked;
static AppTimer *s_pill_select_marker_anim_timer;
static uint8_t s_pill_select_marker_draw_width =
    PILL_SELECT_MARKER_WIDTH;
static bool s_pill_select_marker_button_down;
static bool s_pill_select_marker_peak_reached;
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
static void draw_vespa_next_alarm_bubble(
    GContext *ctx,
    GRect bounds,
    int32_t vespa_top
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
static void alarm_screen_timer_handler(
    void *context
);
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
      !medication_ui_alarm_transitioning_to_pills() &&
      s_confirmation_state == CONFIRM_IDLE;
}

bool medication_ui_alarm_transitioning_to_pills(void) {
  return
      s_alarm_transitioning_to_pills &&
      s_alarm_active;
}

bool medication_ui_alarm_navigation_locked(void) {
  return
      s_alarm_navigation_locked &&
      s_alarm_active;
}

static bool pill_select_marker_page_active(void) {
  if (
    !s_canvas_layer ||
    s_transfer_screen_active ||
    s_confirmed_screen_active ||
    medication_ui_alarm_transitioning_to_pills() ||
    s_scroll.snap_index != 0 ||
    s_scroll.mode != SCROLL_IDLE ||
    visual_canvas_offset_y() !=
        scroll_snap_anchor_y(0)
  ) {
    return false;
  }

  MedicationSymbol active_symbol;

  if (
    active_medication_symbol(&active_symbol) &&
    active_symbol == MEDICATION_SYMBOL_PEN
  ) {
    return false;
  }

  return true;
}

static void reset_pill_select_marker_animation(void) {
  cancel_timer(&s_pill_select_marker_anim_timer);

  s_pill_select_marker_draw_width =
      PILL_SELECT_MARKER_WIDTH;
  s_pill_select_marker_button_down = false;
  s_pill_select_marker_peak_reached = false;

  if (s_canvas_layer) {
    layer_mark_dirty(s_canvas_layer);
  }
}

static void pill_select_marker_animation_tick(
    void *context
) {
  (void)context;
  s_pill_select_marker_anim_timer = NULL;

  if (!pill_select_marker_page_active()) {
    reset_pill_select_marker_animation();
    return;
  }

  if (!s_pill_select_marker_peak_reached) {
    if (
      s_pill_select_marker_draw_width <
          PILL_SELECT_MARKER_PRESSED_WIDTH
    ) {
      s_pill_select_marker_draw_width++;
    }

    if (
      s_pill_select_marker_draw_width >=
          PILL_SELECT_MARKER_PRESSED_WIDTH
    ) {
      s_pill_select_marker_draw_width =
          PILL_SELECT_MARKER_PRESSED_WIDTH;
      s_pill_select_marker_peak_reached = true;
    }
  } else if (
    !s_pill_select_marker_button_down &&
    s_pill_select_marker_draw_width >
        PILL_SELECT_MARKER_WIDTH
  ) {
    s_pill_select_marker_draw_width--;
  }

  if (s_canvas_layer) {
    layer_mark_dirty(s_canvas_layer);
  }

  const bool moving_in =
      !s_pill_select_marker_peak_reached &&
      s_pill_select_marker_draw_width <
          PILL_SELECT_MARKER_PRESSED_WIDTH;

  const bool moving_out =
      s_pill_select_marker_peak_reached &&
      !s_pill_select_marker_button_down &&
      s_pill_select_marker_draw_width >
          PILL_SELECT_MARKER_WIDTH;

  if (moving_in || moving_out) {
    s_pill_select_marker_anim_timer =
        app_timer_register(
          PILL_SELECT_MARKER_ANIM_MS,
          pill_select_marker_animation_tick,
          NULL
        );

    if (!s_pill_select_marker_anim_timer) {
      reset_pill_select_marker_animation();
    }
  }
}

void medication_ui_pill_select_marker_press(void) {
  if (
    !pill_select_marker_page_active() ||
    s_pill_select_marker_button_down ||
    s_pill_select_marker_draw_width !=
        PILL_SELECT_MARKER_WIDTH ||
    s_pill_select_marker_anim_timer
  ) {
    return;
  }

  s_pill_select_marker_button_down = true;
  s_pill_select_marker_peak_reached = false;

  /*
   * Wie in Swiss Chronograph: der erste Pixel kommt sofort,
   * damit der Hardwaredruck ohne wahrnehmbare Verzögerung reagiert.
   */
  s_pill_select_marker_draw_width++;

  if (s_canvas_layer) {
    layer_mark_dirty(s_canvas_layer);
  }

  s_pill_select_marker_anim_timer =
      app_timer_register(
        PILL_SELECT_MARKER_ANIM_MS,
        pill_select_marker_animation_tick,
        NULL
      );

  if (!s_pill_select_marker_anim_timer) {
    reset_pill_select_marker_animation();
  }
}

void medication_ui_pill_select_marker_release(void) {
  if (!s_pill_select_marker_button_down) {
    return;
  }

  s_pill_select_marker_button_down = false;

  /*
   * Bei einem sehr kurzen Klick läuft die Bewegung zunächst noch
   * vollständig bis 7 px und anschließend wieder auf 5 px zurück.
   * Das entspricht der Swiss-Chronograph-Tastenanimation.
   */
  if (
    s_pill_select_marker_peak_reached &&
    s_pill_select_marker_draw_width >
        PILL_SELECT_MARKER_WIDTH &&
    !s_pill_select_marker_anim_timer
  ) {
    s_pill_select_marker_anim_timer =
        app_timer_register(
          PILL_SELECT_MARKER_ANIM_MS,
          pill_select_marker_animation_tick,
          NULL
        );

    if (!s_pill_select_marker_anim_timer) {
      reset_pill_select_marker_animation();
    }
  }
}

static void draw_pill_select_marker(
    GContext *ctx,
    GRect bounds
) {
  if (!pill_select_marker_page_active()) {
    return;
  }

  const int16_t marker_width =
      s_pill_select_marker_draw_width;
  const int16_t marker_y =
      bounds.origin.y +
      bounds.size.h / 2 -
      PILL_SELECT_MARKER_HEIGHT / 2;

  graphics_context_set_fill_color(
    ctx,
    GColorWhite
  );

  graphics_fill_rect(
    ctx,
    GRect(
      bounds.origin.x +
          bounds.size.w -
          marker_width,
      marker_y,
      marker_width,
      PILL_SELECT_MARKER_HEIGHT
    ),
    5,
    GCornersLeft
  );
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
  /* Schweizer Wappen aus der Pillensimulation entfernt. */
  (void)ctx;
  (void)scroll_offset_y;
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
  /*
   * Alter Mitteltasten-Hinweis entfernt.
   * Die Bestätigung wird inzwischen durch die OK-Animation erklärt;
   * der weiße Halbkreis am rechten Displayrand ist daher nicht mehr nötig.
   */
  (void)ctx;
  (void)bounds;
}

static void draw_vespa_next_alarm_bubble(
    GContext *ctx,
    GRect bounds,
    int32_t vespa_top
) {
  const time_t next_alarm =
      alarm_next_timestamp();

  if (next_alarm <= 0) {
    return;
  }

  struct tm *local_ptr =
      localtime(&next_alarm);

  if (!local_ptr) {
    return;
  }

  const struct tm local = *local_ptr;
  char time_text[8];

  snprintf(
    time_text,
    sizeof(time_text),
    "%02d:%02d",
    local.tm_hour,
    local.tm_min
  );

  char vertical_text[16];

  snprintf(
    vertical_text,
    sizeof(vertical_text),
    "%c\n%c\n%c\n%c\n%c",
    time_text[0],
    time_text[1],
    time_text[2],
    time_text[3],
    time_text[4]
  );

  /*
   * Links und oben jeweils exakt 10 px Abstand.
   * Keine Spitze: nur die abgerundete Sprechblase.
   */
  const GRect bubble_rect = GRect(
    bounds.origin.x + 10,
    (int16_t)(vespa_top + 10),
    46,
    122
  );

  /*
   * Außen schwarz, innen weiß: runde Kontur ohne zusätzliche API.
   */
  graphics_context_set_fill_color(
    ctx,
    GColorBlack
  );
  graphics_fill_rect(
    ctx,
    bubble_rect,
    18,
    GCornersAll
  );

  graphics_context_set_fill_color(
    ctx,
    GColorWhite
  );
  graphics_fill_rect(
    ctx,
    GRect(
      bubble_rect.origin.x + 2,
      bubble_rect.origin.y + 2,
      bubble_rect.size.w - 4,
      bubble_rect.size.h - 4
    ),
    16,
    GCornersAll
  );

  graphics_context_set_text_color(
    ctx,
    GColorBlack
  );
  graphics_draw_text(
    ctx,
    vertical_text,
    fonts_get_system_font(
      FONT_KEY_GOTHIC_18_BOLD
    ),
    GRect(
      bubble_rect.origin.x,
      bubble_rect.origin.y + 9,
      bubble_rect.size.w,
      bubble_rect.size.h - 12
    ),
    GTextOverflowModeTrailingEllipsis,
    GTextAlignmentCenter,
    NULL
  );
}

static void canvas_update_proc(
    Layer *layer,
    GContext *ctx
) {
  const GRect bounds = layer_get_bounds(layer);
  const int32_t scroll_offset_y = visual_canvas_offset_y();
  const bool alarm_transitioning =
      medication_ui_alarm_transitioning_to_pills();
  const bool alarm_locked =
      medication_ui_alarm_navigation_locked();

  graphics_context_set_fill_color(ctx, theme_background_color());
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);

  draw_alert_background_pattern(ctx, bounds);

  if (!s_confirmed_screen_active && s_sheet) {
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
        (uint8_t)(s_animation_tick / PILL_TICKS_PER_FRAME),
        theme_foreground_color()
      );
    } else {
      draw_physics_pills(ctx, bounds, pill_y);
    }

    draw_swiss_emblem(ctx, scroll_offset_y);
  }

  cover_scrolled_alert_area(ctx, bounds);

  /*
   * Während Vespa -> Pillenanimation darf die Intake-Liste nicht zwischen
   * beiden Seiten auftauchen. Erst wenn die Pillenseite eingerastet ist,
   * wird sie direkt darunter freigegeben.
   */
  if (!alarm_transitioning) {
    draw_intake_medications(
      ctx,
      bounds,
      scroll_offset_y,
      theme_foreground_color(),
      theme_background_color()
    );
  }

  /*
   * Im 3-Sekunden-Intro liegt die Vespa temporär exakt eine Bildschirmhöhe
   * unter der Pillenanimation. So fährt die automatische Bewegung nur eine
   * Seite hoch und es gibt weder Intake-Liste noch eine leere Zwischenpage.
   */
  const int32_t vespa_content_top =
      alarm_transitioning
          ? bounds.size.h
          : scroll_vespa_page_top_y();

  const int32_t vespa_top =
      vespa_content_top + scroll_offset_y;

  if (
    !alarm_locked &&
    vespa_top < bounds.size.h &&
    vespa_top + bounds.size.h > 0
  ) {
    draw_confirmed_page(
      ctx,
      GRect(
        bounds.origin.x,
        (int16_t)vespa_top,
        bounds.size.w,
        bounds.size.h
      )
    );
    draw_vespa_next_alarm_bubble(
      ctx,
      bounds,
      vespa_top
    );
  }

  /*
   * Solange der Alarm aktiv verriegelt ist, endet die Navigation nach den
   * fälligen Medikamenten. Vespa und Gesamtliste sind dann nicht erreichbar.
   */
  if (!alarm_locked) {
    draw_medications(
      ctx,
      bounds,
      scroll_offset_y,
      theme_foreground_color(),
      theme_background_color()
    );
  }

  draw_pill_select_marker(
    ctx,
    bounds
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

  draw_intake_medications(
    ctx,
    content_bounds,
    visual_canvas_offset_y() - frame.origin.y,
    theme_background_color(),
    theme_foreground_color()
  );

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
    s_band.target_visible
  ) {
    const int row_index =
        scroll_all_medication_row_for_snap(s_scroll.snap_index);

    if (row_index >= 0 && row_index < LIST_ROW_COUNT &&
        s_row_kinds[row_index] == MEDICATION_ROW_ITEM) {
      active_row = (int8_t)row_index;
    }
  }

  if (active_row != s_medication_marquee_row) {
    s_medication_marquee_row = active_row;
    s_medication_marquee_tick = 0;
    return;
  }

  if (advance && active_row >= 0) {
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

static void alarm_screen_timer_handler(
    void *context
) {
  (void)context;
  s_alarm_screen_timer = NULL;

  if (
    !s_canvas_layer ||
    s_transfer_screen_active ||
    INTAKE_ROW_COUNT <= 0
  ) {
    return;
  }

  if (!s_alarm_active) {
    s_alarm_transitioning_to_pills = false;
    s_alarm_navigation_locked = false;
    refresh_app_screen_state();
    return;
  }

  /*
   * Der temporäre Vespa-Screen liegt jetzt direkt unter der Pillenanimation.
   * Ziel 0 ist deshalb genau eine Bildschirmhöhe nach oben.
   */
  scroll_to_snap_index(0);
}

void medication_ui_begin_alarm_sequence(void) {
  cancel_timer(&s_alarm_screen_timer);
  s_refresh_after_vespa_scroll = false;
  s_alarm_navigation_locked = false;
  s_alarm_transitioning_to_pills = false;

  if (
    !s_alarm_active ||
    !s_canvas_layer ||
    INTAKE_ROW_COUNT <= 0
  ) {
    return;
  }

  s_alarm_transitioning_to_pills = true;

  /*
   * refresh_app_screen_state() hat zunächst die normale Vespa-Position
   * aufgebaut. Noch bevor der Eventloop sie zeichnet, setzen wir für das
   * Alarm-Intro eine temporäre Zwei-Seiten-Geometrie:
   *
   *   Pillenanimation
   *   Vespa
   *
   * Die Intake-Liste wird währenddessen nicht gezeichnet.
   */
  cancel_scroll_physics();

  const int32_t intro_position_q8 =
      -(int32_t)layer_get_bounds(s_canvas_layer).size.h *
      SCROLL_Q8;

  s_scroll.position_q8 = intro_position_q8;
  s_scroll.target_q8 = intro_position_q8;
  s_scroll.breakaway_anchor_q8 = intro_position_q8;
  s_scroll.velocity_q8 = 0;
  s_scroll.snap_index = 0;
  s_scroll.mode = SCROLL_IDLE;
  s_scroll.breakaway_locked = false;

#if defined(PBL_TOUCH)
  s_touch.dragging = false;
#endif

  mark_scene_dirty();

  s_alarm_screen_timer = app_timer_register(
    3000,
    alarm_screen_timer_handler,
    NULL
  );

  if (!s_alarm_screen_timer) {
    alarm_screen_timer_handler(NULL);
  }
}

void medication_ui_return_to_vespa_after_confirmation(void) {
  cancel_timer(&s_alarm_screen_timer);
  s_alarm_transitioning_to_pills = false;

  /*
   * Wenn noch eine zweite Medikamentengruppe offen ist, bleibt die
   * Alarmnavigation verriegelt. refresh_app_screen_state() baut den neuen
   * Intake-Snapshot auf und reset_ui_state() startet wegen des Locks direkt
   * wieder auf der Pillenanimation - ohne Vespa-Zwischenstopp.
   */
  if (s_alarm_active) {
    s_alarm_navigation_locked = true;
    s_refresh_after_vespa_scroll = false;
    refresh_app_screen_state();
    return;
  }

  s_alarm_navigation_locked = false;
  s_refresh_after_vespa_scroll = true;
  scroll_to_snap_index(scroll_vespa_snap_index());
}

void medication_ui_scroll_settled(void) {
  if (
    s_alarm_transitioning_to_pills &&
    s_scroll.snap_index == 0
  ) {
    /*
     * Jetzt ist die Pillenanimation vollständig sichtbar. Erst ab diesem
     * Moment kommt die Intake-Liste direkt darunter in den Scrollbereich.
     * Die Vespa wird bis zum Ende des aktiven Alarms gesperrt.
     */
    s_alarm_transitioning_to_pills = false;
    s_alarm_navigation_locked = s_alarm_active;
    mark_scene_dirty();
    return;
  }

  if (
    !s_refresh_after_vespa_scroll ||
    s_scroll.snap_index != scroll_vespa_snap_index()
  ) {
    return;
  }

  s_refresh_after_vespa_scroll = false;
  refresh_app_screen_state();

  if (s_alarm_active && INTAKE_ROW_COUNT > 0) {
    medication_ui_begin_alarm_sequence();
  }
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
  reset_pill_select_marker_animation();

  s_animation_tick = 0;
  s_medication_marquee_tick = 0;
  s_medication_marquee_row = -1;
  s_taken_hint_phase = -1;

  const int initial_snap_index =
      medication_ui_alarm_navigation_locked()
          ? 0
          : scroll_vespa_snap_index();

  const int32_t initial_position_q8 =
      scroll_snap_anchor_y(initial_snap_index) * SCROLL_Q8;

  s_scroll = (ScrollState) {
    .position_q8 = initial_position_q8,
    .target_q8 = initial_position_q8,
    .breakaway_anchor_q8 = initial_position_q8,
    .snap_index = initial_snap_index,
    .mode = SCROLL_IDLE
  };

#if defined(PBL_TOUCH)
  s_touch = (ScrollTouchState) { 0 };
#endif

  cancel_timer(&s_band_animation_timer);
  s_band = (BandAnimationState) {
    .x_q8 = bounds.size.w * SCROLL_Q8,
    .target_x_q8 = bounds.size.w * SCROLL_Q8,
    .target_visible = false,
    .animating = false
  };

  if (s_band_layer) {
    set_band_layer_x_q8(s_band.x_q8);
    set_band_and_arrow_hidden(true);
  }

  s_confirm_radius = 0;
  s_confirm_max_radius =
      bounds.size.w + CONFIRM_CENTER_OUTSIDE_X + bounds.size.h / 2;
  s_confirmation_state = CONFIRM_IDLE;
  s_confirmation_symbol_set = false;
  s_check_size = 0;
  s_check_state = CHECK_HIDDEN;
}

void refresh_app_screen_state(void) {
  if (s_transfer_screen_active) {
    return;
  }

  cancel_timer(&s_alarm_screen_timer);
  reset_medication_confirmations();

  const bool show_confirmed_screen =
      alarm_unconfirmed_symbol_mask_at(time(NULL)) == 0;
  const bool state_changed =
      s_confirmed_screen_active != show_confirmed_screen;
  s_confirmed_screen_active = show_confirmed_screen;

  rebuild_medication_rows();
  rebuild_all_medication_rows();
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

  layer_set_hidden(s_canvas_layer, false);
  reset_ui_state(layer_get_bounds(s_canvas_layer));

  if (s_confirmation_layer) {
    layer_set_hidden(s_confirmation_layer, show_confirmed_screen);
  }

  start_ui_timer();
  mark_scene_dirty();
  pill_physics_update_activity();

  if (state_changed) {
    APP_LOG(APP_LOG_LEVEL_INFO, "App intake state: %s",
            show_confirmed_screen ? "complete" : "pending");
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
  s_alarm_transitioning_to_pills = false;
  s_alarm_navigation_locked = false;

  s_pill_physics_window_visible = false;
  pill_physics_update_activity();

  cancel_timer(&s_alarm_screen_timer);
  s_refresh_after_vespa_scroll = false;
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
  s_alarm_transitioning_to_pills = false;
  s_alarm_navigation_locked = false;

  pill_physics_stop();
  cancel_timer(&s_alarm_screen_timer);
  s_refresh_after_vespa_scroll = false;
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
  s_alarm_transitioning_to_pills = false;
  s_alarm_navigation_locked = false;

  cancel_timer(&s_alarm_screen_timer);
  s_refresh_after_vespa_scroll = false;
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
