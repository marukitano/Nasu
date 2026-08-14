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

/*
 * Limit display-plane gravity to sin(45 degrees) ~= 0.707 g. Tilting the
 * watch farther therefore keeps the same direction, but no longer increases
 * the force pressing settled pills into the wall.
 */
#define PILL_RB_MAX_TILT_MG 707

/*
 * Mass follows the configured pill scale squared: 60 % -> 0.36 mass,
 * 100 % -> 1.00 mass and 140 % -> 1.96 mass. Gravity remains identical
 * for every pill; mass only changes collision response and rotation.
 */
#define PILL_RB_DEFAULT_MASS_Q8 PILL_PHYSICS_Q8
#define PILL_RB_MIN_MASS_Q8 (PILL_PHYSICS_Q8 / 4)
#define PILL_RB_MAX_MASS_Q8 (4 * PILL_PHYSICS_Q8)

/*
 * Stable per-body differences emulate small variations in coating and
 * contact surface. The values are deterministic, never random per frame.
 */
static const int8_t
    s_pill_rb_friction_variation_percent[] = {
  -8, 6, -4, 8, 0, -6, 5, -7, 3, 7, -2, -2
};

static bool pill_physics_medication_is_visible(
    const MedicationSettings *medication
);
static int32_t pill_rb_clamp_angle(int32_t angle);
static int32_t pill_rb_clamp_int32(
    int32_t value,
    int32_t minimum,
    int32_t maximum
);
static uint64_t pill_rb_integer_sqrt64(uint64_t value);
static uint8_t pill_rb_medication_size(
    uint8_t medication_index
);
static uint16_t pill_rb_mass_from_size_q8(uint8_t size);
static uint16_t pill_rb_surface_friction_for_body_q8(
    uint8_t medication_index,
    uint8_t body_index
);
static int32_t pill_rb_mass_q8(
    const PillPhysicsBody *body
);
static int32_t pill_rb_inverse_mass_q8(
    const PillPhysicsBody *body
);
static void pill_rb_collision_geometry(
    uint8_t medication_index,
    uint8_t *half_length,
    uint8_t *radius
);
static int32_t pill_rb_inertia_q8(
    const PillPhysicsBody *body
);
static void pill_rb_segment_endpoints(
    const PillPhysicsBody *body,
    int32_t *ax_q8,
    int32_t *ay_q8,
    int32_t *bx_q8,
    int32_t *by_q8
);
static int32_t pill_rb_rotational_velocity_q8(
    int32_t angular_velocity,
    int16_t lever_pixels
);
static void pill_rb_contact_velocity(
    const PillPhysicsBody *body,
    int16_t lever_x,
    int16_t lever_y,
    int32_t *velocity_x_q8,
    int32_t *velocity_y_q8
);
static int32_t pill_rb_cross_lever_normal(
    int16_t lever_x,
    int16_t lever_y,
    int32_t normal_x_q12,
    int32_t normal_y_q12
);
static void pill_rb_apply_impulse(
    PillPhysicsBody *body,
    int32_t impulse_q8,
    int32_t normal_x_q12,
    int32_t normal_y_q12,
    int16_t lever_x,
    int16_t lever_y,
    int direction
);
static int32_t pill_rb_effective_mass_q8(
    const PillPhysicsBody *first,
    int16_t first_lever_x,
    int16_t first_lever_y,
    const PillPhysicsBody *second,
    int16_t second_lever_x,
    int16_t second_lever_y,
    int32_t normal_x_q12,
    int32_t normal_y_q12
);
static int32_t pill_rb_solve_contact_impulse(
    PillPhysicsBody *first,
    PillPhysicsBody *second,
    int16_t first_lever_x,
    int16_t first_lever_y,
    int16_t second_lever_x,
    int16_t second_lever_y,
    int32_t normal_x_q12,
    int32_t normal_y_q12
);
static void pill_rb_initialize_body(
    PillPhysicsBody *body,
    uint8_t medication_index,
    uint8_t body_index
);
static bool pill_rb_add_body(uint8_t medication_index);
static void pill_rb_closest_point_on_segment(
    int32_t point_x_q8,
    int32_t point_y_q8,
    int32_t start_x_q8,
    int32_t start_y_q8,
    int32_t end_x_q8,
    int32_t end_y_q8,
    int32_t *closest_x_q8,
    int32_t *closest_y_q8
);
static int64_t pill_rb_cross_q8(
    int32_t ax_q8,
    int32_t ay_q8,
    int32_t bx_q8,
    int32_t by_q8
);
static bool pill_rb_segment_intersection(
    int32_t a0x_q8,
    int32_t a0y_q8,
    int32_t a1x_q8,
    int32_t a1y_q8,
    int32_t b0x_q8,
    int32_t b0y_q8,
    int32_t b1x_q8,
    int32_t b1y_q8,
    int32_t *intersection_x_q8,
    int32_t *intersection_y_q8
);
static void pill_rb_consider_closest_pair(
    int32_t candidate_ax_q8,
    int32_t candidate_ay_q8,
    int32_t candidate_bx_q8,
    int32_t candidate_by_q8,
    uint64_t *best_distance_squared,
    int32_t *best_ax_q8,
    int32_t *best_ay_q8,
    int32_t *best_bx_q8,
    int32_t *best_by_q8
);
static void pill_rb_closest_segment_points(
    const PillPhysicsBody *first,
    const PillPhysicsBody *second,
    int32_t *first_x_q8,
    int32_t *first_y_q8,
    int32_t *second_x_q8,
    int32_t *second_y_q8
);
static bool pill_rb_solve_pair(
    PillPhysicsBody *first,
    PillPhysicsBody *second
);
static bool pill_rb_solve_wall(
    PillPhysicsBody *body,
    uint8_t wall,
    int32_t minimum_x_q8,
    int32_t maximum_x_q8,
    int32_t minimum_y_q8,
    int32_t maximum_y_q8
);
static bool pill_rb_solve_swiss_emblem(
    PillPhysicsBody *body,
    int16_t arena_y
);
static int16_t pill_rb_tilt_magnitude(
    int16_t x,
    int16_t y
);
static void pill_rb_drive_from_tilt(
    const PillPhysicsBody *body,
    int16_t *drive_x,
    int16_t *drive_y
);
static void pill_physics_schedule_tick(uint32_t delay_ms);
static void pill_physics_tick(void *context);
static void pill_physics_accel_handler(
    AccelData *data,
    uint32_t num_samples
);

static bool pill_physics_medication_is_visible(
    const MedicationSettings *medication
) {
  return
      medication &&
      medication->enabled &&
      medication->icon_set &&
      medication->symbol == MEDICATION_SYMBOL_PILL &&
      medication->time ==
          (uint8_t)current_medication_time() &&
      !s_pills_confirmed;
}

static int32_t pill_rb_clamp_angle(int32_t angle) {
  while (angle < 0) {
    angle += TRIG_MAX_ANGLE;
  }
  while (angle >= TRIG_MAX_ANGLE) {
    angle -= TRIG_MAX_ANGLE;
  }
  return angle;
}

static int32_t pill_rb_clamp_int32(
    int32_t value,
    int32_t minimum,
    int32_t maximum
) {
  if (value < minimum) {
    return minimum;
  }
  if (value > maximum) {
    return maximum;
  }
  return value;
}

static uint64_t pill_rb_integer_sqrt64(uint64_t value) {
  uint64_t result = 0;
  uint64_t bit = (uint64_t)1 << 62;

  while (bit > value) {
    bit >>= 2;
  }

  while (bit != 0) {
    if (value >= result + bit) {
      value -= result + bit;
      result = (result >> 1) + bit;
    } else {
      result >>= 1;
    }
    bit >>= 2;
  }

  return result;
}

static uint8_t pill_rb_medication_size(
    uint8_t medication_index
) {
  uint8_t size = 100;

  if (
    medication_index < s_medication_appearance_count &&
    s_medication_appearances[medication_index].valid
  ) {
    size = s_medication_appearances[medication_index].size;
  }

  if (size < 60) {
    return 60;
  }
  if (size > 140) {
    return 140;
  }
  return size;
}

static uint16_t pill_rb_mass_from_size_q8(uint8_t size) {
  const int32_t mass_q8 = (int32_t)(
    ((int32_t)size * size * PILL_PHYSICS_Q8 + 5000) /
    10000
  );

  return (uint16_t)pill_rb_clamp_int32(
    mass_q8,
    PILL_RB_MIN_MASS_Q8,
    PILL_RB_MAX_MASS_Q8
  );
}

