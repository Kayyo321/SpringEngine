#include "collider_component.h"

#include "config/static_sprite_component.h"

#include <string.h>

ColliderComponentData *actor_find_collider_component(Actor *actor) {
    return ActorFindBuiltinComponentDataAs(actor, "Collider", ColliderComponentData);
}

static boolean resolve_animated_sprite_frame_size(const AnimatedSpriteState *state, float *out_width, float *out_height) {
    if (!state || !out_width || !out_height)
        return False;

    if (!state->valid || state->state_count <= 0 || state->frame_count <= 0)
        return False;

    int state_index = state->current_state_index;
    if (state_index < 0 || state_index >= state->state_count)
        state_index = 0;

    const AnimatedSpriteStateDef *state_definition = &state->states[state_index];
    if (state_definition->frame_count <= 0)
        return False;

    int frame_offset = state->current_frame_offset;
    if (frame_offset < 0)
        frame_offset = 0;
    if (frame_offset >= state_definition->frame_count)
        frame_offset = state_definition->frame_count - 1;

    const int frame_index = state_definition->frame_start + frame_offset;
    if (frame_index < 0 || frame_index >= state->frame_count)
        return False;

    const AnimatedSpriteFrame *frame = &state->frames[frame_index];
    *out_width = frame->source.width;
    *out_height = frame->source.height;
    return True;
}

static void resolve_visual_sprite_offset(const Actor *actor, float *out_x, float *out_y) {
    if (out_x)
        *out_x = 0.0f;
    if (out_y)
        *out_y = 0.0f;

    if (!actor || !out_x || !out_y)
        return;

    const float actor_scale_x = actor->transform.scale.x;
    const float actor_scale_y = actor->transform.scale.y;

    for (usize component_index = 0; component_index < actor->component_count; ++component_index) {
        const ActorComponent *component = &actor->components[component_index];
        if (component->descriptor.kind != ComponentBuiltin || !component->descriptor.name || !component->data)
            continue;

        if (strcmp(component->descriptor.name, "AnimatedSprite") == 0) {
            const AnimatedSpriteState *state = (const AnimatedSpriteState *)component->data;
            float origin_x = state->anchor.offset.x * state->scale * actor_scale_x;
            float origin_y = state->anchor.offset.y * state->scale * actor_scale_y;

            if (state->anchor.center) {
                float frame_width = 0.0f;
                float frame_height = 0.0f;
                if (resolve_animated_sprite_frame_size(state, &frame_width, &frame_height)) {
                    origin_x = (frame_width * state->scale * actor_scale_x) * 0.5f;
                    origin_y = (frame_height * state->scale * actor_scale_y) * 0.5f;
                } else {
                    origin_x = 0.0f;
                    origin_y = 0.0f;
                }
            }

            *out_x = state->position.x - origin_x;
            *out_y = state->position.y - origin_y;
            return;
        }

        if (strcmp(component->descriptor.name, "StaticSprite") == 0) {
            const StaticSpriteState *state = (const StaticSpriteState *)component->data;
            float origin_x = state->anchor.offset.x * state->scale * actor_scale_x;
            float origin_y = state->anchor.offset.y * state->scale * actor_scale_y;

            if (state->anchor.center) {
                if (state->loaded && state->texture.id != 0) {
                    origin_x = ((float)state->texture.width * state->scale * actor_scale_x) * 0.5f;
                    origin_y = ((float)state->texture.height * state->scale * actor_scale_y) * 0.5f;
                } else {
                    origin_x = 0.0f;
                    origin_y = 0.0f;
                }
            }

            *out_x = state->position.x - origin_x;
            *out_y = state->position.y - origin_y;
            return;
        }

        if (strcmp(component->descriptor.name, "StaticColor") == 0) {
            const StaticColorState *state = (const StaticColorState *)component->data;
            float origin_x = state->anchor.offset.x * actor_scale_x;
            float origin_y = state->anchor.offset.y * actor_scale_y;

            if (state->anchor.center) {
                origin_x = (state->size.x * actor_scale_x) * 0.5f;
                origin_y = (state->size.y * actor_scale_y) * 0.5f;
            }

            *out_x = state->position.x - origin_x;
            *out_y = state->position.y - origin_y;
            return;
        }
    }
}

Rectangle collider_world_bounds(const Actor *actor, const ColliderComponentData *collider) {
    Rectangle bounds = {0.0f, 0.0f, 0.0f, 0.0f};
    if (!actor || !collider)
        return bounds;

    const float actor_scale_x = actor->transform.scale.x;
    const float actor_scale_y = actor->transform.scale.y;
    float visual_offset_x = 0.0f;
    float visual_offset_y = 0.0f;
    resolve_visual_sprite_offset(actor, &visual_offset_x, &visual_offset_y);

    bounds.x = actor->transform.position.x + visual_offset_x + (collider->offset.x * actor_scale_x);
    bounds.y = actor->transform.position.y + visual_offset_y + (collider->offset.y * actor_scale_y);
    bounds.width = collider->size.x * actor_scale_x;
    bounds.height = collider->size.y * actor_scale_y;
    return bounds;
}

boolean collider_components_overlap(const Actor *left_actor, const ColliderComponentData *left, const Actor *right_actor, const ColliderComponentData *right) {
    if (!left_actor || !left || !right_actor || !right)
        return False;

    if (!left->enabled || !right->enabled)
        return False;

    const Rectangle left_bounds = collider_world_bounds(left_actor, left);
    const Rectangle right_bounds = collider_world_bounds(right_actor, right);
    return CheckCollisionRecs(left_bounds, right_bounds) ? True : False;
}
