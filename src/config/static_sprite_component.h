#ifndef StaticSpriteComponentH
#define StaticSpriteComponentH

#include "actor/actor.h"

#include "raylib.h"

typedef struct {
    boolean center;
    Vector2 offset;
} SpriteAnchor;

enum {
    AnimatedSpriteMaxSheets = 16,
    AnimatedSpriteMaxStates = 32,
    AnimatedSpriteMaxFrames = 256,
    AnimatedSpriteMaxTransitions = 128,
    AnimatedSpriteMaxBoolParams = 64,
    AnimatedSpriteMaxNumberParams = 64,
    AnimatedSpriteMaxStateName = 64,
    AnimatedSpriteMaxSheetKey = 64,
};

typedef enum {
    AnimatedTransitionAlways,
    AnimatedTransitionMoving,
    AnimatedTransitionNotMoving,
    AnimatedTransitionParamTrue,
    AnimatedTransitionParamFalse,
    AnimatedTransitionParamGreater,
    AnimatedTransitionParamLess,
} AnimatedTransitionCondition;

typedef struct {
    Rectangle source;
    float duration;
} AnimatedSpriteFrame;

typedef struct {
    char target_state_name[AnimatedSpriteMaxStateName];
    int target_state_index;
    AnimatedTransitionCondition condition;
    float speed_threshold;
    char param_name[AnimatedSpriteMaxStateName];
} AnimatedSpriteTransition;

typedef struct {
    char name[AnimatedSpriteMaxStateName];
    boolean value;
} AnimatedSpriteBoolParam;

typedef struct {
    char name[AnimatedSpriteMaxStateName];
    float value;
} AnimatedSpriteNumberParam;

typedef struct {
    char key[AnimatedSpriteMaxSheetKey];
    char texture_path[4096];
    char resolved_texture_path[4096];
    Texture2D texture;
    boolean loaded;
    boolean attempted_load;
} AnimatedSpriteSheet;

typedef struct {
    char name[AnimatedSpriteMaxStateName];
    int sheet_index;
    float fps;
    boolean loop;
    int frame_start;
    int frame_count;
    int transition_start;
    int transition_count;
} AnimatedSpriteStateDef;

typedef struct {
    char anim_path[4096];
    char resolved_anim_path[4096];
    Actor *actor;
    Vector2 position;
    SpriteAnchor anchor;
    float scale;
    float rotation;
    Color tint;
    AnimatedSpriteSheet sheets[AnimatedSpriteMaxSheets];
    int sheet_count;
    AnimatedSpriteStateDef states[AnimatedSpriteMaxStates];
    int state_count;
    AnimatedSpriteFrame frames[AnimatedSpriteMaxFrames];
    int frame_count;
    AnimatedSpriteTransition transitions[AnimatedSpriteMaxTransitions];
    int transition_count;
    AnimatedSpriteBoolParam bool_params[AnimatedSpriteMaxBoolParams];
    int bool_param_count;
    AnimatedSpriteNumberParam number_params[AnimatedSpriteMaxNumberParams];
    int number_param_count;
    boolean flip_x;
    boolean flip_y;
    boolean auto_flip_x_from_actor_movement;
    char flip_x_param[AnimatedSpriteMaxStateName];
    float flip_x_deadzone;
    boolean flip_x_when_param_negative;
    int current_state_index;
    int current_frame_offset;
    float frame_timer;
    ActorVector3 previous_actor_position;
    boolean has_previous_actor_position;
    boolean valid;
    Heap heap;
} AnimatedSpriteState;

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

typedef struct {
    Actor *actor;
    Vector2 position;
    SpriteAnchor anchor;
    Vector2 size;
    float rotation;
    Color color;
    Heap heap;
} StaticColorState;

#endif // STATIC_SPRITE_COMPONENT_H