static uint16_t pill_rb_surface_friction_for_body_q8(
    uint8_t medication_index,
    uint8_t body_index
) {
  const uint8_t variation_index = (uint8_t)(
    (body_index + medication_index * 3u) %
    ARRAY_LENGTH(s_pill_rb_friction_variation_percent)
  );
  const int16_t percent = (int16_t)(
    100 +
    s_pill_rb_friction_variation_percent[variation_index]
  );

  return (uint16_t)(
    ((int32_t)PILL_PHYSICS_Q8 * percent + 50) / 100
  );
}

static int32_t pill_rb_mass_q8(
    const PillPhysicsBody *body
) {
  return body && body->mass_q8 > 0
      ? body->mass_q8
      : PILL_RB_DEFAULT_MASS_Q8;
}

static int32_t pill_rb_inverse_mass_q8(
    const PillPhysicsBody *body
) {
  const int32_t mass_q8 = pill_rb_mass_q8(body);

  return (int32_t)(
    ((int64_t)PILL_PHYSICS_Q8 * PILL_PHYSICS_Q8 +
     mass_q8 / 2) /
    mass_q8
  );
}

static void pill_rb_collision_geometry(
    uint8_t medication_index,
    uint8_t *half_length,
    uint8_t *radius
) {
  uint8_t shape = 0;
  uint8_t size = 100;

  if (medication_index < s_medication_count) {
    shape = s_medications[medication_index].shape;
  }

  if (
    medication_index < s_medication_appearance_count &&
    s_medication_appearances[medication_index].valid
  ) {
    const MedicationAppearance *appearance =
        &s_medication_appearances[medication_index];
    shape = appearance->shape;
    size = appearance->size;
  }

  int16_t local_half_length;
  int16_t local_radius;

  switch (shape) {
    case 1:
      local_half_length = 19;
      local_radius = 13;
      break;
    case 2:
      local_half_length = 8;
      local_radius = 7;
      break;
    case 4:
      local_half_length = 26;
      local_radius = 12;
      break;
    case 3:
      local_half_length = 0;
      local_radius = 13;
      break;
    case 0:
    default:
      local_half_length = 0;
      local_radius = 10;
      break;
  }

  local_half_length = (int16_t)(
    ((int32_t)local_half_length * size + 50) / 100
  );
  local_radius = (int16_t)(
    ((int32_t)local_radius * size + 50) / 100
  );

  /* Small safety envelope prevents visible interpenetration. */
  if (local_half_length > 0) {
    local_half_length += 1;
  }
  local_radius += 2;

  if (local_half_length < 0) {
    local_half_length = 0;
  } else if (local_half_length > 40) {
    local_half_length = 40;
  }

  if (local_radius < 5) {
    local_radius = 5;
  } else if (local_radius > 28) {
    local_radius = 28;
  }

  *half_length = (uint8_t)local_half_length;
  *radius = (uint8_t)local_radius;
}

static int32_t pill_rb_inertia_q8(
    const PillPhysicsBody *body
) {
  const int32_t half_length =
      body->collision_half_length;
  const int32_t radius =
      body->collision_radius;
  int32_t geometry =
      (half_length * half_length) / 3 +
      (radius * radius) / 2;

  if (geometry < 24) {
    geometry = 24;
  }

  return geometry * pill_rb_mass_q8(body);
}

static void pill_rb_segment_endpoints(
    const PillPhysicsBody *body,
    int32_t *ax_q8,
    int32_t *ay_q8,
    int32_t *bx_q8,
    int32_t *by_q8
) {
  const int32_t dx_q8 = (int32_t)(
    ((int64_t)cos_lookup(body->angle) *
     body->collision_half_length *
     PILL_PHYSICS_Q8) /
    TRIG_MAX_RATIO
  );
  const int32_t dy_q8 = (int32_t)(
    ((int64_t)sin_lookup(body->angle) *
     body->collision_half_length *
     PILL_PHYSICS_Q8) /
    TRIG_MAX_RATIO
  );

  *ax_q8 = body->x_q8 - dx_q8;
  *ay_q8 = body->y_q8 - dy_q8;
  *bx_q8 = body->x_q8 + dx_q8;
  *by_q8 = body->y_q8 + dy_q8;
}

static int32_t pill_rb_rotational_velocity_q8(
    int32_t angular_velocity,
    int16_t lever_pixels
) {
  return (int32_t)(
    ((int64_t)angular_velocity *
     lever_pixels *
     PILL_RB_ANGLE_TO_LINEAR_NUM) /
    PILL_RB_ANGLE_TO_LINEAR_DEN
  );
}

static void pill_rb_contact_velocity(
    const PillPhysicsBody *body,
    int16_t lever_x,
    int16_t lever_y,
    int32_t *velocity_x_q8,
    int32_t *velocity_y_q8
) {
  *velocity_x_q8 =
      body->vx_q8 -
      pill_rb_rotational_velocity_q8(
        body->angular_velocity,
        lever_y
      );
  *velocity_y_q8 =
      body->vy_q8 +
      pill_rb_rotational_velocity_q8(
        body->angular_velocity,
        lever_x
      );
}

static int32_t pill_rb_cross_lever_normal(
    int16_t lever_x,
    int16_t lever_y,
    int32_t normal_x_q12,
    int32_t normal_y_q12
) {
  return (int32_t)(
    ((int64_t)lever_x * normal_y_q12 -
     (int64_t)lever_y * normal_x_q12) /
    PILL_RB_PARAMETER_Q12
  );
}

static void pill_rb_apply_impulse(
    PillPhysicsBody *body,
    int32_t impulse_q8,
    int32_t normal_x_q12,
    int32_t normal_y_q12,
    int16_t lever_x,
    int16_t lever_y,
    int direction
) {
  const int32_t inverse_mass_q8 =
      pill_rb_inverse_mass_q8(body);

  body->vx_q8 += (int32_t)(
    ((int64_t)direction * impulse_q8 *
     normal_x_q12 * inverse_mass_q8) /
    ((int64_t)PILL_RB_PARAMETER_Q12 *
     PILL_PHYSICS_Q8)
  );
  body->vy_q8 += (int32_t)(
    ((int64_t)direction * impulse_q8 *
     normal_y_q12 * inverse_mass_q8) /
    ((int64_t)PILL_RB_PARAMETER_Q12 *
     PILL_PHYSICS_Q8)
  );

  const int32_t cross =
      pill_rb_cross_lever_normal(
        lever_x,
        lever_y,
        normal_x_q12,
        normal_y_q12
      );
  const int32_t inertia_q8 =
      pill_rb_inertia_q8(body);

  body->angular_velocity += (int32_t)(
    ((int64_t)direction * cross * impulse_q8 *
     PILL_RB_RAD_TO_ANGLE_NUM) /
    inertia_q8
  );

  body->angular_velocity = pill_rb_clamp_int32(
    body->angular_velocity,
    -PILL_RB_MAX_ANGULAR,
    PILL_RB_MAX_ANGULAR
  );
}

static int32_t pill_rb_effective_mass_q8(
    const PillPhysicsBody *first,
    int16_t first_lever_x,
    int16_t first_lever_y,
    const PillPhysicsBody *second,
    int16_t second_lever_x,
    int16_t second_lever_y,
    int32_t normal_x_q12,
    int32_t normal_y_q12
) {
  int32_t denominator_q8 =
      pill_rb_inverse_mass_q8(first);

  const int32_t first_cross =
      pill_rb_cross_lever_normal(
        first_lever_x,
        first_lever_y,
        normal_x_q12,
        normal_y_q12
      );
  denominator_q8 += (int32_t)(
    ((int64_t)first_cross * first_cross *
     PILL_PHYSICS_Q8 * PILL_PHYSICS_Q8) /
    pill_rb_inertia_q8(first)
  );

  if (second) {
    denominator_q8 += pill_rb_inverse_mass_q8(second);
    const int32_t second_cross =
        pill_rb_cross_lever_normal(
          second_lever_x,
          second_lever_y,
          normal_x_q12,
          normal_y_q12
        );
    denominator_q8 += (int32_t)(
      ((int64_t)second_cross * second_cross *
       PILL_PHYSICS_Q8 * PILL_PHYSICS_Q8) /
      pill_rb_inertia_q8(second)
    );
  }

  return denominator_q8 > 0
      ? denominator_q8
      : PILL_PHYSICS_Q8;
}

