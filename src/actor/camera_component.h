#ifndef CameraComponentH
#define CameraComponentH

#include "actor.h"

#include "raylib.h"

typedef struct {
    Camera3D camera;
    float near_clip;
    float far_clip;
    boolean active;
    boolean render_to_texture;
    int render_texture_width;
    int render_texture_height;
    RenderTexture2D render_target;
    boolean render_target_initialized;
    Heap heap;
} CameraComponentData;

CameraComponentData *actor_find_camera_component(Actor *actor);

#endif // CAMERA_COMPONENT_H
