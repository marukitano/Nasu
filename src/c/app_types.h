#pragma once

#include <pebble.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#define FRAME_COUNT 8
#define UI_TICK_MS 110
#define PILL_TICKS_PER_FRAME 2

#define CANVAS_START_OFFSET_Y 0

/*
 * Same 13 x 14 pixel emblem geometry as FCK_Gravity, but centered on the
 * Emery display. y=114 is exactly the height of the middle-button marker.
 */
#define SWISS_EMBLEM_PIVOT_X 100
#define SWISS_EMBLEM_PIVOT_Y 114
#define SWISS_EMBLEM_WIDTH 13
#define SWISS_EMBLEM_HEIGHT 14
#define SWISS_EMBLEM_COLLISION_RADIUS 7

#define SCROLL_Q8 256
#define SCROLL_FRAME_MS 16

#define SCROLL_BREAKAWAY_PX 9
#define SCROLL_QUICK_SWIPE_MIN_PX 5
#define SCROLL_QUICK_SWIPE_MAX_MS 230

#define SCROLL_MAGNET_ACCEL_PER_PIXEL_Q8 12
#define SCROLL_EDGE_HALF_INTERVAL_PX \
  ((MEDICATION_ROW_HEIGHT + MEDICATION_ROW_GAP) / 2)

#define SCROLL_FINGER_SPRING_NUM 18
#define SCROLL_FINGER_SPRING_DEN 100
#define SCROLL_FINGER_DAMPING_NUM 68
#define SCROLL_FINGER_DAMPING_DEN 100

/*
 * Diese eine Feder definiert sämtliche automatischen
 * Fahrten und Bounces: Taste, Quick-Swipe, normaler
 * Swipe und die virtuellen Tabellenränder.
 */
#define SCROLL_SNAP_SPRING_NUM 24
#define SCROLL_SNAP_SPRING_DEN 100
#define SCROLL_SNAP_DAMPING_NUM 62
#define SCROLL_SNAP_DAMPING_DEN 100

#define SCROLL_SNAP_REFERENCE_PX \
  (MEDICATION_ROW_HEIGHT + MEDICATION_ROW_GAP)

#define SCROLL_BREAKAWAY_FORCE_Q8 \
  ( \
    SCROLL_BREAKAWAY_PX * \
    SCROLL_Q8 * \
    SCROLL_FINGER_SPRING_NUM / \
    SCROLL_FINGER_SPRING_DEN \
  )

#define SCROLL_SNAP_MAX_FORCE_Q8 \
  ( \
    SCROLL_SNAP_REFERENCE_PX * \
    SCROLL_Q8 * \
    SCROLL_SNAP_SPRING_NUM / \
    SCROLL_SNAP_SPRING_DEN \
  )

#define SCROLL_MAX_VELOCITY_Q8 \
  (32 * SCROLL_Q8)

#define SCROLL_STOP_POSITION_Q8 \
  (SCROLL_Q8 / 4)

#define SCROLL_STOP_VELOCITY_Q8 \
  (SCROLL_Q8 / 4)

#define CONFIRM_ANIMATION_INTERVAL_MS 30
#define CONFIRM_GROW_STEP 5
#define CONFIRM_SHRINK_MIN_STEP 4
#define CONFIRM_SHRINK_DIVISOR 7
#define CONFIRM_CENTER_OUTSIDE_X 8

#define CHECK_STROKE_RADIUS 8
#define CHECK_POP_GROW_STEP 44
#define CHECK_POP_SHRINK_STEP 30
#define CHECK_POP_SETTLE_SIZE 80
#define CHECK_POP_OVERSHOOT_SIZE 140

#define MEDICATION_HEADER_HEIGHT 28
#define MEDICATION_ROW_HEIGHT 72
#define MEDICATION_ROW_GAP 8

#define MEDICATION_NAME_LINE_Y 0
#define MEDICATION_NAME_LINE_HEIGHT 28
#define MEDICATION_EFFECT_LINE_Y 26
#define MEDICATION_DETAIL_LINE_HEIGHT 22
#define MEDICATION_DOSAGE_LINE_Y 48
#define MEDICATION_MARQUEE_START_PAUSE_TICKS 8
#define MEDICATION_MARQUEE_END_PAUSE_TICKS 6
#define MEDICATION_MARQUEE_PIXELS_PER_TICK 2