static int32_t pill_rb_solve_contact_impulse(
    PillPhysicsBody *first,
    PillPhysicsBody *second,
    int16_t first_lever_x,
    int16_t first_lever_y,
    int16_t second_lever_x,
    int16_t second_lever_y,
    int32_t normal_x_q12,
    int32_t normal_y_q12
) {
  int32_t first_vx_q8;
  int32_t first_vy_q8;
  int32_t second_vx_q8 = 0;
  int32_t second_vy_q8 = 0;

  pill_rb_contact_velocity(
    first,
    first_lever_x,
    first_lever_y,
    &first_vx_q8,
    &first_vy_q8
  );

  if (second) {
    pill_rb_contact_velocity(
      second,
      second_lever_x,
      second_lever_y,
      &second_vx_q8,
      &second_vy_q8
    );
  }

  const int32_t relative_vx_q8 =
      first_vx_q8 - second_vx_q8;
  const int32_t relative_vy_q8 =
      first_vy_q8 - second_vy_q8;
  const int32_t normal_velocity_q8 = (int32_t)(
    ((int64_t)relative_vx_q8 * normal_x_q12 +
     (int64_t)relative_vy_q8 * normal_y_q12) /
    PILL_RB_PARAMETER_Q12
  );

  if (normal_velocity_q8 >= 0) {
    return 0;
  }

  const int32_t denominator_q8 =
      pill_rb_effective_mass_q8(
        first,
        first_lever_x,
        first_lever_y,
        second,
        second_lever_x,
        second_lever_y,
        normal_x_q12,
        normal_y_q12
      );

  /*
   * Resting contacts must not bounce. At tiny closing speeds the previous
   * 20 % restitution re-injected energy every frame, especially where two
   * walls constrained the same pill. Real impact bounce is retained only
   * above the low-speed threshold.
   */
  const int32_t closing_speed_q8 =
      -normal_velocity_q8;
  const int32_t restitution_num =
      closing_speed_q8 >
          PILL_RB_RESTITUTION_SPEED_Q8
          ? PILL_RB_RESTITUTION_NUM
          : 0;
  const int32_t normal_impulse_q8 = (int32_t)(
    ((int64_t)closing_speed_q8 *
     (PILL_RB_RESTITUTION_DEN +
      restitution_num) *
     PILL_PHYSICS_Q8) /
    ((int64_t)PILL_RB_RESTITUTION_DEN *
     denominator_q8)
  );

  pill_rb_apply_impulse(
    first,
    normal_impulse_q8,
    normal_x_q12,
    normal_y_q12,
    first_lever_x,
    first_lever_y,
    1
  );

  if (second) {
    pill_rb_apply_impulse(
      second,
      normal_impulse_q8,
      normal_x_q12,
      normal_y_q12,
      second_lever_x,
      second_lever_y,
      -1
    );
  }

  /* Coulomb friction at the same contact point. */
  const int32_t tangent_x_q12 = -normal_y_q12;
  const int32_t tangent_y_q12 = normal_x_q12;

  pill_rb_contact_velocity(
    first,
    first_lever_x,
    first_lever_y,
    &first_vx_q8,
    &first_vy_q8
  );
  if (second) {
    pill_rb_contact_velocity(
      second,
      second_lever_x,
      second_lever_y,
      &second_vx_q8,
      &second_vy_q8
    );
  }

  const int32_t tangent_velocity_q8 = (int32_t)(
    ((int64_t)(first_vx_q8 - second_vx_q8) *
         tangent_x_q12 +
     (int64_t)(first_vy_q8 - second_vy_q8) *
         tangent_y_q12) /
    PILL_RB_PARAMETER_Q12
  );

  const int32_t tangent_denominator_q8 =
      pill_rb_effective_mass_q8(
        first,
        first_lever_x,
        first_lever_y,
        second,
        second_lever_x,
        second_lever_y,
        tangent_x_q12,
        tangent_y_q12
      );

  int32_t tangent_impulse_q8 = (int32_t)(
    ((int64_t)(-tangent_velocity_q8) *
     PILL_PHYSICS_Q8) /
    tangent_denominator_q8
  );
  int32_t friction_scale_q8 =
      first->surface_friction_q8 > 0
          ? first->surface_friction_q8
          : PILL_PHYSICS_Q8;
  if (second) {
    const int32_t second_friction_q8 =
        second->surface_friction_q8 > 0
            ? second->surface_friction_q8
            : PILL_PHYSICS_Q8;
    friction_scale_q8 =
        (friction_scale_q8 + second_friction_q8) / 2;
  }

  const int32_t friction_limit_q8 = (int32_t)(
    ((int64_t)normal_impulse_q8 *
     PILL_RB_FRICTION_NUM * friction_scale_q8) /
    ((int64_t)PILL_RB_FRICTION_DEN *
     PILL_PHYSICS_Q8)
  );

  tangent_impulse_q8 = pill_rb_clamp_int32(
    tangent_impulse_q8,
    -friction_limit_q8,
    friction_limit_q8
  );

  pill_rb_apply_impulse(
    first,
    tangent_impulse_q8,
    tangent_x_q12,
    tangent_y_q12,
    first_lever_x,
    first_lever_y,
    1
  );
  if (second) {
    pill_rb_apply_impulse(
      second,
      tangent_impulse_q8,
      tangent_x_q12,
      tangent_y_q12,
      second_lever_x,
      second_lever_y,
      -1
    );
  }

  return normal_impulse_q8;
}

static void pill_rb_initialize_body(
    PillPhysicsBody *body,
    uint8_t medication_index,
    uint8_t body_index
) {
  if (!body || medication_index >= s_medication_count) {
    return;
  }

  uint8_t half_length;
  uint8_t radius;
  pill_rb_collision_geometry(
    medication_index,
    &half_length,
    &radius
  );
  const uint8_t size =
      pill_rb_medication_size(medication_index);

  int16_t arena_width = 228;
  int16_t arena_height = 228;
  int16_t arena_y = 0;

  if (s_canvas_layer) {
    const GRect bounds = layer_get_bounds(s_canvas_layer);
    arena_width = bounds.size.w;
    arena_height = bounds.size.h;
    arena_y = (int16_t)pill_arena_origin_y();
  }

  const uint8_t column = body_index % 3;
  const uint8_t row = body_index / 3;
  int16_t x = (int16_t)(
    ((int32_t)(column * 2 + 1) * arena_width) / 6
  );
  int16_t screen_y = (int16_t)(22 + row * 38);
  int16_t local_y = (int16_t)(screen_y - arena_y);
  const int16_t extent =
      radius + half_length + PILL_PHYSICS_EDGE_MARGIN;

  /*
   * Original Nasu behaviour: pills already lie inside the display when the
   * alert opens. From the first physics tick onward they react normally to
   * the current watch orientation.
   */
  if (x < extent) {
    x = extent;
  } else if (x > arena_width - extent) {
    x = arena_width - extent;
  }

  if (screen_y < extent) {
    local_y = extent - arena_y;
  } else if (screen_y > arena_height - extent) {
    local_y = arena_height - extent - arena_y;
  }

  *body = (PillPhysicsBody) {
    .x_q8 = (int32_t)x * PILL_PHYSICS_Q8,
    .y_q8 = (int32_t)local_y * PILL_PHYSICS_Q8,
    .vx_q8 = 0,
    .vy_q8 = 0,
    .angle =
        ((int32_t)(body_index * 5u + 1u) *
         TRIG_MAX_ANGLE) /
        32,
    .angular_velocity = 0,
    .medication_index = medication_index,
    .collision_radius = radius,
    .collision_half_length = half_length,
    .entered_arena = true,
    .mass_q8 = pill_rb_mass_from_size_q8(size),
    .surface_friction_q8 =
        pill_rb_surface_friction_for_body_q8(
          medication_index,
          body_index
        )
  };
}

static bool pill_rb_add_body(uint8_t medication_index) {
  if (
    s_pill_physics_body_count >=
        PILL_PHYSICS_MAX_BODIES
  ) {
    return false;
  }

  pill_rb_initialize_body(
    &s_pill_physics_bodies[
      s_pill_physics_body_count
    ],
    medication_index,
    s_pill_physics_body_count
  );
  s_pill_physics_body_count++;
  return true;
}

