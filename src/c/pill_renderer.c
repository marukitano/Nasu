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

#define PILL_IMPRINT_GLYPH_WIDTH 5
#define PILL_IMPRINT_GLYPH_HEIGHT 7
#define PILL_IMPRINT_GLYPH_GAP 1
#define PILL_IMPRINT_MAX_SCALE 4

static const uint8_t s_pill_imprint_digits[10][7] = {
  { 14, 17, 19, 21, 25, 17, 14 },
  { 4, 12, 4, 4, 4, 4, 14 },
  { 14, 17, 1, 2, 4, 8, 31 },
  { 30, 1, 1, 14, 1, 1, 30 },
  { 2, 6, 10, 18, 31, 2, 2 },
  { 31, 16, 16, 30, 1, 1, 30 },
  { 14, 16, 16, 30, 17, 17, 14 },
  { 31, 1, 2, 4, 8, 8, 8 },
  { 14, 17, 17, 14, 17, 17, 14 },
  { 14, 17, 17, 15, 1, 1, 14 }
};

static const uint8_t s_pill_imprint_letters[26][7] = {
  { 14, 17, 17, 31, 17, 17, 17 },
  { 30, 17, 17, 30, 17, 17, 30 },
  { 14, 17, 16, 16, 16, 17, 14 },
  { 30, 17, 17, 17, 17, 17, 30 },
  { 31, 16, 16, 30, 16, 16, 31 },
  { 31, 16, 16, 30, 16, 16, 16 },
  { 14, 17, 16, 23, 17, 17, 15 },
  { 17, 17, 17, 31, 17, 17, 17 },
  { 14, 4, 4, 4, 4, 4, 14 },
  { 7, 2, 2, 2, 2, 18, 12 },
  { 17, 18, 20, 24, 20, 18, 17 },
  { 16, 16, 16, 16, 16, 16, 31 },
  { 17, 27, 21, 21, 17, 17, 17 },
  { 17, 25, 21, 19, 17, 17, 17 },
  { 14, 17, 17, 17, 17, 17, 14 },
  { 30, 17, 17, 30, 16, 16, 16 },
  { 14, 17, 17, 17, 21, 18, 13 },
  { 30, 17, 17, 30, 20, 18, 17 },
  { 15, 16, 16, 14, 1, 1, 30 },
  { 31, 4, 4, 4, 4, 4, 4 },
  { 17, 17, 17, 17, 17, 17, 14 },
  { 17, 17, 17, 17, 17, 10, 4 },
  { 17, 17, 17, 21, 21, 21, 10 },
  { 17, 17, 10, 4, 10, 17, 17 },
  { 17, 17, 10, 4, 4, 4, 4 },
  { 31, 1, 2, 4, 8, 16, 31 }
};

static const uint8_t s_pill_imprint_unknown[7] = {
  14, 17, 1, 2, 4, 0, 4
};
static const uint8_t s_pill_imprint_bar[7] = {
  4, 4, 4, 4, 4, 4, 4
};
static const uint8_t s_pill_imprint_dash[7] = {
  0, 0, 0, 31, 0, 0, 0
};
static const uint8_t s_pill_imprint_slash[7] = {
  1, 2, 2, 4, 8, 8, 16
};
static const uint8_t s_pill_imprint_plus[7] = {
  0, 4, 4, 31, 4, 4, 0
};
static const uint8_t s_pill_imprint_dot[7] = {
  0, 0, 0, 0, 0, 6, 6
};
static const uint8_t s_pill_imprint_space[7] = {
  0, 0, 0, 0, 0, 0, 0
};


#define PHYSICS_ROUNDED_OVAL_QUADRANT_STEPS 8
#define PHYSICS_ROUNDED_OVAL_PATH_POINTS \
  (PHYSICS_ROUNDED_OVAL_QUADRANT_STEPS * 4)
#define PILL_RENDER_OUTLINE_PX 3
#define PILL_RENDER_DIAMOND_OUTLINE_PX 4
#define PHYSICS_ROUNDED_OVAL_Q12 4096

/*
 * Longer cubic control arms mean larger visual radii:
 * - 82 % at the left/right ends makes them much rounder.
 * - 76 % along the top/bottom keeps those arcs noticeably flatter.
 */
#define PHYSICS_ROUNDED_OVAL_END_CONTROL_NUM 81
#define PHYSICS_ROUNDED_OVAL_TOP_CONTROL_NUM 57
#define PHYSICS_ROUNDED_OVAL_CONTROL_DEN 100


typedef struct {
  bool valid;
  uint8_t shape;
  int16_t line_half;
  int16_t radius;
  int16_t diamond_half;
  GPoint outer_points[PHYSICS_ROUNDED_OVAL_PATH_POINTS];
  GPoint inner_points[PHYSICS_ROUNDED_OVAL_PATH_POINTS];
  GPathInfo outer_info;
  GPathInfo inner_info;
  GPath *outer_path;
  GPath *inner_path;
} PillRenderPathCache;

static PillRenderPathCache
    s_pill_render_path_cache[MAX_MEDICATIONS];

static PillRenderPathCache s_pill_icon_oval_cache;
static PillRenderPathCache s_pill_icon_diamond_cache;

static PillRenderPathCache *pill_render_icon_path_cache(
    uint8_t shape
);

static void pill_render_path_cache_clear(
    PillRenderPathCache *cache
);
static PillRenderPathCache *pill_render_path_cache_prepare(
    uint8_t medication_index,
    const MedicationAppearance *appearance,
    int16_t line_half,
    int16_t radius,
    int16_t diamond_half
);

static const int8_t s_pen_alert_y_offsets[8] = {
  2, 0, -3, -5, -3, 0, 2, 3
};
static const int8_t s_pen_alert_angle_degrees[8] = {
  -4, -2, 0, 2, 4, 2, 0, -2
};


static void draw_tablet_icon(
    GContext *ctx,
    GRect frame,
    const MedicationSettings *medication,
    const MedicationAppearance *appearance,
    GColor outline_color
);
static void draw_pen_icon(
    GContext *ctx,
    GRect frame,
    const MedicationSettings *medication,
    const MedicationAppearance *appearance,
    GColor outline_color
);
static void draw_medication_icon(
    GContext *ctx,
    GRect frame,
    const MedicationSettings *medication,
    const MedicationAppearance *appearance,
    GColor outline_color
);
static int16_t medication_name_width(
    const char *name
);
static int16_t medication_marquee_offset(
    int16_t overflow
);
static void draw_medication_name(
    GContext *ctx,
    const char *name,
    GRect frame,
    int row_index
);
static void draw_physics_round_line(
    GContext *ctx,
    GPoint start,
    GPoint end,
    uint8_t radius,
    GColor color
);
static void draw_physics_capsule(
    GContext *ctx,
    GPoint center,
    int32_t angle,
    int16_t half_length,
    uint8_t radius,
    GColor fill_color,
    GColor outline_color,
    bool draw_divider
);
static uint8_t medication_appearance_size(
    const MedicationAppearance *appearance
);
static int16_t medication_appearance_scaled(
    int16_t base,
    uint8_t size
);
static void medication_appearance_geometry(
    const MedicationAppearance *appearance,
    int16_t *line_half,
    int16_t *radius,
    int16_t *diamond_half
);
static void draw_physics_capsule_pixel_run(
    GContext *ctx,
    int16_t y,
    int16_t start_x,
    int16_t end_x,
    uint8_t color_index,
    GColor first_outline_color,
    GColor second_outline_color,
    GColor first_color,
    GColor second_color
);
static void draw_physics_two_color_capsule(
    GContext *ctx,
    GPoint center,
    int32_t angle,
    int16_t half_length,
    uint8_t radius,
    GColor first_color,
    GColor second_color,
    GColor first_outline_color,
    GColor second_outline_color
);
static void draw_physics_appearance_diamond(
    GContext *ctx,
    GPoint center,
    int32_t angle,
    PillRenderPathCache *cache,
    GColor fill_color,
    GColor outline_color
);
static GColor medication_appearance_darker_color(
    uint8_t argb
);
static const uint8_t *pill_physics_imprint_glyph(
    char character
);
static GPoint pill_physics_imprint_rotated_point(
    GPoint center,
    int32_t angle,
    int16_t local_x,
    int16_t local_y
);
static void draw_physics_appearance_imprint(
    GContext *ctx,
    GPoint center,
    int32_t angle,
    const MedicationAppearance *appearance,
    int16_t body_width,
    int16_t body_height
);
static int16_t pill_physics_cubic_component(
    int16_t p0,
    int16_t p1,
    int16_t p2,
    int16_t p3,
    int32_t t_q12
);
static void pill_physics_rounded_oval_quadrant(
    GPoint *points,
    uint8_t point_offset,
    int16_t p0_x,
    int16_t p0_y,
    int16_t p1_x,
    int16_t p1_y,
    int16_t p2_x,
    int16_t p2_y,
    int16_t p3_x,
    int16_t p3_y
);
static void draw_physics_true_ellipse(
    GContext *ctx,
    GPoint center,
    int32_t angle,
    PillRenderPathCache *cache,
    GColor fill_color,
    GColor outline_color
);
static void draw_physics_pill_body(
    GContext *ctx,
    const PillPhysicsBody *body,
    int32_t arena_y
);