/*
 * The alert and medication views are two complete stacked pages.
 * Page 0 occupies one full screen. Page 1 starts exactly one screen below it,
 * with its first row centered vertically.
 */
#define MEDICATION_PAGE_FIRST_ROW_TOP(screen_height) \
  ( \
    (screen_height) + \
    ((screen_height) - MEDICATION_ROW_HEIGHT) / 2 \
  )
#define MEDICATION_PAGE_HEADER_TOP(screen_height) \
  ( \
    MEDICATION_PAGE_FIRST_ROW_TOP(screen_height) - \
    MEDICATION_HEADER_HEIGHT \
  )
#define MEDICATION_PAGE_ROW_TOP(screen_height, row_index) \
  ( \
    MEDICATION_PAGE_FIRST_ROW_TOP(screen_height) + \
    (row_index) * \
        (MEDICATION_ROW_HEIGHT + MEDICATION_ROW_GAP) \
  )
#define MEDICATION_PAGE_SNAP_OFFSET(screen_height, row_index) \
  ( \
    -(screen_height) - \
    (row_index) * \
        (MEDICATION_ROW_HEIGHT + MEDICATION_ROW_GAP) \
  )
#define MEDICATION_ICON_SIZE 30
#define MEDICATION_ICON_LEFT 10
#define MEDICATION_ICON_TEXT_X 46
#define MEDICATION_ICON_TEXT_RIGHT 8
#define BAND_OVERSHOOT_COVER_PX 32
#define BAND_ARROW_WIDTH 18
#define TAKEN_HINT_MIN_RADIUS 5

#define THEME_PERSIST_KEY 200
#define LEGACY_MEDICATION_PERSIST_KEY 201
#define MEDICATION_LIST_PERSIST_KEY 202
#define MEDICATION_COUNT_PERSIST_KEY 203
#define MEDICATION_ITEM_PERSIST_KEY_BASE 230
#define SETTINGS_MESSAGE_BUFFER_SIZE 512
#define SETTINGS_ACK_OUTBOX_SIZE 128
#define SETTINGS_ACK_RETRY_MS 1000
#define MEDICATION_NAME_LENGTH 32
#define MEDICATION_DOSAGE_LENGTH 21
#define MEDICATION_EFFECT_LENGTH 32
#define MEDICATION_LABEL_LENGTH 48
#define MAX_MEDICATIONS 8
#define MAX_LIST_ROWS (MAX_MEDICATIONS + 2)

#define DAYPART_PERSIST_KEY 204
#define ALARM_AUDIO_VOLUME_PERSIST_KEY 205
#define ALARM_VIBRATION_PERSIST_KEY 206
#define ALARM_INTERVAL_PERSIST_KEY 207
#define ALARM_WINDOW_STATE_PERSIST_KEY 208
#define LANGUAGE_PERSIST_KEY 209
#define SHOW_SWISS_EMBLEM_PERSIST_KEY 211
#define SHOW_JAPANESE_PATTERN_PERSIST_KEY 212
#define DAYPART_MINUTES_PER_DAY 1440
#define LEGACY_DEFAULT_MORNING_START_MINUTE (5 * 60)
#define LEGACY_DEFAULT_NOON_START_MINUTE (11 * 60)
#define LEGACY_DEFAULT_EVENING_START_MINUTE (16 * 60)
#define LEGACY_DEFAULT_NIGHT_START_MINUTE (21 * 60)
#define DEFAULT_MORNING_START_MINUTE (6 * 60)
#define DEFAULT_NOON_START_MINUTE (12 * 60)
#define DEFAULT_EVENING_START_MINUTE (18 * 60)
#define DEFAULT_NIGHT_START_MINUTE (22 * 60)

#define DEFAULT_ALARM_AUDIO_VOLUME 100
#define DEFAULT_ALARM_VIBRATION_ENABLED true
#define DEFAULT_ALARM_REMINDER_INTERVAL_MINUTES 15
#define ALARM_ACTIVE_SECONDS 60
#define ALARM_VIBE_INTERVAL_MS 2000
#define ALARM_AUDIO_BUFFER_SIZE 1024
#define ALARM_AUDIO_PUMP_INTERVAL_MS 10
#define ALARM_AUDIO_MAX_WRITES_PER_PUMP 8
#define ALARM_WAKEUP_COOKIE 0x50494c4c
#define TRANSFER_CLOSE_DELAY_MS 5000
#define TRANSFER_ANIMATION_INTERVAL_MS 30
#define TRANSFER_MORPH_DURATION_MS 450
#define TRANSFER_SHAFT_DURATION_MS 650
#define TRANSFER_FALL_DURATION_MS 500
#define TRANSFER_PROGRESS_MAX 1000