void pill_physics_rebuild(void) {
  memset(
    s_pill_physics_bodies,
    0,
    sizeof(s_pill_physics_bodies)
  );
  s_pill_physics_body_count = 0;

  uint8_t remaining[MAX_MEDICATIONS] = { 0 };

  for (
    uint8_t index = 0;
    index < s_medication_count &&
    s_pill_physics_body_count <
        PILL_PHYSICS_MAX_BODIES;
    index++
  ) {
    const MedicationSettings *medication =
        &s_medications[index];

    if (!pill_physics_medication_is_visible(medication)) {
      continue;
    }

    (void)pill_rb_add_body(index);
    remaining[index] =
        medication->quantity > 0
            ? medication->quantity - 1
            : 0;
  }

  bool added = true;
  while (
    added &&
    s_pill_physics_body_count <
        PILL_PHYSICS_MAX_BODIES
  ) {
    added = false;
    for (
      uint8_t index = 0;
      index < s_medication_count &&
      s_pill_physics_body_count <
          PILL_PHYSICS_MAX_BODIES;
      index++
    ) {
      if (remaining[index] == 0) {
        continue;
      }
      if (pill_rb_add_body(index)) {
        remaining[index]--;
        added = true;
      }
    }
  }

  s_pill_physics_gravity_x = 0;
  s_pill_physics_gravity_y = 0;
  s_pill_physics_last_target_x = 0;
  s_pill_physics_last_target_y = 0;
  s_pill_physics_quiet_frames = 0;
  s_pill_physics_sensor_quiet_samples = 0;

  if (s_canvas_layer) {
    layer_mark_dirty(s_canvas_layer);
  }
}

static void pill_rb_closest_point_on_segment(
    int32_t point_x_q8,
    int32_t point_y_q8,
    int32_t start_x_q8,
    int32_t start_y_q8,
    int32_t end_x_q8,
    int32_t end_y_q8,
    int32_t *closest_x_q8,
    int32_t *closest_y_q8
) {
  const int32_t segment_x_q8 = end_x_q8 - start_x_q8;
  const int32_t segment_y_q8 = end_y_q8 - start_y_q8;
  const int64_t length_squared =
      (int64_t)segment_x_q8 * segment_x_q8 +
      (int64_t)segment_y_q8 * segment_y_q8;

  if (length_squared <= 0) {
    *closest_x_q8 = start_x_q8;
    *closest_y_q8 = start_y_q8;
    return;
  }

  int64_t parameter_q12 =
      ((int64_t)(point_x_q8 - start_x_q8) *
           segment_x_q8 +
       (int64_t)(point_y_q8 - start_y_q8) *
           segment_y_q8) *
      PILL_RB_PARAMETER_Q12 /
      length_squared;

  if (parameter_q12 < 0) {
    parameter_q12 = 0;
  } else if (parameter_q12 > PILL_RB_PARAMETER_Q12) {
    parameter_q12 = PILL_RB_PARAMETER_Q12;
  }

  *closest_x_q8 = start_x_q8 + (int32_t)(
    ((int64_t)segment_x_q8 * parameter_q12) /
    PILL_RB_PARAMETER_Q12
  );
  *closest_y_q8 = start_y_q8 + (int32_t)(
    ((int64_t)segment_y_q8 * parameter_q12) /
    PILL_RB_PARAMETER_Q12
  );
}

static int64_t pill_rb_cross_q8(
    int32_t ax_q8,
    int32_t ay_q8,
    int32_t bx_q8,
    int32_t by_q8
) {
  return
      (int64_t)ax_q8 * by_q8 -
      (int64_t)ay_q8 * bx_q8;
}

static bool pill_rb_segment_intersection(
    int32_t a0x_q8,
    int32_t a0y_q8,
    int32_t a1x_q8,
    int32_t a1y_q8,
    int32_t b0x_q8,
    int32_t b0y_q8,
    int32_t b1x_q8,
    int32_t b1y_q8,
    int32_t *intersection_x_q8,
    int32_t *intersection_y_q8
) {
  const int32_t rx_q8 = a1x_q8 - a0x_q8;
  const int32_t ry_q8 = a1y_q8 - a0y_q8;
  const int32_t sx_q8 = b1x_q8 - b0x_q8;
  const int32_t sy_q8 = b1y_q8 - b0y_q8;
  const int32_t qpx_q8 = b0x_q8 - a0x_q8;
  const int32_t qpy_q8 = b0y_q8 - a0y_q8;
  const int64_t denominator =
      pill_rb_cross_q8(
        rx_q8,
        ry_q8,
        sx_q8,
        sy_q8
      );

  if (denominator == 0) {
    return false;
  }

  const int64_t t_numerator =
      pill_rb_cross_q8(
        qpx_q8,
        qpy_q8,
        sx_q8,
        sy_q8
      );
  const int64_t u_numerator =
      pill_rb_cross_q8(
        qpx_q8,
        qpy_q8,
        rx_q8,
        ry_q8
      );

  if (
    (denominator > 0 &&
     (t_numerator < 0 || t_numerator > denominator ||
      u_numerator < 0 || u_numerator > denominator)) ||
    (denominator < 0 &&
     (t_numerator > 0 || t_numerator < denominator ||
      u_numerator > 0 || u_numerator < denominator))
  ) {
    return false;
  }

  const int64_t t_q12 =
      t_numerator * PILL_RB_PARAMETER_Q12 /
      denominator;
  *intersection_x_q8 = a0x_q8 + (int32_t)(
    ((int64_t)rx_q8 * t_q12) /
    PILL_RB_PARAMETER_Q12
  );
  *intersection_y_q8 = a0y_q8 + (int32_t)(
    ((int64_t)ry_q8 * t_q12) /
    PILL_RB_PARAMETER_Q12
  );
  return true;
}

static void pill_rb_consider_closest_pair(
    int32_t candidate_ax_q8,
    int32_t candidate_ay_q8,
    int32_t candidate_bx_q8,
    int32_t candidate_by_q8,
    uint64_t *best_distance_squared,
    int32_t *best_ax_q8,
    int32_t *best_ay_q8,
    int32_t *best_bx_q8,
    int32_t *best_by_q8
) {
  const int64_t dx_q8 =
      (int64_t)candidate_ax_q8 - candidate_bx_q8;
  const int64_t dy_q8 =
      (int64_t)candidate_ay_q8 - candidate_by_q8;
  const uint64_t distance_squared = (uint64_t)(
    dx_q8 * dx_q8 + dy_q8 * dy_q8
  );

  if (distance_squared >= *best_distance_squared) {
    return;
  }

  *best_distance_squared = distance_squared;
  *best_ax_q8 = candidate_ax_q8;
  *best_ay_q8 = candidate_ay_q8;
  *best_bx_q8 = candidate_bx_q8;
  *best_by_q8 = candidate_by_q8;
}

static void pill_rb_closest_segment_points(
    const PillPhysicsBody *first,
    const PillPhysicsBody *second,
    int32_t *first_x_q8,
    int32_t *first_y_q8,
    int32_t *second_x_q8,
    int32_t *second_y_q8
) {
  int32_t a0x_q8;
  int32_t a0y_q8;
  int32_t a1x_q8;
  int32_t a1y_q8;
  int32_t b0x_q8;
  int32_t b0y_q8;
  int32_t b1x_q8;
  int32_t b1y_q8;

  pill_rb_segment_endpoints(
    first,
    &a0x_q8,
    &a0y_q8,
    &a1x_q8,
    &a1y_q8
  );
  pill_rb_segment_endpoints(
    second,
    &b0x_q8,
    &b0y_q8,
    &b1x_q8,
    &b1y_q8
  );

  int32_t intersection_x_q8;
  int32_t intersection_y_q8;
  if (
    pill_rb_segment_intersection(
      a0x_q8,
      a0y_q8,
      a1x_q8,
      a1y_q8,
      b0x_q8,
      b0y_q8,
      b1x_q8,
      b1y_q8,
      &intersection_x_q8,
      &intersection_y_q8
    )
  ) {
    *first_x_q8 = intersection_x_q8;
    *first_y_q8 = intersection_y_q8;
    *second_x_q8 = intersection_x_q8;
    *second_y_q8 = intersection_y_q8;
    return;
  }

  uint64_t best_distance_squared = UINT64_MAX;
  int32_t closest_x_q8;
  int32_t closest_y_q8;

  pill_rb_closest_point_on_segment(
    a0x_q8,
    a0y_q8,
    b0x_q8,
    b0y_q8,
    b1x_q8,
    b1y_q8,
    &closest_x_q8,
    &closest_y_q8
  );
  pill_rb_consider_closest_pair(
    a0x_q8,
    a0y_q8,
    closest_x_q8,
    closest_y_q8,
    &best_distance_squared,
    first_x_q8,
    first_y_q8,
    second_x_q8,
    second_y_q8
  );

  pill_rb_closest_point_on_segment(
    a1x_q8,
    a1y_q8,
    b0x_q8,
    b0y_q8,
    b1x_q8,
    b1y_q8,
    &closest_x_q8,
    &closest_y_q8
  );
  pill_rb_consider_closest_pair(
    a1x_q8,
    a1y_q8,
    closest_x_q8,
    closest_y_q8,
    &best_distance_squared,
    first_x_q8,
    first_y_q8,
    second_x_q8,
    second_y_q8
  );

  pill_rb_closest_point_on_segment(
    b0x_q8,
    b0y_q8,
    a0x_q8,
    a0y_q8,
    a1x_q8,
    a1y_q8,
    &closest_x_q8,
    &closest_y_q8
  );
  pill_rb_consider_closest_pair(
    closest_x_q8,
    closest_y_q8,
    b0x_q8,
    b0y_q8,
    &best_distance_squared,
    first_x_q8,
    first_y_q8,
    second_x_q8,
    second_y_q8
  );

  pill_rb_closest_point_on_segment(
    b1x_q8,
    b1y_q8,
    a0x_q8,
    a0y_q8,
    a1x_q8,
    a1y_q8,
    &closest_x_q8,
    &closest_y_q8
  );
  pill_rb_consider_closest_pair(
    closest_x_q8,
    closest_y_q8,
    b1x_q8,
    b1y_q8,
    &best_distance_squared,
    first_x_q8,
    first_y_q8,
    second_x_q8,
    second_y_q8
  );
}

