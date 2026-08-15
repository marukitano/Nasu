# Watch-side architecture

## Goal

The watch application is split into ordinary, separately compiled C modules.
`main.c` is only responsible for lifecycle order.

## Modules

- **watch_settings** — AppMessage parsing, transaction validation and persistent
  medication, daypart and appearance data.
- **medication_model** — current daypart, medication groups, confirmation state
  and the rows shown by the UI.
- **medication_alarm** — Pebble wakeups, due-window calculation, repeating
  reminders, speaker streaming and vibration.
- **pill_physics** — accelerometer sampling, rigid-body movement, wall contacts
  and pill-to-pill collision solving.
- **pill_renderer** — medication icons, pill geometry, colors, imprints and the
  medication list drawing helpers.
- **scroll_controller** — button and touch scrolling, snapping, edge bounce and
  the animated selection band.
- **confirmation_ui** — hold-to-confirm animation and settings-transfer screen.
- **medication_ui** — Pebble window/layer creation and high-level screen state.
- **app_util** — small helpers shared by multiple modules.

## Shared types and compatibility state

`app_types.h` contains constants and data structures used by more than one
module. `app_state.c/.h` contains only runtime state that is genuinely shared
across module boundaries. Timers, audio-stream buffers, physics internals and
settings-transfer staging data are owned privately by their respective modules.

`app_state.h` is an internal header; module consumers should prefer the
focused module headers where an API is available. Medication rendering and
physics consume a normalized `MedicationRuntimeView`, while the persisted
legacy-compatible `MedicationSettings` and `MedicationAppearance` formats stay
unchanged at the storage and AppMessage boundary.

The renderer keeps reusable `GPath` geometry for oval and diamond tablets, so
animated physics frames only move/rotate existing paths instead of allocating
and destroying them repeatedly. The general UI animation timer is demand-driven
and runs only while the pen animation or a real medication-name marquee needs
it.

## Lifecycle

The startup order is:

1. `watch_settings_init()`
2. `medication_alarm_init()`
3. `pill_physics_init()`
4. `medication_ui_init()`
5. `app_event_loop()`

Shutdown stops physics and alarms, tears down the UI, and finally unregisters
AppMessage callbacks.

## Phone-side code

`src/js/pebble-js-app.js` owns the companion configuration page, local
settings storage and the transactional AppMessage transfer to the watch.