#define PILL_PHYSICS_MAX_BODIES 12
#define PILL_PHYSICS_SCROLL_PAUSE_MS 80
#define PILL_PHYSICS_Q8 256
#define PILL_PHYSICS_EDGE_MARGIN 4
#define PILL_PHYSICS_ANGLE_BUCKET (TRIG_MAX_ANGLE / 32)
/* Capsule rigid-body physics. Positions and linear velocities use Q8. */
#define PILL_RB_FRAME_MS 40
#define PILL_RB_ALARM_FRAME_MS 40
#define PILL_RB_ACCEL_DIVISOR 1
#define PILL_RB_MAX_LINEAR_Q8 (52 * PILL_PHYSICS_Q8)
#define PILL_RB_MAX_ANGULAR (TRIG_MAX_ANGLE / 12)
#define PILL_RB_ANGULAR_DAMPING_NUM 253
#define PILL_RB_ANGULAR_DAMPING_DEN 256
#define PILL_RB_RESTITUTION_NUM 1
#define PILL_RB_RESTITUTION_DEN 5
#define PILL_RB_RESTITUTION_SPEED_Q8 (2 * PILL_PHYSICS_Q8)
#define PILL_RB_FLAT_CONTACT_TOLERANCE_Q8 (4 * PILL_PHYSICS_Q8)
#define PILL_RB_FRICTION_NUM 3
#define PILL_RB_FRICTION_DEN 5
#define PILL_RB_POSITION_SLOP_Q8 (PILL_PHYSICS_Q8 / 2)
#define PILL_RB_SOLVER_ITERATIONS 6
#define PILL_RB_PARAMETER_Q12 4096
#define PILL_RB_ANGLE_TO_LINEAR_NUM 201
#define PILL_RB_ANGLE_TO_LINEAR_DEN 8192
#define PILL_RB_RAD_TO_ANGLE_NUM 10430
#define PILL_RB_SLEEP_LINEAR_Q8 (PILL_PHYSICS_Q8)
#define PILL_RB_SLEEP_ANGULAR (TRIG_MAX_ANGLE / 240)
#define PILL_RB_SLEEP_FRAMES 3
#define PILL_RB_SENSOR_WAKE_MG 50
#define PILL_RB_TILT_DEADZONE_MG 50
#define PILL_RB_TILT_WAKE_HYSTERESIS_MG 10
#define PILL_RB_REST_TRAVEL_Q8 (2 * PILL_PHYSICS_Q8)
#define PILL_RB_REST_ANGLE (TRIG_MAX_ANGLE / 180)

/*
 * New pills are born above the display and must physically enter through
 * the top. A minimum downward entry speed guarantees the intro even when
 * the wrist is tilted upward at that exact moment.
 */
#define PILL_RB_ENTRY_MIN_SPEED_Q8 (2 * PILL_PHYSICS_Q8)
#define PILL_RB_ENTRY_ROW_GAP_PX 34

typedef enum {
  THEME_MODE_DARK = 0,
  THEME_MODE_LIGHT = 1
} ThemeMode;

typedef enum {
  APP_LANGUAGE_GERMAN = 0,
  APP_LANGUAGE_ENGLISH = 1
} AppLanguage;

typedef enum {
  MEDICATION_TIME_MORNING,
  MEDICATION_TIME_NOON,
  MEDICATION_TIME_EVENING,
  MEDICATION_TIME_NIGHT
} MedicationTime;

typedef enum {
  MEDICATION_SCHEDULE_DAILY,
  MEDICATION_SCHEDULE_WEEKLY,
  MEDICATION_SCHEDULE_MONTHLY
} MedicationSchedule;

typedef enum {
  MEDICATION_SYMBOL_PILL,
  MEDICATION_SYMBOL_PEN
} MedicationSymbol;

#define LEGACY_MEDICATION_SYMBOL_TUBE 2