static bool pill_rb_solve_pair(
    PillPhysicsBody *first,
    PillPhysicsBody *second
) {
  int32_t first_line_x_q8;
  int32_t first_line_y_q8;
  int32_t second_line_x_q8;
  int32_t second_line_y_q8;

  pill_rb_closest_segment_points(
    first,
    second,
    &first_line_x_q8,
    &first_line_y_q8,
    &second_line_x_q8,
    &second_line_y_q8
  );

  int32_t delta_x_q8 =
      first_line_x_q8 - second_line_x_q8;
  int32_t delta_y_q8 =
      first_line_y_q8 - second_line_y_q8;
  uint64_t distance_squared =
      (uint64_t)((int64_t)delta_x_q8 * delta_x_q8) +
      (uint64_t)((int64_t)delta_y_q8 * delta_y_q8);
  int32_t distance_q8 = (int32_t)
      pill_rb_integer_sqrt64(distance_squared);
  const int32_t minimum_distance_q8 =
      (first->collision_radius +
       second->collision_radius) *
      PILL_PHYSICS_Q8;

  if (distance_q8 >= minimum_distance_q8) {
    return false;
  }

  if (distance_q8 <= 0) {
    delta_x_q8 = first->x_q8 - second->x_q8;
    delta_y_q8 = first->y_q8 - second->y_q8;
    distance_squared =
        (uint64_t)((int64_t)delta_x_q8 * delta_x_q8) +
        (uint64_t)((int64_t)delta_y_q8 * delta_y_q8);
    distance_q8 = (int32_t)
        pill_rb_integer_sqrt64(distance_squared);

    if (distance_q8 <= 0) {
      delta_x_q8 = PILL_PHYSICS_Q8;
      delta_y_q8 = 0;
      distance_q8 = PILL_PHYSICS_Q8;
    }
  }

  const int32_t normal_x_q12 = (int32_t)(
    ((int64_t)delta_x_q8 *
     PILL_RB_PARAMETER_Q12) /
    distance_q8
  );
  const int32_t normal_y_q12 = (int32_t)(
    ((int64_t)delta_y_q8 *
     PILL_RB_PARAMETER_Q12) /
    distance_q8
  );
  int32_t penetration_q8 =
      minimum_distance_q8 - distance_q8;

  if (penetration_q8 > PILL_RB_POSITION_SLOP_Q8) {
    penetration_q8 -= PILL_RB_POSITION_SLOP_Q8;
    const int32_t first_inverse_mass_q8 =
        pill_rb_inverse_mass_q8(first);
    const int32_t second_inverse_mass_q8 =
        pill_rb_inverse_mass_q8(second);
    const int32_t total_inverse_mass_q8 =
        first_inverse_mass_q8 +
        second_inverse_mass_q8;
    const int32_t first_correction_q8 = (int32_t)(
      ((int64_t)penetration_q8 *
       first_inverse_mass_q8) /
      total_inverse_mass_q8
    );
    const int32_t second_correction_q8 =
        penetration_q8 - first_correction_q8;

    first->x_q8 += (int32_t)(
      ((int64_t)normal_x_q12 * first_correction_q8) /
      PILL_RB_PARAMETER_Q12
    );
    first->y_q8 += (int32_t)(
      ((int64_t)normal_y_q12 * first_correction_q8) /
      PILL_RB_PARAMETER_Q12
    );
    second->x_q8 -= (int32_t)(
      ((int64_t)normal_x_q12 * second_correction_q8) /
      PILL_RB_PARAMETER_Q12
    );
    second->y_q8 -= (int32_t)(
      ((int64_t)normal_y_q12 * second_correction_q8) /
      PILL_RB_PARAMETER_Q12
    );
  }

  const int32_t first_surface_x_q8 =
      first_line_x_q8 - (int32_t)(
        ((int64_t)normal_x_q12 *
         first->collision_radius *
         PILL_PHYSICS_Q8) /
        PILL_RB_PARAMETER_Q12
      );
  const int32_t first_surface_y_q8 =
      first_line_y_q8 - (int32_t)(
        ((int64_t)normal_y_q12 *
         first->collision_radius *
         PILL_PHYSICS_Q8) /
        PILL_RB_PARAMETER_Q12
      );
  const int32_t second_surface_x_q8 =
      second_line_x_q8 + (int32_t)(
        ((int64_t)normal_x_q12 *
         second->collision_radius *
         PILL_PHYSICS_Q8) /
        PILL_RB_PARAMETER_Q12
      );
  const int32_t second_surface_y_q8 =
      second_line_y_q8 + (int32_t)(
        ((int64_t)normal_y_q12 *
         second->collision_radius *
         PILL_PHYSICS_Q8) /
        PILL_RB_PARAMETER_Q12
      );
  const int32_t contact_x_q8 =
      (first_surface_x_q8 + second_surface_x_q8) / 2;
  const int32_t contact_y_q8 =
      (first_surface_y_q8 + second_surface_y_q8) / 2;

  pill_rb_solve_contact_impulse(
    first,
    second,
    (int16_t)(
      (contact_x_q8 - first->x_q8) /
      PILL_PHYSICS_Q8
    ),
    (int16_t)(
      (contact_y_q8 - first->y_q8) /
      PILL_PHYSICS_Q8
    ),
    (int16_t)(
      (contact_x_q8 - second->x_q8) /
      PILL_PHYSICS_Q8
    ),
    (int16_t)(
      (contact_y_q8 - second->y_q8) /
      PILL_PHYSICS_Q8
    ),
    normal_x_q12,
    normal_y_q12
  );
  return true;
}