static void draw_tablet_icon(
    GContext *ctx,
    GRect frame,
    const MedicationSettings *medication,
    const MedicationAppearance *appearance,
    GColor outline_color
) {
  (void)outline_color;

  const bool use_appearance =
      appearance && appearance->valid;
  const uint8_t shape =
      use_appearance
          ? appearance->shape
          : medication->shape;
  const GColor fill_color = {
    .argb =
        use_appearance
            ? appearance->primary_color
            : medication->color
  };
  const GColor second_fill_color = {
    .argb =
        use_appearance
            ? appearance->secondary_color
            : medication->color
  };
  const GColor primary_outline_color =
      medication_appearance_darker_color(
        fill_color.argb
      );
  const GColor secondary_outline_color =
      medication_appearance_darker_color(
        second_fill_color.argb
      );

  const int16_t center_x =
      frame.origin.x + frame.size.w / 2;
  const int16_t center_y =
      frame.origin.y + frame.size.h / 2;
  const GPoint center =
      GPoint(center_x, center_y);

  switch (shape) {
    case 0:
      graphics_context_set_fill_color(
        ctx,
        primary_outline_color
      );
      graphics_fill_circle(
        ctx,
        center,
        8 + PILL_RENDER_OUTLINE_PX
      );

      graphics_context_set_fill_color(
        ctx,
        fill_color
      );
      graphics_fill_circle(
        ctx,
        center,
        8
      );
      break;

    case 1: {
      PillRenderPathCache *icon_cache =
          pill_render_icon_path_cache(1);

      draw_physics_true_ellipse(
        ctx,
        center,
        0,
        icon_cache,
        fill_color,
        primary_outline_color
      );
      break;
    }

    case 2:
      draw_physics_capsule(
        ctx,
        center,
        0,
        8,
        4,
        fill_color,
        primary_outline_color,
        true
      );
      break;

    case 4:
      draw_physics_two_color_capsule(
        ctx,
        center,
        0,
        7,
        7,
        fill_color,
        second_fill_color,
        primary_outline_color,
        secondary_outline_color
      );
      break;

    case 3: {
      PillRenderPathCache *icon_cache =
          pill_render_icon_path_cache(3);

      draw_physics_appearance_diamond(
        ctx,
        center,
        0,
        icon_cache,
        fill_color,
        primary_outline_color
      );
      break;
    }
  }

  graphics_context_set_stroke_width(ctx, 1);
}

static GPoint pen_shape_point(
    GPoint center,
    int32_t angle,
    int16_t along,
    int16_t normal
) {
  const int32_t normal_angle =
      angle + TRIG_MAX_ANGLE / 4;

  return GPoint(
    center.x +
        (int16_t)(
          ((int32_t)cos_lookup(angle) * along) /
          TRIG_MAX_RATIO
        ) +
        (int16_t)(
          ((int32_t)cos_lookup(normal_angle) * normal) /
          TRIG_MAX_RATIO
        ),
    center.y +
        (int16_t)(
          ((int32_t)sin_lookup(angle) * along) /
          TRIG_MAX_RATIO
        ) +
        (int16_t)(
          ((int32_t)sin_lookup(normal_angle) * normal) /
          TRIG_MAX_RATIO
        )
  );
}

static void draw_pen_shape(
    GContext *ctx,
    GPoint center,
    int32_t angle,
    int16_t half_length,
    uint8_t outline_width,
    uint8_t fill_width,
    uint8_t detail_width,
    GColor body_color,
    GColor accent_color,
    GColor outline_color
) {
  const GPoint body_top =
      pen_shape_point(
        center,
        angle,
        (int16_t)-half_length,
        0
      );
  const GPoint body_bottom =
      pen_shape_point(
        center,
        angle,
        half_length,
        0
      );

  /* Main barrel with rounded ends. */
  graphics_context_set_stroke_color(
    ctx,
    outline_color
  );
  graphics_context_set_stroke_width(
    ctx,
    outline_width
  );
  graphics_draw_line(
    ctx,
    body_top,
    body_bottom
  );
  graphics_context_set_fill_color(
    ctx,
    outline_color
  );
  graphics_fill_circle(
    ctx,
    body_top,
    outline_width / 2
  );
  graphics_fill_circle(
    ctx,
    body_bottom,
    outline_width / 2
  );

  graphics_context_set_stroke_color(
    ctx,
    body_color
  );
  graphics_context_set_stroke_width(
    ctx,
    fill_width
  );
  graphics_draw_line(
    ctx,
    body_top,
    body_bottom
  );
  graphics_context_set_fill_color(
    ctx,
    body_color
  );
  graphics_fill_circle(
    ctx,
    body_top,
    fill_width / 2
  );
  graphics_fill_circle(
    ctx,
    body_bottom,
    fill_width / 2
  );

  /*
   * Broad coloured dose/grip section. Keeping this broad makes the
   * configured accent clearly visible on the small colour display.
   */
  const GPoint grip_top =
      pen_shape_point(
        center,
        angle,
        half_length / 5,
        0
      );
  const GPoint grip_bottom =
      pen_shape_point(
        center,
        angle,
        half_length - 6,
        0
      );

  graphics_context_set_stroke_color(
    ctx,
    accent_color
  );
  graphics_context_set_stroke_width(
    ctx,
    fill_width > 4
        ? fill_width - 4
        : fill_width
  );
  graphics_draw_line(
    ctx,
    grip_top,
    grip_bottom
  );

  /* Dark dose window in the upper part of the barrel. */
  const GPoint window_top =
      pen_shape_point(
        center,
        angle,
        (int16_t)(-half_length / 3),
        0
      );
  const GPoint window_bottom =
      pen_shape_point(
        center,
        angle,
        (int16_t)(-half_length / 9),
        0
      );

  graphics_context_set_stroke_color(
    ctx,
    outline_color
  );
  graphics_context_set_stroke_width(
    ctx,
    detail_width + 2
  );
  graphics_draw_line(
    ctx,
    window_top,
    window_bottom
  );

  /* Plunger and coloured button at the top. */
  const GPoint plunger_center =
      pen_shape_point(
        center,
        angle,
        (int16_t)(-half_length - 7),
        0
      );
  const int16_t button_half =
      fill_width / 2 + 2;
  const GPoint button_left =
      pen_shape_point(
        plunger_center,
        angle,
        0,
        (int16_t)-button_half
      );
  const GPoint button_right =
      pen_shape_point(
        plunger_center,
        angle,
        0,
        button_half
      );

  graphics_context_set_stroke_color(
    ctx,
    outline_color
  );
  graphics_context_set_stroke_width(
    ctx,
    detail_width + 2
  );
  graphics_draw_line(
    ctx,
    body_top,
    plunger_center
  );

  graphics_context_set_stroke_color(
    ctx,
    accent_color
  );
  graphics_context_set_stroke_width(
    ctx,
    detail_width + 2
  );
  graphics_draw_line(
    ctx,
    button_left,
    button_right
  );

  /*
   * Needle hub and needle. `angle` points from the top of the pen towards
   * the bottom, so the needle always leaves the lower end.
   */
  const int16_t collar_half =
      fill_width / 2 + 1;
  const GPoint collar_left =
      pen_shape_point(
        body_bottom,
        angle,
        0,
        (int16_t)-collar_half
      );
  const GPoint collar_right =
      pen_shape_point(
        body_bottom,
        angle,
        0,
        collar_half
      );

  graphics_context_set_stroke_color(
    ctx,
    accent_color
  );
  graphics_context_set_stroke_width(
    ctx,
    detail_width + 1
  );
  graphics_draw_line(
    ctx,
    collar_left,
    collar_right
  );

  const int16_t needle_length =
      half_length > 20
          ? half_length / 3
          : 7;
  const GPoint needle_start =
      pen_shape_point(
        body_bottom,
        angle,
        4,
        0
      );
  const GPoint needle_end =
      pen_shape_point(
        body_bottom,
        angle,
        needle_length,
        0
      );

  graphics_context_set_stroke_color(
    ctx,
    outline_color
  );
  graphics_context_set_stroke_width(
    ctx,
    detail_width
  );
  graphics_draw_line(
    ctx,
    needle_start,
    needle_end
  );

  graphics_context_set_stroke_width(ctx, 1);
}

