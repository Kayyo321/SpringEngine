#ifndef COLLIDER_COMPONENT_H
#define COLLIDER_COMPONENT_H

#include "actor.h"

#include "raylib.h"

typedef struct {
    Vector2 offset;
    Vector2 size;
    boolean enabled;
    boolean is_trigger;
    Heap heap;
} ColliderComponentData;

ColliderComponentData *actor_find_collider_component(Actor *actor);
Rectangle collider_world_bounds(const Actor *actor, const ColliderComponentData *collider);
boolean collider_components_overlap(const Actor *left_actor, const ColliderComponentData *left, const Actor *right_actor, const ColliderComponentData *right);

#endif // COLLIDER_COMPONENT_H