static bool pill_rb_solve_wall(
    PillPhysicsBody *body,
    uint8_t wall,
    int32_t minimum_x_q8,
    int32_t maximum_x_q8,
    int32_t minimum_y_q8,
    int32_t maximum_y_q8
) {
  int32_t ax_q8;
  int32_t ay_q8;
  int32_t bx_q8;
  int32_t by_q8;
  pill_rb_segment_endpoints(
    body,
    &ax_q8,
    &ay_q8,
    &bx_q8,
    &by_q8
  );

  const int32_t radius_q8 =
      body->collision_radius * PILL_PHYSICS_Q8;
  int32_t penetration_q8 = 0;
  int32_t normal_x_q12 = 0;
  int32_t normal_y_q12 = 0;
  int32_t support_x_q8 = body->x_q8;
  int32_t support_y_q8 = body->y_q8;

  switch (wall) {
    case 0: {
      const int32_t minimum_surface_q8 =
          (ax_q8 < bx_q8 ? ax_q8 : bx_q8) -
          radius_q8;
      penetration_q8 =
          minimum_x_q8 - minimum_surface_q8;
      normal_x_q12 = PILL_RB_PARAMETER_Q12;
      if (abs_int32(ax_q8 - bx_q8) <= PILL_RB_FLAT_CONTACT_TOLERANCE_Q8) {
        support_x_q8 = (ax_q8 + bx_q8) / 2;
        support_y_q8 = (ay_q8 + by_q8) / 2;
      } else if (ax_q8 < bx_q8) {
        support_x_q8 = ax_q8;
        support_y_q8 = ay_q8;
      } else {
        support_x_q8 = bx_q8;
        support_y_q8 = by_q8;
      }
      break;
    }
    case 1: {
      const int32_t maximum_surface_q8 =
          (ax_q8 > bx_q8 ? ax_q8 : bx_q8) +
          radius_q8;
      penetration_q8 =
          maximum_surface_q8 - maximum_x_q8;
      normal_x_q12 = -PILL_RB_PARAMETER_Q12;
      if (abs_int32(ax_q8 - bx_q8) <= PILL_RB_FLAT_CONTACT_TOLERANCE_Q8) {
        support_x_q8 = (ax_q8 + bx_q8) / 2;
        support_y_q8 = (ay_q8 + by_q8) / 2;
      } else if (ax_q8 > bx_q8) {
        support_x_q8 = ax_q8;
        support_y_q8 = ay_q8;
      } else {
        support_x_q8 = bx_q8;
        support_y_q8 = by_q8;
      }
      break;
    }
    case 2: {
      const int32_t minimum_surface_q8 =
          (ay_q8 < by_q8 ? ay_q8 : by_q8) -
          radius_q8;
      penetration_q8 =
          minimum_y_q8 - minimum_surface_q8;
      normal_y_q12 = PILL_RB_PARAMETER_Q12;
      if (abs_int32(ay_q8 - by_q8) <= PILL_RB_FLAT_CONTACT_TOLERANCE_Q8) {
        support_x_q8 = (ax_q8 + bx_q8) / 2;
        support_y_q8 = (ay_q8 + by_q8) / 2;
      } else if (ay_q8 < by_q8) {
        support_x_q8 = ax_q8;
        support_y_q8 = ay_q8;
      } else {
        support_x_q8 = bx_q8;
        support_y_q8 = by_q8;
      }
      break;
    }
    case 3:
    default: {
      const int32_t maximum_surface_q8 =
          (ay_q8 > by_q8 ? ay_q8 : by_q8) +
          radius_q8;
      penetration_q8 =
          maximum_surface_q8 - maximum_y_q8;
      normal_y_q12 = -PILL_RB_PARAMETER_Q12;
      if (abs_int32(ay_q8 - by_q8) <= PILL_RB_FLAT_CONTACT_TOLERANCE_Q8) {
        support_x_q8 = (ax_q8 + bx_q8) / 2;
        support_y_q8 = (ay_q8 + by_q8) / 2;
      } else if (ay_q8 > by_q8) {
        support_x_q8 = ax_q8;
        support_y_q8 = ay_q8;
      } else {
        support_x_q8 = bx_q8;
        support_y_q8 = by_q8;
      }
      break;
    }
  }

  if (penetration_q8 <= 0) {
    return false;
  }

  body->x_q8 += (int32_t)(
    ((int64_t)normal_x_q12 * penetration_q8) /
    PILL_RB_PARAMETER_Q12
  );
  body->y_q8 += (int32_t)(
    ((int64_t)normal_y_q12 * penetration_q8) /
    PILL_RB_PARAMETER_Q12
  );

  const int32_t contact_x_q8 =
      support_x_q8 - (int32_t)(
        ((int64_t)normal_x_q12 * radius_q8) /
        PILL_RB_PARAMETER_Q12
      );
  const int32_t contact_y_q8 =
      support_y_q8 - (int32_t)(
        ((int64_t)normal_y_q12 * radius_q8) /
        PILL_RB_PARAMETER_Q12
      );

  pill_rb_solve_contact_impulse(
    body,
    NULL,
    (int16_t)(
      (contact_x_q8 - body->x_q8) /
      PILL_PHYSICS_Q8
    ),
    (int16_t)(
      (contact_y_q8 - body->y_q8) /
      PILL_PHYSICS_Q8
    ),
    0,
    0,
    normal_x_q12,
    normal_y_q12
  );

  return true;
}

/*
 * Fixed collision obstacle around the 13 x 14 emblem at (100,100).
 * Only the pill receives position correction and impulse.
 */
static bool pill_rb_solve_swiss_emblem(
    PillPhysicsBody *body,
    int16_t arena_y
) {
  if (
    !body ||
    !s_show_swiss_emblem
  ) {
    return false;
  }

  const int32_t emblem_x_q8 =
      SWISS_EMBLEM_PIVOT_X * PILL_PHYSICS_Q8;
  const int32_t emblem_y_q8 =
      (SWISS_EMBLEM_PIVOT_Y - arena_y) *
      PILL_PHYSICS_Q8;

  int32_t axis_start_x_q8;
  int32_t axis_start_y_q8;
  int32_t axis_end_x_q8;
  int32_t axis_end_y_q8;
  pill_rb_segment_endpoints(
    body,
    &axis_start_x_q8,
    &axis_start_y_q8,
    &axis_end_x_q8,
    &axis_end_y_q8
  );

  int32_t closest_x_q8;
  int32_t closest_y_q8;
  pill_rb_closest_point_on_segment(
    emblem_x_q8,
    emblem_y_q8,
    axis_start_x_q8,
    axis_start_y_q8,
    axis_end_x_q8,
    axis_end_y_q8,
    &closest_x_q8,
    &closest_y_q8
  );

  int32_t delta_x_q8 =
      closest_x_q8 - emblem_x_q8;
  int32_t delta_y_q8 =
      closest_y_q8 - emblem_y_q8;
  uint64_t distance_squared =
      (uint64_t)((int64_t)delta_x_q8 * delta_x_q8) +
      (uint64_t)((int64_t)delta_y_q8 * delta_y_q8);
  int32_t distance_q8 = (int32_t)
      pill_rb_integer_sqrt64(distance_squared);

  const int32_t minimum_distance_q8 =
      (body->collision_radius +
       SWISS_EMBLEM_COLLISION_RADIUS) *
      PILL_PHYSICS_Q8;

  if (distance_q8 >= minimum_distance_q8) {
    return false;
  }

  if (distance_q8 <= 0) {
    delta_x_q8 = body->x_q8 - emblem_x_q8;
    delta_y_q8 = body->y_q8 - emblem_y_q8;
    distance_squared =
        (uint64_t)((int64_t)delta_x_q8 * delta_x_q8) +
        (uint64_t)((int64_t)delta_y_q8 * delta_y_q8);
    distance_q8 = (int32_t)
        pill_rb_integer_sqrt64(distance_squared);

    if (distance_q8 <= 0) {
      delta_x_q8 = PILL_PHYSICS_Q8;
      delta_y_q8 = 0;
      distance_q8 = PILL_PHYSICS_Q8;
    }
  }

  const int32_t normal_x_q12 = (int32_t)(
    ((int64_t)delta_x_q8 * PILL_RB_PARAMETER_Q12) /
    distance_q8
  );
  const int32_t normal_y_q12 = (int32_t)(
    ((int64_t)delta_y_q8 * PILL_RB_PARAMETER_Q12) /
    distance_q8
  );
  const int32_t penetration_q8 =
      minimum_distance_q8 - distance_q8;

  body->x_q8 += (int32_t)(
    ((int64_t)normal_x_q12 * penetration_q8) /
    PILL_RB_PARAMETER_Q12
  );
  body->y_q8 += (int32_t)(
    ((int64_t)normal_y_q12 * penetration_q8) /
    PILL_RB_PARAMETER_Q12
  );

  const int32_t pill_radius_q8 =
      body->collision_radius * PILL_PHYSICS_Q8;
  const int32_t contact_x_q8 =
      closest_x_q8 - (int32_t)(
        ((int64_t)normal_x_q12 * pill_radius_q8) /
        PILL_RB_PARAMETER_Q12
      );
  const int32_t contact_y_q8 =
      closest_y_q8 - (int32_t)(
        ((int64_t)normal_y_q12 * pill_radius_q8) /
        PILL_RB_PARAMETER_Q12
      );

  pill_rb_solve_contact_impulse(
    body,
    NULL,
    (int16_t)(
      (contact_x_q8 - body->x_q8) /
      PILL_PHYSICS_Q8
    ),
    (int16_t)(
      (contact_y_q8 - body->y_q8) /
      PILL_PHYSICS_Q8
    ),
    0,
    0,
    normal_x_q12,
    normal_y_q12
  );

  return true;
}

static int16_t pill_rb_tilt_magnitude(
    int16_t x,
    int16_t y
) {
  const int64_t x_squared =
      (int64_t)x * x;
  const int64_t y_squared =
      (int64_t)y * y;

  return (int16_t)pill_rb_integer_sqrt64(
    (uint64_t)(x_squared + y_squared)
  );
}