static void draw_pen_icon(
    GContext *ctx,
    GRect frame,
    const MedicationSettings *medication,
    const MedicationAppearance *appearance,
    GColor outline_color
) {
  const bool use_appearance =
      appearance && appearance->valid;
  const GColor body_color = {
    .argb =
        use_appearance
            ? appearance->primary_color
            : medication->color
  };
  const GColor accent_color = {
    .argb =
        use_appearance
            ? appearance->secondary_color
            : 240
  };
  const GColor body_outline =
      medication_appearance_darker_color(
        body_color.argb
      );

  draw_pen_shape(
    ctx,
    GPoint(
      frame.origin.x + frame.size.w / 2,
      frame.origin.y + frame.size.h / 2 - 1
    ),
    TRIG_MAX_ANGLE / 4,
    8,
    7,
    5,
    1,
    body_color,
    accent_color,
    body_outline
  );

  (void)outline_color;
}

void draw_pen_alert_animation(
    GContext *ctx,
    GRect bounds,
    int32_t scroll_offset_y,
    uint8_t phase,
    GColor outline_color
) {
  MedicationSymbol symbol;

  if (
    !active_medication_symbol(&symbol) ||
    symbol != MEDICATION_SYMBOL_PEN
  ) {
    return;
  }

  uint8_t pen_index = 0;

  if (
    !medication_group_first_index(
      MEDICATION_SYMBOL_PEN,
      &pen_index
    ) ||
    pen_index >= s_medication_count
  ) {
    return;
  }

  const MedicationSettings *pen =
      &s_medications[pen_index];

  if (!pen->icon_set) {
    return;
  }

  phase %= ARRAY_LENGTH(s_pen_alert_y_offsets);

  const int16_t center_y = (int16_t)(
    bounds.origin.y +
    bounds.size.h / 2 +
    scroll_offset_y +
    s_pen_alert_y_offsets[phase]
  );

  if (
    center_y < bounds.origin.y - 96 ||
    center_y > bounds.origin.y + bounds.size.h + 96
  ) {
    return;
  }

  const MedicationAppearance *appearance =
      pen_index < s_medication_appearance_count
          ? &s_medication_appearances[pen_index]
          : NULL;
  const bool use_appearance =
      appearance && appearance->valid;

  const GColor body_color = {
    .argb =
        use_appearance
            ? appearance->primary_color
            : pen->color
  };
  const GColor accent_color = {
    .argb =
        use_appearance
            ? appearance->secondary_color
            : 240
  };
  const GColor body_outline =
      medication_appearance_darker_color(
        body_color.argb
      );

  /*
   * The pen points downwards and only rocks a few degrees around vertical.
   * It is deliberately much larger than the list icon.
   */
  const int32_t angle =
      TRIG_MAX_ANGLE / 4 +
      ((int32_t)s_pen_alert_angle_degrees[phase] *
       TRIG_MAX_ANGLE) /
          360;

  draw_pen_shape(
    ctx,
    GPoint(
      bounds.origin.x + bounds.size.w / 2,
      center_y + 7
    ),
    angle,
    62,
    20,
    14,
    2,
    body_color,
    accent_color,
    body_outline
  );

  /*
   * Beim Pen ist der obere Bereich des Alarm-Screens frei.
   * Den Medikamentennamen dort direkt zeigen, damit er schon vor dem
   * Herunterscrollen zu den Einnahmedetails eindeutig erkennbar ist.
   *
   * scroll_offset_y koppelt den Namen an dieselbe Alarmseite wie den Pen.
   */
  graphics_context_set_text_color(
    ctx,
    outline_color
  );

  graphics_draw_text(
    ctx,
    pen->name,
    fonts_get_system_font(
      FONT_KEY_GOTHIC_28_BOLD
    ),
    GRect(
      bounds.origin.x + 8,
      bounds.origin.y +
          scroll_offset_y +
          4,
      bounds.size.w - 16,
      MEDICATION_NAME_LINE_HEIGHT
    ),
    GTextOverflowModeTrailingEllipsis,
    GTextAlignmentCenter,
    NULL
  );
}

static void draw_medication_icon(
    GContext *ctx,
    GRect frame,
    const MedicationSettings *medication,
    const MedicationAppearance *appearance,
    GColor outline_color
) {
  if (
    !medication ||
    !medication->icon_set
  ) {
    return;
  }

  if (
    medication->symbol ==
        MEDICATION_SYMBOL_PEN
  ) {
    draw_pen_icon(
      ctx,
      frame,
      medication,
      appearance,
      outline_color
    );
    return;
  }

  draw_tablet_icon(
    ctx,
    frame,
    medication,
    appearance,
    outline_color
  );
}

static void draw_physics_round_line(
    GContext *ctx,
    GPoint start,
    GPoint end,
    uint8_t radius,
    GColor color
) {
  /*
   * The previous renderer filled one circle for every pixel along the line.
   * A thick native line plus two end caps produces the same rounded capsule
   * with only three drawing primitives.
   */
  graphics_context_set_stroke_color(ctx, color);
  graphics_context_set_stroke_width(
    ctx,
    (uint8_t)(radius * 2)
  );
  graphics_draw_line(ctx, start, end);

  graphics_context_set_fill_color(ctx, color);
  graphics_fill_circle(ctx, start, radius);
  graphics_fill_circle(ctx, end, radius);
  graphics_context_set_stroke_width(ctx, 1);
}

static void draw_physics_capsule(
    GContext *ctx,
    GPoint center,
    int32_t angle,
    int16_t half_length,
    uint8_t radius,
    GColor fill_color,
    GColor outline_color,
    bool draw_divider
) {
  const int16_t dx = (int16_t)(
    ((int32_t)cos_lookup(angle) *
     half_length) /
    TRIG_MAX_RATIO
  );
  const int16_t dy = (int16_t)(
    ((int32_t)sin_lookup(angle) *
     half_length) /
    TRIG_MAX_RATIO
  );
  const GPoint start = GPoint(
    center.x - dx,
    center.y - dy
  );
  const GPoint end = GPoint(
    center.x + dx,
    center.y + dy
  );

  draw_physics_round_line(
    ctx,
    start,
    end,
    radius + PILL_RENDER_OUTLINE_PX,
    outline_color
  );
  draw_physics_round_line(
    ctx,
    start,
    end,
    radius,
    fill_color
  );

  if (draw_divider) {
    const int32_t divider_angle =
        angle + TRIG_MAX_ANGLE / 4;
    const int16_t divider_dx = (int16_t)(
      ((int32_t)cos_lookup(divider_angle) *
       (radius - 1)) /
      TRIG_MAX_RATIO
    );
    const int16_t divider_dy = (int16_t)(
      ((int32_t)sin_lookup(divider_angle) *
       (radius - 1)) /
      TRIG_MAX_RATIO
    );

    draw_physics_round_line(
      ctx,
      GPoint(
        center.x - divider_dx,
        center.y - divider_dy
      ),
      GPoint(
        center.x + divider_dx,
        center.y + divider_dy
      ),
      1,
      outline_color
    );
  }
}

static uint8_t medication_appearance_size(
    const MedicationAppearance *appearance
) {
  return
      appearance &&
      appearance->valid &&
      appearance->size >= 60 &&
      appearance->size <= 140
          ? appearance->size
          : 100;
}

static int16_t medication_appearance_scaled(
    int16_t base,
    uint8_t size
) {
  if (base <= 0) {
    return 0;
  }

  const int16_t scaled = (int16_t)(
    ((int32_t)base * size + 50) / 100
  );

  return scaled < 1 ? 1 : scaled;
}