typedef enum {
  MEDICATION_ROW_ITEM,
  MEDICATION_ROW_CONFIRM_PILLS,
  MEDICATION_ROW_CONFIRM_PEN
} MedicationRowKind;

typedef struct {
  char name[MEDICATION_NAME_LENGTH];
  uint8_t quantity;
  uint8_t time;
  uint8_t schedule;
  uint8_t day;
  uint8_t symbol;
  uint8_t enabled;
} LegacyMedicationSettingsV1;

typedef struct {
  char name[MEDICATION_NAME_LENGTH];
  uint8_t quantity;
  uint8_t time;
  uint8_t schedule;
  uint8_t day;
  uint8_t symbol;
  uint8_t shape;
  uint8_t color;
  uint8_t icon_set;
  uint8_t enabled;
} LegacyMedicationSettingsV2;

typedef struct {
  char name[MEDICATION_NAME_LENGTH];
  char dosage[MEDICATION_DOSAGE_LENGTH];
  char effect[MEDICATION_EFFECT_LENGTH];
  uint8_t quantity;
  uint8_t time;
  uint8_t schedule;
  uint8_t day;
  uint8_t symbol;
  uint8_t shape;
  uint8_t color;
  uint8_t icon_set;
  uint8_t enabled;
} MedicationSettings;

typedef struct {
  uint16_t morning;
  uint16_t noon;
  uint16_t evening;
  uint16_t night;
} DaypartSettings;

typedef struct {
  int32_t window_start;
  int32_t last_reminder;
  uint8_t confirmed_mask;
  uint8_t reserved[3];
} AlarmWindowState;

typedef enum {
  CONFIRM_IDLE,
  CONFIRM_GROWING,
  CONFIRM_SHRINKING,
  CONFIRM_COMPLETE
} ConfirmationState;

typedef enum {
  CHECK_HIDDEN,
  CHECK_POPPING_OUT,
  CHECK_AT_PEAK,
  CHECK_SETTLING,
  CHECK_VISIBLE
} CheckState;

typedef struct {
  int32_t x_q8;
  int32_t velocity_q8;
  int32_t target_x_q8;
  bool target_visible;
  bool animating;
} BandAnimationState;

typedef enum {
  TRANSFER_ANIMATION_IDLE,
  TRANSFER_ANIMATION_MORPHING,
  TRANSFER_ANIMATION_SHAFT_DROP,
  TRANSFER_ANIMATION_READY,
  TRANSFER_ANIMATION_FALLING
} TransferAnimationState;

typedef struct {
  int32_t x_q8;
  int32_t y_q8;
  int32_t vx_q8;
  int32_t vy_q8;
  int32_t angle;
  int32_t angular_velocity;
  uint8_t medication_index;
  uint8_t collision_radius;
  uint8_t collision_half_length;
  bool entered_arena;

  /* Q8 mass and deterministic surface-friction variation. */
  uint16_t mass_q8;
  uint16_t surface_friction_q8;
} PillPhysicsBody;

typedef enum {
  SCROLL_IDLE,
  SCROLL_TOUCH,
  SCROLL_SNAP,
  SCROLL_EDGE_BOUNCE
} ScrollMode;

typedef struct {
  int32_t position_q8;
  int32_t velocity_q8;
  int32_t target_q8;
  int32_t breakaway_anchor_q8;

  int8_t snap_index;

  ScrollMode mode;
  bool breakaway_locked;
} ScrollState;

#if defined(PBL_TOUCH)
typedef struct {
  int16_t last_y;
  int16_t total_delta_y;
  uint32_t start_time_ms;

  int8_t start_index;
  int8_t neighbor_index;
  int8_t pair_direction;

  bool dragging;
  bool pair_selected;
  bool edge_consumed;
} ScrollTouchState;
#endif

#define MEDICATION_APPEARANCE_IMPRINT_LENGTH 6
#define MEDICATION_APPEARANCE_COUNT_PERSIST_KEY 219
#define MEDICATION_APPEARANCE_PERSIST_KEY_BASE 220

typedef struct {
  bool valid;
  uint8_t shape;
  uint8_t primary_color;
  uint8_t secondary_color;
  uint8_t size;
  char imprint[MEDICATION_APPEARANCE_IMPRINT_LENGTH];
} MedicationAppearance;