static void pill_rb_drive_from_tilt(
    const PillPhysicsBody *body,
    int16_t *drive_x,
    int16_t *drive_y
) {
  if (!drive_x || !drive_y) {
    return;
  }

  const int16_t magnitude =
      pill_rb_tilt_magnitude(
        s_pill_physics_gravity_x,
        s_pill_physics_gravity_y
      );

  const int32_t friction_q8 =
      body && body->surface_friction_q8 > 0
          ? body->surface_friction_q8
          : PILL_PHYSICS_Q8;
  const int16_t deadzone_mg = (int16_t)(
    ((int32_t)PILL_RB_TILT_DEADZONE_MG *
     friction_q8 + PILL_PHYSICS_Q8 / 2) /
    PILL_PHYSICS_Q8
  );

  if (magnitude <= deadzone_mg) {
    *drive_x = 0;
    *drive_y = 0;
    return;
  }

  /*
   * Each pill has a small, stable static-friction variation. Gravity itself
   * remains the same; only the force needed to break loose differs.
   */
  const int16_t limited_magnitude =
      magnitude > PILL_RB_MAX_TILT_MG
          ? PILL_RB_MAX_TILT_MG
          : magnitude;
  const int16_t active_magnitude =
      limited_magnitude - deadzone_mg;

  *drive_x = (int16_t)(
    ((int32_t)s_pill_physics_gravity_x *
     active_magnitude) /
    magnitude
  );
  *drive_y = (int16_t)(
    ((int32_t)s_pill_physics_gravity_y *
     active_magnitude) /
    magnitude
  );
}

static void pill_physics_schedule_tick(uint32_t delay_ms) {
  if (
    s_pill_physics_timer ||
    !s_pill_physics_window_visible ||
    s_confirmed_screen_active ||
    s_transfer_screen_active ||
    s_pill_physics_body_count == 0
  ) {
    return;
  }

  s_pill_physics_timer = app_timer_register(
    delay_ms,
    pill_physics_tick,
    NULL
  );
}

static void pill_physics_tick(void *context) {
  (void)context;
  s_pill_physics_timer = NULL;

  if (
    !s_pill_physics_window_visible ||
    s_confirmed_screen_active ||
    s_transfer_screen_active ||
    s_pill_physics_body_count == 0
  ) {
    pill_physics_update_activity();
    return;
  }

  if (s_scroll.mode != SCROLL_IDLE) {
    s_pill_physics_quiet_frames = 0;
    pill_physics_schedule_tick(
      PILL_PHYSICS_SCROLL_PAUSE_MS
    );
    return;
  }

  int16_t arena_width = 228;
  int16_t arena_height = 228;
  int16_t arena_y = 0;

  if (s_canvas_layer) {
    const GRect bounds = layer_get_bounds(s_canvas_layer);
    arena_width = bounds.size.w;
    arena_height = bounds.size.h;
    arena_y = (int16_t)pill_arena_origin_y();
  }

  const int32_t minimum_x_q8 =
      PILL_PHYSICS_EDGE_MARGIN * PILL_PHYSICS_Q8;
  const int32_t maximum_x_q8 =
      (arena_width - PILL_PHYSICS_EDGE_MARGIN) *
      PILL_PHYSICS_Q8;
  const int32_t minimum_y_q8 =
      (PILL_PHYSICS_EDGE_MARGIN - arena_y) *
      PILL_PHYSICS_Q8;
  const int32_t maximum_y_q8 =
      (arena_height - PILL_PHYSICS_EDGE_MARGIN - arena_y) *
      PILL_PHYSICS_Q8;

  int32_t old_x_q8[PILL_PHYSICS_MAX_BODIES];
  int32_t old_y_q8[PILL_PHYSICS_MAX_BODIES];
  int32_t old_angle[PILL_PHYSICS_MAX_BODIES];

  for (
    uint8_t index = 0;
    index < s_pill_physics_body_count;
    index++
  ) {
    PillPhysicsBody *body =
        &s_pill_physics_bodies[index];
    old_x_q8[index] = body->x_q8;
    old_y_q8[index] = body->y_q8;
    old_angle[index] = body->angle;

    int16_t drive_x;
    int16_t drive_y;
    pill_rb_drive_from_tilt(
      body,
      &drive_x,
      &drive_y
    );

    /*
     * New pills enter from the top in screen coordinates regardless of how
     * the watch is held. Horizontal wrist movement is still allowed.
     *
     * Vertically, the entry phase now accelerates like an almost maximally
     * tilted watch. This keeps the intro deterministic while making the pills
     * fall into view at the same lively speed as normal near-vertical physics.
     */
    if (!body->entered_arena) {
      drive_y =
          PILL_RB_MAX_TILT_MG -
          PILL_RB_TILT_DEADZONE_MG;

      if (body->vy_q8 < PILL_RB_ENTRY_MIN_SPEED_Q8) {
        body->vy_q8 = PILL_RB_ENTRY_MIN_SPEED_Q8;
      }
    }

    body->vx_q8 +=
        drive_x / PILL_RB_ACCEL_DIVISOR;
    body->vy_q8 +=
        drive_y / PILL_RB_ACCEL_DIVISOR;

    body->vx_q8 = pill_rb_clamp_int32(
      body->vx_q8,
      -PILL_RB_MAX_LINEAR_Q8,
      PILL_RB_MAX_LINEAR_Q8
    );
    body->vy_q8 = pill_rb_clamp_int32(
      body->vy_q8,
      -PILL_RB_MAX_LINEAR_Q8,
      PILL_RB_MAX_LINEAR_Q8
    );

    body->angular_velocity = (int32_t)(
      ((int64_t)body->angular_velocity *
       PILL_RB_ANGULAR_DAMPING_NUM) /
      PILL_RB_ANGULAR_DAMPING_DEN
    );

    body->x_q8 += body->vx_q8;
    body->y_q8 += body->vy_q8;
    body->angle = pill_rb_clamp_angle(
      body->angle + body->angular_velocity
    );

    if (!body->entered_arena) {
      const int32_t fully_inside_y_q8 =
          (
            body->collision_radius +
            body->collision_half_length +
            PILL_PHYSICS_EDGE_MARGIN -
            arena_y
          ) *
          PILL_PHYSICS_Q8;

      if (body->y_q8 >= fully_inside_y_q8) {
        body->entered_arena = true;
      }
    }
  }

  bool had_contact = false;

  for (
    uint8_t iteration = 0;
    iteration < PILL_RB_SOLVER_ITERATIONS;
    iteration++
  ) {
    for (
      uint8_t index = 0;
      index < s_pill_physics_body_count;
      index++
    ) {
      PillPhysicsBody *body =
          &s_pill_physics_bodies[index];
      had_contact |= pill_rb_solve_wall(
        body,
        0,
        minimum_x_q8,
        maximum_x_q8,
        minimum_y_q8,
        maximum_y_q8
      );
      had_contact |= pill_rb_solve_wall(
        body,
        1,
        minimum_x_q8,
        maximum_x_q8,
        minimum_y_q8,
        maximum_y_q8
      );
      /*
       * Do not let the top wall teleport a freshly spawned off-screen pill
       * into the arena. After it entered once, the top wall is solid again.
       */
      if (body->entered_arena) {
        had_contact |= pill_rb_solve_wall(
          body,
          2,
          minimum_x_q8,
          maximum_x_q8,
          minimum_y_q8,
          maximum_y_q8
        );
      }
      had_contact |= pill_rb_solve_wall(
        body,
        3,
        minimum_x_q8,
        maximum_x_q8,
        minimum_y_q8,
        maximum_y_q8
      );
      had_contact |= pill_rb_solve_swiss_emblem(
        body,
        arena_y
      );
    }

    for (
      uint8_t first_index = 0;
      first_index < s_pill_physics_body_count;
      first_index++
    ) {
      for (
        uint8_t second_index = first_index + 1;
        second_index < s_pill_physics_body_count;
        second_index++
      ) {
        had_contact |= pill_rb_solve_pair(
          &s_pill_physics_bodies[first_index],
          &s_pill_physics_bodies[second_index]
        );
      }
    }
  }

  bool visual_changed = false;
  int32_t maximum_rest_travel_q8 = 0;
  int32_t maximum_rest_angle = 0;
  const bool resting_contact_candidate =
      had_contact &&
      s_pill_physics_sensor_quiet_samples >= 3;

  for (
    uint8_t index = 0;
    index < s_pill_physics_body_count;
    index++
  ) {
    PillPhysicsBody *body =
        &s_pill_physics_bodies[index];
    const int32_t travel_x_q8 =
        body->x_q8 - old_x_q8[index];
    const int32_t travel_y_q8 =
        body->y_q8 - old_y_q8[index];
    const int32_t travel_q8 =
        abs_int32(travel_x_q8) +
        abs_int32(travel_y_q8);
    int32_t angle_delta =
        body->angle - old_angle[index];

    if (angle_delta > TRIG_MAX_ANGLE / 2) {
      angle_delta -= TRIG_MAX_ANGLE;
    } else if (angle_delta < -TRIG_MAX_ANGLE / 2) {
      angle_delta += TRIG_MAX_ANGLE;
    }

    const int32_t angle_travel =
        abs_int32(angle_delta);

    if (travel_q8 > maximum_rest_travel_q8) {
      maximum_rest_travel_q8 = travel_q8;
    }
    if (angle_travel > maximum_rest_angle) {
      maximum_rest_angle = angle_travel;
    }

    /*
     * A supported body may still carry a sizeable attempted velocity because
     * gravity is applied again every frame. If the solver kept the body
     * geometrically still, discard that hidden velocity instead of letting it
     * re-enter the next contact solve and shake a stack.
     */
    if (
      resting_contact_candidate &&
      travel_q8 <= PILL_RB_REST_TRAVEL_Q8 &&
      angle_travel <= PILL_RB_REST_ANGLE
    ) {
      body->vx_q8 = 0;
      body->vy_q8 = 0;
      body->angular_velocity = 0;
    } else if (resting_contact_candidate) {
      if (
        abs_int32(body->vx_q8) <=
            PILL_RB_SLEEP_LINEAR_Q8
      ) {
        body->vx_q8 = 0;
      }
      if (
        abs_int32(body->vy_q8) <=
            PILL_RB_SLEEP_LINEAR_Q8
      ) {
        body->vy_q8 = 0;
      }
      if (
        abs_int32(body->angular_velocity) <=
            PILL_RB_SLEEP_ANGULAR
      ) {
        body->angular_velocity = 0;
      }
    }

    if (
      body->x_q8 / PILL_PHYSICS_Q8 !=
          old_x_q8[index] / PILL_PHYSICS_Q8 ||
      body->y_q8 / PILL_PHYSICS_Q8 !=
          old_y_q8[index] / PILL_PHYSICS_Q8 ||
      body->angle / PILL_PHYSICS_ANGLE_BUCKET !=
          old_angle[index] / PILL_PHYSICS_ANGLE_BUCKET
    ) {
      visual_changed = true;
    }
  }

  if (visual_changed && s_canvas_layer) {
    layer_mark_dirty(s_canvas_layer);
  }

  const bool geometrically_resting =
      resting_contact_candidate &&
      maximum_rest_travel_q8 <=
          PILL_RB_REST_TRAVEL_Q8 &&
      maximum_rest_angle <=
          PILL_RB_REST_ANGLE;

  if (geometrically_resting) {
    if (s_pill_physics_quiet_frames < 255) {
      s_pill_physics_quiet_frames++;
    }
  } else {
    s_pill_physics_quiet_frames = 0;
  }

  if (s_pill_physics_quiet_frames >= PILL_RB_SLEEP_FRAMES) {
    for (
      uint8_t index = 0;
      index < s_pill_physics_body_count;
      index++
    ) {
      s_pill_physics_bodies[index].vx_q8 = 0;
      s_pill_physics_bodies[index].vy_q8 = 0;
      s_pill_physics_bodies[index].angular_velocity = 0;
    }
    return;
  }

  pill_physics_schedule_tick(
    s_alarm_active
        ? PILL_RB_ALARM_FRAME_MS
        : PILL_RB_FRAME_MS
  );
}