static void medication_appearance_geometry(
    const MedicationAppearance *appearance,
    int16_t *line_half,
    int16_t *radius,
    int16_t *diamond_half
) {
  const uint8_t size =
      medication_appearance_size(appearance);
  int16_t local_line_half = 0;
  int16_t local_radius = 10;
  int16_t local_diamond_half = 0;

  switch (appearance ? appearance->shape : 0) {
    case 1:
      /*
       * Same height and curvature, but about 50 % more total width.
       * Width changes from 2 * (8 + 13) = 42 px to
       * 2 * (19 + 13) = 64 px. The phone percentage is still
       * applied afterwards as before.
       */
      local_line_half = 19;
      local_radius = 13;
      break;
    case 2:
      local_line_half = 8;
      local_radius = 7;
      break;
    case 3:
      local_radius = 0;
      local_diamond_half = 11;
      break;
    case 4:
      /*
       * 20 % thinner while keeping the same total capsule length:
       * old half extent 23 + 15 = 38 px
       * new half extent 26 + 12 = 38 px
       * The phone size percentage is still applied afterwards.
       */
      local_line_half = 26;
      local_radius = 12;
      break;
    case 0:
    default:
      break;
  }

  if (line_half) {
    *line_half = medication_appearance_scaled(local_line_half, size);
  }
  if (radius) {
    *radius = medication_appearance_scaled(local_radius, size);
  }
  if (diamond_half) {
    *diamond_half = medication_appearance_scaled(local_diamond_half, size);
  }
}

static void draw_physics_capsule_pixel_run(
    GContext *ctx,
    int16_t y,
    int16_t start_x,
    int16_t end_x,
    uint8_t color_index,
    GColor first_outline_color,
    GColor second_outline_color,
    GColor first_color,
    GColor second_color
) {
  if (
    color_index == 0 ||
    end_x < start_x
  ) {
    return;
  }

  GColor color = first_outline_color;

  if (color_index == 2) {
    color = first_color;
  } else if (color_index == 3) {
    color = second_color;
  } else if (color_index == 4) {
    color = second_outline_color;
  }

  graphics_context_set_stroke_color(ctx, color);
  graphics_context_set_stroke_width(ctx, 1);
  graphics_draw_line(
    ctx,
    GPoint(start_x, y),
    GPoint(end_x, y)
  );
}

static void draw_physics_two_color_capsule(
    GContext *ctx,
    GPoint center,
    int32_t angle,
    int16_t half_length,
    uint8_t radius,
    GColor first_color,
    GColor second_color,
    GColor first_outline_color,
    GColor second_outline_color
) {
  /*
   * Exact capsule geometry:
   * all pixels whose distance to the center line segment is <= radius.
   * The inner capsule uses exactly the same geometry with a radius reduced
   * by the shared outline thickness. The outline uses the same darker
   * colors as the imprint,
   * separately for both capsule halves, while retaining an even thickness.
   */
  const int32_t cosine = cos_lookup(angle);
  const int32_t sine = sin_lookup(angle);
  const int32_t absolute_cosine = abs_int32(cosine);
  const int32_t absolute_sine = abs_int32(sine);

  /*
   * The center segment rotates, but each end cap is a circle. A circle has
   * the full radius on both screen axes at every angle. Multiplying the
   * radius by sine/cosine clipped horizontal capsules at left/right and
   * vertical capsules at top/bottom.
   */
  const int16_t extent_x = (int16_t)(
    (absolute_cosine * half_length) /
    TRIG_MAX_RATIO +
    radius +
    2
  );
  const int16_t extent_y = (int16_t)(
    (absolute_sine * half_length) /
    TRIG_MAX_RATIO +
    radius +
    2
  );

  const int16_t minimum_x =
      (int16_t)(center.x - extent_x);
  const int16_t maximum_x =
      (int16_t)(center.x + extent_x);
  const int16_t minimum_y =
      (int16_t)(center.y - extent_y);
  const int16_t maximum_y =
      (int16_t)(center.y + extent_y);

  const int32_t cosine_q8 =
      (cosine * PILL_PHYSICS_Q8) /
      TRIG_MAX_RATIO;
  const int32_t sine_q8 =
      (sine * PILL_PHYSICS_Q8) /
      TRIG_MAX_RATIO;
  const int32_t half_length_q8 =
      half_length * PILL_PHYSICS_Q8;
  const int32_t outer_radius_q8 =
      radius * PILL_PHYSICS_Q8;
  const int32_t inner_radius_q8 =
      radius > PILL_RENDER_OUTLINE_PX
          ? (radius - PILL_RENDER_OUTLINE_PX) * PILL_PHYSICS_Q8
          : 0;
  const int32_t divider_half_width_q8 =
      PILL_PHYSICS_Q8 / 2;
  const int32_t outer_radius_squared =
      outer_radius_q8 * outer_radius_q8;
  const int32_t inner_radius_squared =
      inner_radius_q8 * inner_radius_q8;

  for (int16_t y = minimum_y; y <= maximum_y; y++) {
    const int32_t relative_y = y - center.y;
    const int32_t first_relative_x =
        minimum_x - center.x;

    int32_t local_x_q8 =
        first_relative_x * cosine_q8 +
        relative_y * sine_q8;
    int32_t local_y_q8 =
        -first_relative_x * sine_q8 +
        relative_y * cosine_q8;

    uint8_t run_color = 0;
    int16_t run_start_x = minimum_x;

    for (int16_t x = minimum_x; x <= maximum_x; x++) {
      int32_t nearest_x_q8 = local_x_q8;

      if (nearest_x_q8 < -half_length_q8) {
        nearest_x_q8 = -half_length_q8;
      } else if (nearest_x_q8 > half_length_q8) {
        nearest_x_q8 = half_length_q8;
      }

      const int32_t distance_x_q8 =
          local_x_q8 - nearest_x_q8;
      const int32_t distance_squared =
          distance_x_q8 * distance_x_q8 +
          local_y_q8 * local_y_q8;

      uint8_t pixel_color = 0;

      if (distance_squared <= outer_radius_squared) {
        if (
          distance_squared > inner_radius_squared ||
          abs_int32(local_x_q8) <=
              divider_half_width_q8
        ) {
          pixel_color =
              local_x_q8 < 0 ? 1 : 4;
        } else {
          pixel_color =
              local_x_q8 < 0 ? 2 : 3;
        }
      }

      if (pixel_color != run_color) {
        draw_physics_capsule_pixel_run(
          ctx,
          y,
          run_start_x,
          (int16_t)(x - 1),
          run_color,
          first_outline_color,
          second_outline_color,
          first_color,
          second_color
        );

        run_color = pixel_color;
        run_start_x = x;
      }

      local_x_q8 += cosine_q8;
      local_y_q8 -= sine_q8;
    }

    draw_physics_capsule_pixel_run(
      ctx,
      y,
      run_start_x,
      maximum_x,
      run_color,
      first_outline_color,
      second_outline_color,
      first_color,
      second_color
    );
  }
}

static void draw_physics_appearance_diamond(
    GContext *ctx,
    GPoint center,
    int32_t angle,
    PillRenderPathCache *cache,
    GColor fill_color,
    GColor outline_color
) {
  if (
    !cache ||
    !cache->outer_path ||
    !cache->inner_path
  ) {
    return;
  }

  gpath_move_to(cache->outer_path, center);
  gpath_rotate_to(cache->outer_path, angle);
  graphics_context_set_fill_color(ctx, outline_color);
  gpath_draw_filled(ctx, cache->outer_path);

  gpath_move_to(cache->inner_path, center);
  gpath_rotate_to(cache->inner_path, angle);
  graphics_context_set_fill_color(ctx, fill_color);
  gpath_draw_filled(ctx, cache->inner_path);
}

static GColor medication_appearance_darker_color(
    uint8_t argb
) {
  uint8_t red = (argb >> 4) & 3;
  uint8_t green = (argb >> 2) & 3;
  uint8_t blue = argb & 3;

  if (red > 0) {
    red--;
  }
  if (green > 0) {
    green--;
  }
  if (blue > 0) {
    blue--;
  }

  return (GColor) {
    .argb = (uint8_t)(
      0xc0 |
      (red << 4) |
      (green << 2) |
      blue
    )
  };
}

