#ifndef STATIC_SPRITE_COMPONENT_H
#define STATIC_SPRITE_COMPONENT_H

#include "actor/actor.h"

#include "raylib.h"

typedef struct {
    boolean center;
    Vector2 offset;
} SpriteAnchor;

typedef struct {
    char texture_path[4096];
    char resolved_texture_path[4096];
    Actor *actor;
    Vector2 position;
    SpriteAnchor anchor;
    float scale;
    float rotation;
    Color tint;
    Texture2D texture;
    boolean loaded;
    boolean attempted_load;
    Heap heap;
} StaticSpriteState;

#endif // STATIC_SPRITE_COMPONENT_H
