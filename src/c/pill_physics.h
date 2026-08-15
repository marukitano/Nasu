#pragma once

#include "app_types.h"

/* Accelerometer-driven pill rigid-body physics. */
void pill_physics_set_window_visible(
    bool visible
);
uint8_t pill_physics_body_count(void);
const PillPhysicsBody *pill_physics_body_at(
    uint8_t index
);
void pill_physics_init(void);
void pill_physics_deinit(void);
void pill_physics_rebuild(void);
void pill_physics_stop(void);
void pill_physics_update_activity(void);