static const uint8_t *pill_physics_imprint_glyph(
    char character
) {
  if (character >= '0' && character <= '9') {
    return s_pill_imprint_digits[character - '0'];
  }

  if (character >= 'a' && character <= 'z') {
    character = (char)(character - 'a' + 'A');
  }

  if (character >= 'A' && character <= 'Z') {
    return s_pill_imprint_letters[character - 'A'];
  }

  switch (character) {
    case '|':
      return s_pill_imprint_bar;
    case '-':
      return s_pill_imprint_dash;
    case '/':
      return s_pill_imprint_slash;
    case '+':
      return s_pill_imprint_plus;
    case '.':
      return s_pill_imprint_dot;
    case ' ':
      return s_pill_imprint_space;
    default:
      return s_pill_imprint_unknown;
  }
}

static GPoint pill_physics_imprint_rotated_point(
    GPoint center,
    int32_t angle,
    int16_t local_x,
    int16_t local_y
) {
  const int32_t cosine = cos_lookup(angle);
  const int32_t sine = sin_lookup(angle);

  return GPoint(
    center.x + (int16_t)(
      ((int32_t)local_x * cosine -
       (int32_t)local_y * sine) /
      TRIG_MAX_RATIO
    ),
    center.y + (int16_t)(
      ((int32_t)local_x * sine +
       (int32_t)local_y * cosine) /
      TRIG_MAX_RATIO
    )
  );
}

static void draw_physics_appearance_imprint(
    GContext *ctx,
    GPoint center,
    int32_t angle,
    const MedicationAppearance *appearance,
    int16_t body_width,
    int16_t body_height
) {
  if (
    !appearance ||
    appearance->imprint[0] == '\0' ||
    body_width < 8 ||
    body_height < 8
  ) {
    return;
  }

  uint8_t character_count = 0;
  while (
    character_count < sizeof(appearance->imprint) &&
    appearance->imprint[character_count] != '\0'
  ) {
    character_count++;
  }

  if (character_count == 0) {
    return;
  }

  const bool round_tablet =
      appearance->shape == 0;
  const int16_t imprint_padding =
      round_tablet ? 2 : 4;
  const int16_t available_width =
      body_width - imprint_padding;
  const int16_t available_height =
      body_height - imprint_padding;

  /*
   * A round 100 % tablet has a 20 px inner diameter and a 24 px outer
   * diameter. With the old scaled two-pixel character gap, "20" needed
   * 22 px and was therefore reduced to a tiny 1x bitmap. Keep a single
   * physical pixel between characters on round tablets and choose the
   * largest complete scale that fits inside the outlined body.
   */
  int16_t scale = 1;

  for (
    int16_t candidate = PILL_IMPRINT_MAX_SCALE;
    candidate >= 1;
    candidate--
  ) {
    const int16_t candidate_gap =
        round_tablet
            ? 1
            : PILL_IMPRINT_GLYPH_GAP * candidate;
    const int16_t candidate_width = (int16_t)(
      character_count *
          PILL_IMPRINT_GLYPH_WIDTH * candidate +
      (character_count - 1) * candidate_gap
    );
    const int16_t candidate_height =
        PILL_IMPRINT_GLYPH_HEIGHT * candidate;

    if (
      candidate_width <= available_width &&
      candidate_height <= available_height
    ) {
      scale = candidate;
      break;
    }
  }

  const int16_t glyph_gap =
      round_tablet
          ? 1
          : PILL_IMPRINT_GLYPH_GAP * scale;
  const int16_t text_width = (int16_t)(
    character_count *
        PILL_IMPRINT_GLYPH_WIDTH * scale +
    (character_count - 1) * glyph_gap
  );
  const int16_t text_height =
      PILL_IMPRINT_GLYPH_HEIGHT * scale;
  const int16_t text_left =
      (int16_t)(-text_width / 2);
  const int16_t text_top =
      (int16_t)(-text_height / 2);

  const GColor primary_imprint_color =
      medication_appearance_darker_color(
        appearance->primary_color
      );
  const GColor secondary_imprint_color =
      appearance->shape == 4
          ? medication_appearance_darker_color(
              appearance->secondary_color
            )
          : primary_imprint_color;
  uint8_t current_imprint_argb = 0;

  for (
    uint8_t character_index = 0;
    character_index < character_count;
    character_index++
  ) {
    const uint8_t *glyph =
        pill_physics_imprint_glyph(
          appearance->imprint[character_index]
        );
    const int16_t character_x = (int16_t)(
      text_left +
      character_index *
          (PILL_IMPRINT_GLYPH_WIDTH * scale + glyph_gap)
    );

    for (
      uint8_t row = 0;
      row < PILL_IMPRINT_GLYPH_HEIGHT;
      row++
    ) {
      for (
        uint8_t column = 0;
        column < PILL_IMPRINT_GLYPH_WIDTH;
        column++
      ) {
        if (
          !(glyph[row] &
            (1 << (PILL_IMPRINT_GLYPH_WIDTH - 1 - column)))
        ) {
          continue;
        }

        const int16_t local_x = (int16_t)(
          character_x + column * scale + scale / 2
        );
        const int16_t local_y = (int16_t)(
          text_top + row * scale + scale / 2
        );
        const GPoint pixel_center =
            pill_physics_imprint_rotated_point(
              center,
              angle,
              local_x,
              local_y
            );
        const int16_t pixel_half = scale / 2;
        const GColor imprint_color =
            appearance->shape == 4 && local_x >= 0
                ? secondary_imprint_color
                : primary_imprint_color;

        if (current_imprint_argb != imprint_color.argb) {
          graphics_context_set_fill_color(
            ctx,
            imprint_color
          );
          current_imprint_argb = imprint_color.argb;
        }

        /*
         * Filled pixel blocks are reliable even for short glyph strokes.
         * Their positions rotate with the pill; unlike graphics_draw_text(),
         * the complete imprint therefore follows body->angle.
         */
        graphics_fill_rect(
          ctx,
          GRect(
            pixel_center.x - pixel_half,
            pixel_center.y - pixel_half,
            scale,
            scale
          ),
          0,
          GCornerNone
        );
      }
    }
  }

}

static int16_t pill_physics_cubic_component(
    int16_t p0,
    int16_t p1,
    int16_t p2,
    int16_t p3,
    int32_t t_q12
) {
  const int64_t scale = PHYSICS_ROUNDED_OVAL_Q12;
  const int64_t t = t_q12;
  const int64_t u = scale - t;
  const int64_t scale_cubed = scale * scale * scale;

  const int64_t value =
      u * u * u * p0 +
      3 * u * u * t * p1 +
      3 * u * t * t * p2 +
      t * t * t * p3;

  if (value >= 0) {
    return (int16_t)(
      (value + scale_cubed / 2) /
      scale_cubed
    );
  }

  return (int16_t)(
    -((-value + scale_cubed / 2) /
      scale_cubed)
  );
}

static void pill_physics_rounded_oval_quadrant(
    GPoint *points,
    uint8_t point_offset,
    int16_t p0_x,
    int16_t p0_y,
    int16_t p1_x,
    int16_t p1_y,
    int16_t p2_x,
    int16_t p2_y,
    int16_t p3_x,
    int16_t p3_y
) {
  for (
    uint8_t step = 0;
    step < PHYSICS_ROUNDED_OVAL_QUADRANT_STEPS;
    step++
  ) {
    /*
     * The endpoint belongs to the following quadrant. Omitting it here keeps
     * every vertex unique while the final GPath closes the last gap itself.
     */
    const int32_t t_q12 = (int32_t)(
      ((int64_t)step * PHYSICS_ROUNDED_OVAL_Q12) /
      PHYSICS_ROUNDED_OVAL_QUADRANT_STEPS
    );

    points[point_offset + step] = GPoint(
      pill_physics_cubic_component(
        p0_x,
        p1_x,
        p2_x,
        p3_x,
        t_q12
      ),
      pill_physics_cubic_component(
        p0_y,
        p1_y,
        p2_y,
        p3_y,
        t_q12
      )
    );
  }
}

static void pill_render_path_cache_clear(
    PillRenderPathCache *cache
) {
  if (!cache) {
    return;
  }

  if (cache->outer_path) {
    gpath_destroy(cache->outer_path);
  }
  if (cache->inner_path) {
    gpath_destroy(cache->inner_path);
  }

  *cache = (PillRenderPathCache) { 0 };
}