static void pill_physics_accel_handler(
    AccelData *data,
    uint32_t num_samples
) {
  if (!data || num_samples == 0) {
    return;
  }

  const AccelData sample = data[num_samples - 1];

  if (sample.did_vibrate) {
    return;
  }

  if (
    s_confirmed_screen_active ||
    s_transfer_screen_active ||
    s_pill_physics_body_count == 0
  ) {
    return;
  }

  const int16_t target_x = sample.x;
  const int16_t target_y = (int16_t)-sample.y;
  const int16_t old_magnitude =
      pill_rb_tilt_magnitude(
        s_pill_physics_gravity_x,
        s_pill_physics_gravity_y
      );
  s_pill_physics_gravity_x = (int16_t)(
    (s_pill_physics_gravity_x + target_x * 5) / 6
  );
  s_pill_physics_gravity_y = (int16_t)(
    (s_pill_physics_gravity_y + target_y * 5) / 6
  );

  const int16_t new_magnitude =
      pill_rb_tilt_magnitude(
        s_pill_physics_gravity_x,
        s_pill_physics_gravity_y
      );
  /*
   * Hysteresis prevents filtered sensor noise around the nominal 50 mg
   * threshold from repeatedly waking and sleeping a settled pile. Individual
   * pills vary slightly around that threshold; the wake event still uses the
   * shared wider 40/60 mg band.
   */
  const bool entered_drive_band =
      old_magnitude <=
          PILL_RB_TILT_DEADZONE_MG +
          PILL_RB_TILT_WAKE_HYSTERESIS_MG &&
      new_magnitude >
          PILL_RB_TILT_DEADZONE_MG +
          PILL_RB_TILT_WAKE_HYSTERESIS_MG;
  const bool left_drive_band =
      old_magnitude >=
          PILL_RB_TILT_DEADZONE_MG -
          PILL_RB_TILT_WAKE_HYSTERESIS_MG &&
      new_magnitude <
          PILL_RB_TILT_DEADZONE_MG -
          PILL_RB_TILT_WAKE_HYSTERESIS_MG;
  const bool deadzone_crossed =
      entered_drive_band ||
      left_drive_band;
  const bool meaningful_change =
      deadzone_crossed ||
      abs_int32(
        (int32_t)s_pill_physics_gravity_x -
        s_pill_physics_last_target_x
      ) >= PILL_RB_SENSOR_WAKE_MG ||
      abs_int32(
        (int32_t)s_pill_physics_gravity_y -
        s_pill_physics_last_target_y
      ) >= PILL_RB_SENSOR_WAKE_MG;

  if (!meaningful_change) {
    if (s_pill_physics_sensor_quiet_samples < 255) {
      s_pill_physics_sensor_quiet_samples++;
    }
    return;
  }

  s_pill_physics_last_target_x =
      s_pill_physics_gravity_x;
  s_pill_physics_last_target_y =
      s_pill_physics_gravity_y;
  s_pill_physics_sensor_quiet_samples = 0;
  s_pill_physics_quiet_frames = 0;

  if (!s_pill_physics_timer) {
    pill_physics_update_activity();
  }
}

void pill_physics_stop(void) {
  cancel_timer(&s_pill_physics_timer);

  if (s_pill_physics_accel_subscribed) {
    accel_data_service_unsubscribe();
    s_pill_physics_accel_subscribed = false;
  }
}

void pill_physics_update_activity(void) {
  const bool physics_should_run =
      s_pill_physics_window_visible &&
      !s_confirmed_screen_active &&
      !s_transfer_screen_active &&
      s_pill_physics_body_count > 0;

  const bool accel_should_run =
      physics_should_run;

  if (!physics_should_run) {
    cancel_timer(&s_pill_physics_timer);
  }

  if (!accel_should_run) {
    if (s_pill_physics_accel_subscribed) {
      accel_data_service_unsubscribe();
      s_pill_physics_accel_subscribed = false;
    }
    return;
  }

  if (!s_pill_physics_accel_subscribed) {
    accel_service_set_sampling_rate(
      ACCEL_SAMPLING_25HZ
    );
    accel_data_service_subscribe(
      1,
      pill_physics_accel_handler
    );
    s_pill_physics_accel_subscribed = true;
  }

  if (
    physics_should_run &&
    !s_pill_physics_timer
  ) {
    s_pill_physics_quiet_frames = 0;
    pill_physics_schedule_tick(
      s_alarm_active
          ? PILL_RB_ALARM_FRAME_MS
          : PILL_RB_FRAME_MS
    );
  }
}


void pill_physics_init(void) {
  s_pill_physics_window_visible = false;
  s_pill_physics_accel_subscribed = false;
  s_pill_physics_timer = NULL;
}

void pill_physics_deinit(void) {
  pill_physics_stop();
}
