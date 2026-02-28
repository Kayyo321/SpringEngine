#ifndef CAMERA_COMPONENT_H
#define CAMERA_COMPONENT_H

#include "actor.h"

#include "raylib.h"

typedef struct {
    Camera3D camera;
    float near_clip;
    float far_clip;
    boolean active;
    Heap heap;
} CameraComponentData;

CameraComponentData *actor_find_camera_component(Actor *actor);

#endif // CAMERA_COMPONENT_H