static bool pill_render_cache_build_oval(
    PillRenderPathCache *cache,
    int16_t half_width,
    int16_t half_height
) {
  const int16_t outer_half_width =
      half_width + PILL_RENDER_OUTLINE_PX;
  const int16_t outer_half_height =
      half_height + PILL_RENDER_OUTLINE_PX;

  const int16_t outer_end_control = (int16_t)(
    ((int32_t)outer_half_height *
     PHYSICS_ROUNDED_OVAL_END_CONTROL_NUM +
     PHYSICS_ROUNDED_OVAL_CONTROL_DEN / 2) /
    PHYSICS_ROUNDED_OVAL_CONTROL_DEN
  );
  const int16_t outer_top_control = (int16_t)(
    ((int32_t)outer_half_width *
     PHYSICS_ROUNDED_OVAL_TOP_CONTROL_NUM +
     PHYSICS_ROUNDED_OVAL_CONTROL_DEN / 2) /
    PHYSICS_ROUNDED_OVAL_CONTROL_DEN
  );
  const int16_t inner_end_control = (int16_t)(
    ((int32_t)half_height *
     PHYSICS_ROUNDED_OVAL_END_CONTROL_NUM +
     PHYSICS_ROUNDED_OVAL_CONTROL_DEN / 2) /
    PHYSICS_ROUNDED_OVAL_CONTROL_DEN
  );
  const int16_t inner_top_control = (int16_t)(
    ((int32_t)half_width *
     PHYSICS_ROUNDED_OVAL_TOP_CONTROL_NUM +
     PHYSICS_ROUNDED_OVAL_CONTROL_DEN / 2) /
    PHYSICS_ROUNDED_OVAL_CONTROL_DEN
  );

  pill_physics_rounded_oval_quadrant(
    cache->outer_points,
    0,
    outer_half_width, 0,
    outer_half_width, (int16_t)-outer_end_control,
    outer_top_control, (int16_t)-outer_half_height,
    0, (int16_t)-outer_half_height
  );
  pill_physics_rounded_oval_quadrant(
    cache->outer_points,
    PHYSICS_ROUNDED_OVAL_QUADRANT_STEPS,
    0, (int16_t)-outer_half_height,
    (int16_t)-outer_top_control, (int16_t)-outer_half_height,
    (int16_t)-outer_half_width, (int16_t)-outer_end_control,
    (int16_t)-outer_half_width, 0
  );
  pill_physics_rounded_oval_quadrant(
    cache->outer_points,
    PHYSICS_ROUNDED_OVAL_QUADRANT_STEPS * 2,
    (int16_t)-outer_half_width, 0,
    (int16_t)-outer_half_width, outer_end_control,
    (int16_t)-outer_top_control, outer_half_height,
    0, outer_half_height
  );
  pill_physics_rounded_oval_quadrant(
    cache->outer_points,
    PHYSICS_ROUNDED_OVAL_QUADRANT_STEPS * 3,
    0, outer_half_height,
    outer_top_control, outer_half_height,
    outer_half_width, outer_end_control,
    outer_half_width, 0
  );

  pill_physics_rounded_oval_quadrant(
    cache->inner_points,
    0,
    half_width, 0,
    half_width, (int16_t)-inner_end_control,
    inner_top_control, (int16_t)-half_height,
    0, (int16_t)-half_height
  );
  pill_physics_rounded_oval_quadrant(
    cache->inner_points,
    PHYSICS_ROUNDED_OVAL_QUADRANT_STEPS,
    0, (int16_t)-half_height,
    (int16_t)-inner_top_control, (int16_t)-half_height,
    (int16_t)-half_width, (int16_t)-inner_end_control,
    (int16_t)-half_width, 0
  );
  pill_physics_rounded_oval_quadrant(
    cache->inner_points,
    PHYSICS_ROUNDED_OVAL_QUADRANT_STEPS * 2,
    (int16_t)-half_width, 0,
    (int16_t)-half_width, inner_end_control,
    (int16_t)-inner_top_control, half_height,
    0, half_height
  );
  pill_physics_rounded_oval_quadrant(
    cache->inner_points,
    PHYSICS_ROUNDED_OVAL_QUADRANT_STEPS * 3,
    0, half_height,
    inner_top_control, half_height,
    half_width, inner_end_control,
    half_width, 0
  );

  cache->outer_info = (GPathInfo) {
    .num_points = PHYSICS_ROUNDED_OVAL_PATH_POINTS,
    .points = cache->outer_points
  };
  cache->inner_info = (GPathInfo) {
    .num_points = PHYSICS_ROUNDED_OVAL_PATH_POINTS,
    .points = cache->inner_points
  };

  cache->outer_path =
      gpath_create(&cache->outer_info);
  cache->inner_path =
      gpath_create(&cache->inner_info);

  return
      cache->outer_path != NULL &&
      cache->inner_path != NULL;
}

static bool pill_render_cache_build_diamond(
    PillRenderPathCache *cache,
    int16_t outer_half
) {
  const int16_t inner_half =
      outer_half > PILL_RENDER_DIAMOND_OUTLINE_PX
          ? outer_half - PILL_RENDER_DIAMOND_OUTLINE_PX
          : 1;

  cache->outer_points[0] = GPoint(outer_half, 0);
  cache->outer_points[1] = GPoint(0, outer_half);
  cache->outer_points[2] = GPoint((int16_t)-outer_half, 0);
  cache->outer_points[3] = GPoint(0, (int16_t)-outer_half);

  cache->inner_points[0] = GPoint(inner_half, 0);
  cache->inner_points[1] = GPoint(0, inner_half);
  cache->inner_points[2] = GPoint((int16_t)-inner_half, 0);
  cache->inner_points[3] = GPoint(0, (int16_t)-inner_half);

  cache->outer_info = (GPathInfo) {
    .num_points = 4,
    .points = cache->outer_points
  };
  cache->inner_info = (GPathInfo) {
    .num_points = 4,
    .points = cache->inner_points
  };

  cache->outer_path =
      gpath_create(&cache->outer_info);
  cache->inner_path =
      gpath_create(&cache->inner_info);

  return
      cache->outer_path != NULL &&
      cache->inner_path != NULL;
}

static PillRenderPathCache *pill_render_icon_path_cache(
    uint8_t shape
) {
  PillRenderPathCache *cache = NULL;
  bool built = false;

  if (shape == 1) {
    cache = &s_pill_icon_oval_cache;

    if (cache->valid) {
      return cache;
    }

    pill_render_path_cache_clear(cache);
    built = pill_render_cache_build_oval(
      cache,
      10,
      6
    );
  } else if (shape == 3) {
    cache = &s_pill_icon_diamond_cache;

    if (cache->valid) {
      return cache;
    }

    pill_render_path_cache_clear(cache);
    built = pill_render_cache_build_diamond(
      cache,
      11
    );
  }

  if (!cache || !built) {
    if (cache) {
      pill_render_path_cache_clear(cache);
    }
    return NULL;
  }

  cache->valid = true;
  cache->shape = shape;
  return cache;
}

static PillRenderPathCache *pill_render_path_cache_prepare(
    uint8_t medication_index,
    const MedicationAppearance *appearance,
    int16_t line_half,
    int16_t radius,
    int16_t diamond_half
) {
  if (
    medication_index >= MAX_MEDICATIONS ||
    !appearance ||
    (appearance->shape != 1 &&
     appearance->shape != 3)
  ) {
    return NULL;
  }

  PillRenderPathCache *cache =
      &s_pill_render_path_cache[medication_index];

  if (
    cache->valid &&
    cache->shape == appearance->shape &&
    cache->line_half == line_half &&
    cache->radius == radius &&
    cache->diamond_half == diamond_half
  ) {
    return cache;
  }

  pill_render_path_cache_clear(cache);

  cache->shape = appearance->shape;
  cache->line_half = line_half;
  cache->radius = radius;
  cache->diamond_half = diamond_half;

  bool built = false;

  if (appearance->shape == 1) {
    built = pill_render_cache_build_oval(
      cache,
      line_half + radius,
      radius
    );
  } else {
    built = pill_render_cache_build_diamond(
      cache,
      diamond_half
    );
  }

  if (!built) {
    pill_render_path_cache_clear(cache);
    return NULL;
  }

  cache->valid = true;
  return cache;
}

void pill_renderer_deinit(void) {
  for (
    uint8_t index = 0;
    index < MAX_MEDICATIONS;
    index++
  ) {
    pill_render_path_cache_clear(
      &s_pill_render_path_cache[index]
    );
  }

  pill_render_path_cache_clear(
    &s_pill_icon_oval_cache
  );
  pill_render_path_cache_clear(
    &s_pill_icon_diamond_cache
  );
}

