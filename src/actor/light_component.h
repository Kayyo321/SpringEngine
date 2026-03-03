#ifndef LightComponentH
#define LightComponentH

#include "actor.h"

#include "raylib.h"

typedef struct {
    Actor *actor;
    Vector3 position;
    Color color;
    float intensity;
    float range;
    boolean enabled;
    Heap heap;
} PointLightComponentData;

typedef struct {
    Actor *actor;
    Vector3 position;
    Vector3 direction;
    Color color;
    float intensity;
    float range;
    float angle;
    boolean enabled;
    Heap heap;
} SpotLightComponentData;

typedef struct {
    Actor *actor;
    Vector3 direction;
    Color color;
    float intensity;
    boolean enabled;
    Heap heap;
} DirectionLightComponentData;

PointLightComponentData *actor_find_point_light_component(Actor *actor);
SpotLightComponentData *actor_find_spot_light_component(Actor *actor);
DirectionLightComponentData *actor_find_direction_light_component(Actor *actor);

#endif // LIGHT_COMPONENT_H