static void draw_physics_true_ellipse(
    GContext *ctx,
    GPoint center,
    int32_t angle,
    PillRenderPathCache *cache,
    GColor fill_color,
    GColor outline_color
) {
  if (
    !cache ||
    !cache->outer_path ||
    !cache->inner_path
  ) {
    return;
  }

  gpath_move_to(cache->outer_path, center);
  gpath_rotate_to(cache->outer_path, angle);
  graphics_context_set_fill_color(ctx, outline_color);
  gpath_draw_filled(ctx, cache->outer_path);

  gpath_move_to(cache->inner_path, center);
  gpath_rotate_to(cache->inner_path, angle);
  graphics_context_set_fill_color(ctx, fill_color);
  gpath_draw_filled(ctx, cache->inner_path);
}

static void draw_physics_pill_body(
    GContext *ctx,
    const PillPhysicsBody *body,
    int32_t arena_y
) {
  if (!body) {
    return;
  }

  MedicationRuntimeView view;

  if (
    !medication_runtime_view(
      body->medication_index,
      &view
    )
  ) {
    return;
  }

  const MedicationAppearance *appearance =
      &view.appearance;
  const GColor fill_color = {
    .argb = appearance->primary_color
  };
  const GColor second_color = {
    .argb = appearance->secondary_color
  };
  const GColor primary_outline_color =
      medication_appearance_darker_color(
        appearance->primary_color
      );
  const GColor secondary_outline_color =
      appearance->shape == 4
          ? medication_appearance_darker_color(
              appearance->secondary_color
            )
          : primary_outline_color;
  const GPoint center = GPoint(
    (int16_t)(body->x_q8 / PILL_PHYSICS_Q8),
    (int16_t)(arena_y + body->y_q8 / PILL_PHYSICS_Q8)
  );
  int16_t line_half;
  int16_t radius;
  int16_t diamond_half;

  medication_appearance_geometry(
    appearance,
    &line_half,
    &radius,
    &diamond_half
  );

  PillRenderPathCache *path_cache =
      pill_render_path_cache_prepare(
        body->medication_index,
        appearance,
        line_half,
        radius,
        diamond_half
      );

  switch (appearance->shape) {
    case 1:
      draw_physics_true_ellipse(
        ctx,
        center,
        body->angle,
        path_cache,
        fill_color,
        primary_outline_color
      );
      break;

    case 2:
      draw_physics_capsule(
        ctx,
        center,
        body->angle,
        line_half,
        (uint8_t)radius,
        fill_color,
        primary_outline_color,
        true
      );
      break;

    case 3:
      draw_physics_appearance_diamond(
        ctx,
        center,
        body->angle,
        path_cache,
        fill_color,
        primary_outline_color
      );
      break;

    case 4:
      draw_physics_two_color_capsule(
        ctx,
        center,
        body->angle,
        line_half,
        (uint8_t)radius,
        fill_color,
        second_color,
        primary_outline_color,
        secondary_outline_color
      );
      break;

    case 0:
    default:
      graphics_context_set_fill_color(
        ctx,
        primary_outline_color
      );
      graphics_fill_circle(
        ctx,
        center,
        (uint16_t)(radius + PILL_RENDER_OUTLINE_PX)
      );
      graphics_context_set_fill_color(ctx, fill_color);
      graphics_fill_circle(
        ctx,
        center,
        (uint16_t)radius
      );
      break;
  }

  const int16_t body_width =
      diamond_half > 0
          ? diamond_half * 2
          : appearance->shape == 0
              ? (radius + 2) * 2
              : (line_half + radius) * 2;
  const int16_t body_height =
      diamond_half > 0
          ? diamond_half * 2
          : appearance->shape == 0
              ? (radius + 2) * 2
              : radius * 2;

  draw_physics_appearance_imprint(
    ctx,
    center,
    body->angle,
    appearance,
    body_width,
    body_height
  );
}

void draw_physics_pills(
    GContext *ctx,
    GRect bounds,
    int32_t arena_y
) {
  const int32_t page_top =
      visual_canvas_offset_y();

  if (
    page_top >= bounds.size.h ||
    page_top + bounds.size.h <= 0
  ) {
    return;
  }

  const uint8_t body_count =
      pill_physics_body_count();

  for (
    uint8_t index = 0;
    index < body_count;
    index++
  ) {
    const PillPhysicsBody *body =
        pill_physics_body_at(index);

    if (!body) {
      continue;
    }

    draw_physics_pill_body(
      ctx,
      body,
      arena_y
    );
  }
}

static int16_t medication_name_width(
    const char *name
) {
  const GSize size =
      graphics_text_layout_get_content_size(
        name,
        s_medication_font,
        GRect(
          0,
          0,
          1000,
          MEDICATION_NAME_LINE_HEIGHT
        ),
        GTextOverflowModeFill,
        GTextAlignmentLeft
      );

  return size.w;
}

bool medication_name_needs_marquee(
    int row_index
) {
  if (
    row_index < 0 ||
    row_index >= LIST_ROW_COUNT ||
    !s_canvas_layer
  ) {
    return false;
  }

  const int8_t medication_index =
      s_row_medication_indices[row_index];

  if (
    medication_index < 0 ||
    medication_index >= (int8_t)s_medication_count
  ) {
    return false;
  }

  const GRect bounds =
      layer_get_bounds(s_canvas_layer);
  const int16_t available_width =
      bounds.size.w -
      MEDICATION_ICON_TEXT_X -
      MEDICATION_ICON_TEXT_RIGHT;

  return
      medication_name_width(
        s_medications[medication_index].name
      ) > available_width;
}

static int16_t medication_marquee_offset(
    int16_t overflow
) {
  if (overflow <= 0) {
    return 0;
  }

  const uint16_t travel_ticks =
      (uint16_t)(
        (
          overflow +
          MEDICATION_MARQUEE_PIXELS_PER_TICK - 1
        ) /
        MEDICATION_MARQUEE_PIXELS_PER_TICK
      );

  const uint16_t cycle_ticks =
      MEDICATION_MARQUEE_START_PAUSE_TICKS +
      travel_ticks +
      MEDICATION_MARQUEE_END_PAUSE_TICKS;

  if (cycle_ticks == 0) {
    return 0;
  }

  const uint16_t phase =
      s_medication_marquee_tick % cycle_ticks;

  if (
    phase <
        MEDICATION_MARQUEE_START_PAUSE_TICKS
  ) {
    return 0;
  }

  const uint16_t travel_phase =
      phase -
      MEDICATION_MARQUEE_START_PAUSE_TICKS;

  if (travel_phase >= travel_ticks) {
    return overflow;
  }

  const int16_t offset =
      (int16_t)(
        travel_phase *
        MEDICATION_MARQUEE_PIXELS_PER_TICK
      );

  return offset < overflow
      ? offset
      : overflow;
}

static void draw_medication_name(
    GContext *ctx,
    const char *name,
    GRect frame,
    int row_index
) {
  const int16_t name_width =
      medication_name_width(name);

  const int16_t overflow =
      name_width - frame.size.w;

  if (
    row_index != s_medication_marquee_row ||
    overflow <= 0
  ) {
    graphics_draw_text(
      ctx,
      name,
      s_medication_font,
      frame,
      GTextOverflowModeTrailingEllipsis,
      GTextAlignmentLeft,
      NULL
    );
    return;
  }

  const int16_t offset =
      medication_marquee_offset(overflow);

  graphics_draw_text(
    ctx,
    name,
    s_medication_font,
    GRect(
      frame.origin.x - offset,
      frame.origin.y,
      name_width + 2,
      frame.size.h
    ),
    GTextOverflowModeFill,
    GTextAlignmentLeft,
    NULL
  );
}

void draw_intake_medications(
    GContext *ctx,
    GRect bounds,
    int32_t scroll_offset_y,
    GColor text_color,
    GColor background_color
) {
  (void)background_color;

  if (INTAKE_ROW_COUNT <= 0) {
    return;
  }

  int16_t page_height = 228;
  if (s_canvas_layer) {
    page_height = layer_get_bounds(s_canvas_layer).size.h;
  }

  const int32_t page_start = page_height;
  const int32_t rows_y =
      page_start +
      (page_height - MEDICATION_ROW_HEIGHT) / 2 +
      scroll_offset_y;
  const int32_t label_y =
      rows_y - MEDICATION_HEADER_HEIGHT;

  if (
    rows_y + INTAKE_ROW_COUNT *
        (MEDICATION_ROW_HEIGHT + MEDICATION_ROW_GAP) < 0 ||
    label_y > bounds.size.h
  ) {
    return;
  }

  graphics_context_set_text_color(ctx, text_color);
  graphics_draw_text(
    ctx,
    s_language == APP_LANGUAGE_ENGLISH ? "INTAKE" : "EINNAHME",
    s_header_font,
    GRect(bounds.origin.x + 10, (int16_t)label_y,
          bounds.size.w - 20, MEDICATION_HEADER_HEIGHT),
    GTextOverflowModeTrailingEllipsis,
    GTextAlignmentCenter,
    NULL
  );

  for (int row = 0; row < INTAKE_ROW_COUNT; row++) {
    const int32_t row_y =
        rows_y + row *
        (MEDICATION_ROW_HEIGHT + MEDICATION_ROW_GAP);

    if (row_y + MEDICATION_ROW_HEIGHT < 0 || row_y > bounds.size.h) {
      continue;
    }

    const int8_t medication_index =
        s_intake_medication_indices[row];
    if (medication_index < 0 ||
        medication_index >= (int8_t)s_medication_count) {
      continue;
    }

    const MedicationSettings *medication =
        &s_medications[medication_index];
    char label[MEDICATION_LABEL_LENGTH];

    if (medication->quantity > 1) {
      snprintf(
        label,
        sizeof(label),
        "%u x %s",
        (unsigned int)medication->quantity,
        medication->name
      );
    } else {
      snprintf(label, sizeof(label), "%s", medication->name);
    }

    const GRect name_frame = GRect(
      bounds.origin.x + MEDICATION_ICON_TEXT_X,
      (int16_t)row_y + MEDICATION_NAME_LINE_Y,
      bounds.size.w - MEDICATION_ICON_TEXT_X - MEDICATION_ICON_TEXT_RIGHT,
      MEDICATION_NAME_LINE_HEIGHT
    );

    graphics_draw_text(ctx, label, s_medication_font, name_frame,
                       GTextOverflowModeTrailingEllipsis,
                       GTextAlignmentLeft, NULL);
    graphics_draw_text(
      ctx, medication->effect, s_medication_detail_font,
      GRect(name_frame.origin.x, (int16_t)row_y + MEDICATION_EFFECT_LINE_Y,
            name_frame.size.w, MEDICATION_DETAIL_LINE_HEIGHT),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL
    );
    graphics_draw_text(
      ctx, medication->dosage, s_medication_detail_font,
      GRect(name_frame.origin.x, (int16_t)row_y + MEDICATION_DOSAGE_LINE_Y,
            name_frame.size.w, MEDICATION_DETAIL_LINE_HEIGHT),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL
    );

    MedicationRuntimeView runtime_view;
    const MedicationAppearance *appearance = NULL;

    if (
      medication_runtime_view(
        (uint8_t)medication_index,
        &runtime_view
      )
    ) {
      appearance = &runtime_view.appearance;
    }

    draw_medication_icon(
      ctx,
      GRect(bounds.origin.x + MEDICATION_ICON_LEFT,
            (int16_t)row_y +
                (MEDICATION_ROW_HEIGHT - MEDICATION_ICON_SIZE) / 2,
            MEDICATION_ICON_SIZE, MEDICATION_ICON_SIZE),
      medication, appearance, text_color
    );
  }
}

void draw_medications(
    GContext *ctx,
    GRect bounds,
    int32_t scroll_offset_y,
    GColor text_color,
    GColor background_color
) {
  int16_t page_height = 228;

  if (s_canvas_layer) {
    page_height =
        layer_get_bounds(s_canvas_layer).size.h;
  }

  const int32_t page_start =
      scroll_all_medications_page_start_y();

  const int32_t rows_y =
      page_start +
      (page_height - MEDICATION_ROW_HEIGHT) / 2 +
      scroll_offset_y;

  const int32_t label_y =
      rows_y - MEDICATION_HEADER_HEIGHT;

  if (
    rows_y +
        LIST_ROW_COUNT *
            (MEDICATION_ROW_HEIGHT + MEDICATION_ROW_GAP) < 0 ||
    label_y > bounds.size.h
  ) {
    return;
  }

  graphics_context_set_text_color(ctx, text_color);

  graphics_draw_text(
    ctx,
    s_language == APP_LANGUAGE_ENGLISH
        ? "ALL MEDICATIONS"
        : "ALLE MEDIKAMENTE",
    s_header_font,
    GRect(
      bounds.origin.x + 10,
      (int16_t)label_y,
      bounds.size.w - 20,
      MEDICATION_HEADER_HEIGHT
    ),
    GTextOverflowModeTrailingEllipsis,
    GTextAlignmentCenter,
    NULL
  );

  for (int index = 0; index < LIST_ROW_COUNT; index++) {
    const int32_t row_y =
        rows_y +
        index * (MEDICATION_ROW_HEIGHT + MEDICATION_ROW_GAP);

    if (
      row_y + MEDICATION_ROW_HEIGHT < 0 ||
      row_y > bounds.size.h
    ) {
      continue;
    }

    const int8_t medication_index =
        s_row_medication_indices[index];

    if (
      medication_index < 0 ||
      medication_index >=
          (int8_t)s_medication_count
    ) {
      continue;
    }

    const MedicationSettings *medication =
        &s_medications[medication_index];

    const GRect name_frame = GRect(
      bounds.origin.x +
          MEDICATION_ICON_TEXT_X,
      (int16_t)row_y +
          MEDICATION_NAME_LINE_Y,
      bounds.size.w -
          MEDICATION_ICON_TEXT_X -
          MEDICATION_ICON_TEXT_RIGHT,
      MEDICATION_NAME_LINE_HEIGHT
    );

    draw_medication_name(
      ctx,
      medication->name,
      name_frame,
      index
    );

    graphics_draw_text(
      ctx,
      medication->effect,
      s_medication_detail_font,
      GRect(
        name_frame.origin.x,
        (int16_t)row_y +
            MEDICATION_EFFECT_LINE_Y,
        name_frame.size.w,
        MEDICATION_DETAIL_LINE_HEIGHT
      ),
      GTextOverflowModeTrailingEllipsis,
      GTextAlignmentLeft,
      NULL
    );

    char dosage_and_next[48];

    snprintf(
      dosage_and_next,
      sizeof(dosage_and_next),
      "%s",
      medication->dosage
    );

    if (
      medication->time ==
          MEDICATION_TIME_INTERVAL
    ) {
      const time_t next_intake =
          alarm_next_medication_timestamp(
            (uint8_t)medication_index
          );

      if (next_intake > 0) {
        struct tm *next_local =
            localtime(&next_intake);

        if (next_local) {
          snprintf(
            dosage_and_next,
            sizeof(dosage_and_next),
            "%s%s%02d.%02d %02d:%02d",
            medication->dosage,
            medication->dosage[0] != '\0'
                ? "  "
                : "",
            next_local->tm_mday,
            next_local->tm_mon + 1,
            next_local->tm_hour,
            next_local->tm_min
          );
        }
      }
    }

    graphics_draw_text(
      ctx,
      dosage_and_next,
      s_medication_detail_font,
      GRect(
        name_frame.origin.x,
        (int16_t)row_y +
            MEDICATION_DOSAGE_LINE_Y,
        name_frame.size.w,
        MEDICATION_DETAIL_LINE_HEIGHT
      ),
      GTextOverflowModeTrailingEllipsis,
      GTextAlignmentLeft,
      NULL
    );

    if (index == s_medication_marquee_row) {
      graphics_context_set_fill_color(
        ctx,
        background_color
      );
      graphics_fill_rect(
        ctx,
        GRect(
          bounds.origin.x,
          (int16_t)row_y,
          MEDICATION_ICON_TEXT_X,
          MEDICATION_ROW_HEIGHT
        ),
        0,
        GCornerNone
      );
    }

    MedicationRuntimeView runtime_view;
    const MedicationAppearance *appearance = NULL;

    if (
      medication_runtime_view(
        (uint8_t)medication_index,
        &runtime_view
      )
    ) {
      appearance = &runtime_view.appearance;
    }

    draw_medication_icon(
      ctx,
      GRect(
        bounds.origin.x +
            MEDICATION_ICON_LEFT,
        (int16_t)row_y +
            (
              MEDICATION_ROW_HEIGHT -
              MEDICATION_ICON_SIZE
            ) /
            2,
        MEDICATION_ICON_SIZE,
        MEDICATION_ICON_SIZE
      ),
      medication,
      appearance,
      text_color
    );
  }
}
