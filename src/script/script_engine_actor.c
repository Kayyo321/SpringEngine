#include "script_runtime_internal.h"

#include <math.h>

static const char *resolve_component_name_alias(const char *component_name) {
    if (!component_name || component_name[0] == '\0')
        return Null;

    if (strcmp(component_name, "AnimConf") == 0)
        return "AnimatedSprite";

    if (strcmp(component_name, "Rigidbody2D") == 0)
        return "Rigidbody";

    if (strcmp(component_name, "DirectionalLight") == 0)
        return "DirectionLight";

    return component_name;
}

static ActorComponent *find_builtin_component(Actor *actor, const char *component_name) {
    return actor_find_builtin_component(actor, component_name);
}

static int animated_sprite_find_bool_param_index(const AnimatedSpriteState *state, const char *name) {
    if (!state || !name || name[0] == '\0')
        return -1;

    for (int index = 0; index < state->bool_param_count; ++index) {
        if (strcmp(state->bool_params[index].name, name) == 0)
            return index;
    }

    return -1;
}

static result animated_sprite_set_bool_param(AnimatedSpriteState *state, const char *name, boolean value) {
    if (!state || !name || name[0] == '\0')
        return Err;

    int index = animated_sprite_find_bool_param_index(state, name);
    if (index < 0) {
        if (state->bool_param_count >= AnimatedSpriteMaxBoolParams)
            return Err;

        index = state->bool_param_count++;
        if (snprintf(state->bool_params[index].name, sizeof(state->bool_params[index].name), "%s", name) >= (int)sizeof(state->bool_params[index].name)) {
            state->bool_param_count--;
            return Err;
        }
    }

    state->bool_params[index].value = value ? True : False;
    return Ok;
}

static result animated_sprite_get_bool_param_value(const AnimatedSpriteState *state, const char *name, boolean *out_value) {
    if (!state || !name || name[0] == '\0' || !out_value)
        return Err;

    int index = animated_sprite_find_bool_param_index(state, name);
    if (index < 0)
        return Err;

    *out_value = state->bool_params[index].value ? True : False;
    return Ok;
}

static int animated_sprite_find_number_param_index(const AnimatedSpriteState *state, const char *name) {
    if (!state || !name || name[0] == '\0')
        return -1;

    for (int index = 0; index < state->number_param_count; ++index) {
        if (strcmp(state->number_params[index].name, name) == 0)
            return index;
    }

    return -1;
}

static result animated_sprite_set_number_param(AnimatedSpriteState *state, const char *name, float value) {
    if (!state || !name || name[0] == '\0')
        return Err;

    int index = animated_sprite_find_number_param_index(state, name);
    if (index < 0) {
        if (state->number_param_count >= AnimatedSpriteMaxNumberParams)
            return Err;

        index = state->number_param_count++;
        if (snprintf(state->number_params[index].name, sizeof(state->number_params[index].name), "%s", name) >= (int)sizeof(state->number_params[index].name)) {
            state->number_param_count--;
            return Err;
        }
    }

    state->number_params[index].value = value;
    return Ok;
}

static result animated_sprite_get_number_param_value(const AnimatedSpriteState *state, const char *name, float *out_value) {
    if (!state || !name || name[0] == '\0' || !out_value)
        return Err;

    int index = animated_sprite_find_number_param_index(state, name);
    if (index < 0)
        return Err;

    *out_value = state->number_params[index].value;
    return Ok;
}

static AnimatedSpriteState *find_actor_animated_sprite(Actor *actor) {
    ActorComponent *component = find_builtin_component(actor, "AnimatedSprite");
    if (!component)
        return Null;

    return (AnimatedSpriteState *)component->data;
}

static ColliderComponentData *find_actor_collider(Actor *actor) {
    ActorComponent *component = find_builtin_component(actor, "Collider");
    if (!component)
        return Null;

    return (ColliderComponentData *)component->data;
}

static RigidbodyComponentData *find_actor_rigidbody(Actor *actor) {
    ActorComponent *component = find_builtin_component(actor, "Rigidbody");
    if (!component)
        return Null;

    return (RigidbodyComponentData *)component->data;
}

static StaticColorState *find_actor_static_color(Actor *actor) {
    ActorComponent *component = find_builtin_component(actor, "StaticColor");
    if (!component)
        return Null;

    return (StaticColorState *)component->data;
}

static PointLightComponentData *find_actor_point_light(Actor *actor) {
    ActorComponent *component = find_builtin_component(actor, "PointLight");
    if (!component)
        return Null;

    return (PointLightComponentData *)component->data;
}

static SpotLightComponentData *find_actor_spot_light(Actor *actor) {
    ActorComponent *component = find_builtin_component(actor, "SpotLight");
    if (!component)
        return Null;

    return (SpotLightComponentData *)component->data;
}

static DirectionLightComponentData *find_actor_direction_light(Actor *actor) {
    ActorComponent *component = find_builtin_component(actor, "DirectionLight");
    if (!component)
        return Null;

    return (DirectionLightComponentData *)component->data;
}

static int lua_actor_component_static_color_set_position(lua_State *lua_state) {
    const char *actor_id = lua_tostring(lua_state, lua_upvalueindex(1));
    const int using_colon_call = lua_istable(lua_state, 1) ? 1 : 0;
    const int x_index = using_colon_call ? 2 : 1;
    const int y_index = using_colon_call ? 3 : 2;

    Actor *actor = lua_runtime_find_actor(lua_state, actor_id);
    StaticColorState *state = find_actor_static_color(actor);
    if (!state) {
        lua_pushboolean(lua_state, 0);
        return 1;
    }

    state->position.x = (float)luaL_checknumber(lua_state, x_index);
    state->position.y = (float)luaL_checknumber(lua_state, y_index);
    lua_pushboolean(lua_state, 1);
    return 1;
}

static int lua_actor_component_static_color_get_position(lua_State *lua_state) {
    const char *actor_id = lua_tostring(lua_state, lua_upvalueindex(1));

    Actor *actor = lua_runtime_find_actor(lua_state, actor_id);
    StaticColorState *state = find_actor_static_color(actor);
    if (!state) {
        lua_pushnil(lua_state);
        lua_pushnil(lua_state);
        return 2;
    }

    lua_pushnumber(lua_state, state->position.x);
    lua_pushnumber(lua_state, state->position.y);
    return 2;
}

static int lua_actor_component_static_color_set_size(lua_State *lua_state) {
    const char *actor_id = lua_tostring(lua_state, lua_upvalueindex(1));
    const int using_colon_call = lua_istable(lua_state, 1) ? 1 : 0;
    const int width_index = using_colon_call ? 2 : 1;
    const int height_index = using_colon_call ? 3 : 2;

    Actor *actor = lua_runtime_find_actor(lua_state, actor_id);
    StaticColorState *state = find_actor_static_color(actor);
    if (!state) {
        lua_pushboolean(lua_state, 0);
        return 1;
    }

    const float width = (float)luaL_checknumber(lua_state, width_index);
    const float height = (float)luaL_checknumber(lua_state, height_index);
    if (width <= 0.0f || height <= 0.0f) {
        lua_pushboolean(lua_state, 0);
        return 1;
    }

    state->size.x = width;
    state->size.y = height;
    lua_pushboolean(lua_state, 1);
    return 1;
}

static int lua_actor_component_static_color_get_size(lua_State *lua_state) {
    const char *actor_id = lua_tostring(lua_state, lua_upvalueindex(1));

    Actor *actor = lua_runtime_find_actor(lua_state, actor_id);
    StaticColorState *state = find_actor_static_color(actor);
    if (!state) {
        lua_pushnil(lua_state);
        lua_pushnil(lua_state);
        return 2;
    }

    lua_pushnumber(lua_state, state->size.x);
    lua_pushnumber(lua_state, state->size.y);
    return 2;
}

static int lua_actor_component_static_color_set_anchor(lua_State *lua_state) {
    const char *actor_id = lua_tostring(lua_state, lua_upvalueindex(1));
    const int using_colon_call = lua_istable(lua_state, 1) ? 1 : 0;
    const int x_index = using_colon_call ? 2 : 1;
    const int y_index = using_colon_call ? 3 : 2;

    Actor *actor = lua_runtime_find_actor(lua_state, actor_id);
    StaticColorState *state = find_actor_static_color(actor);
    if (!state) {
        lua_pushboolean(lua_state, 0);
        return 1;
    }

    state->anchor.center = False;
    state->anchor.offset.x = (float)luaL_checknumber(lua_state, x_index);
    state->anchor.offset.y = (float)luaL_checknumber(lua_state, y_index);
    lua_pushboolean(lua_state, 1);
    return 1;
}

static int lua_actor_component_static_color_set_anchor_center(lua_State *lua_state) {
    const char *actor_id = lua_tostring(lua_state, lua_upvalueindex(1));
    const int using_colon_call = lua_istable(lua_state, 1) ? 1 : 0;
    const int value_index = using_colon_call ? 2 : 1;

    Actor *actor = lua_runtime_find_actor(lua_state, actor_id);
    StaticColorState *state = find_actor_static_color(actor);
    if (!state) {
        lua_pushboolean(lua_state, 0);
        return 1;
    }

    state->anchor.center = lua_toboolean(lua_state, value_index) ? True : False;
    lua_pushboolean(lua_state, 1);
    return 1;
}

static int lua_actor_component_static_color_get_anchor(lua_State *lua_state) {
    const char *actor_id = lua_tostring(lua_state, lua_upvalueindex(1));

    Actor *actor = lua_runtime_find_actor(lua_state, actor_id);
    StaticColorState *state = find_actor_static_color(actor);
    if (!state) {
        lua_pushnil(lua_state);
        lua_pushnil(lua_state);
        lua_pushnil(lua_state);
        return 3;
    }

    lua_pushboolean(lua_state, state->anchor.center ? 1 : 0);
    lua_pushnumber(lua_state, state->anchor.offset.x);
    lua_pushnumber(lua_state, state->anchor.offset.y);
    return 3;
}

static int lua_actor_component_static_color_set_rotation(lua_State *lua_state) {
    const char *actor_id = lua_tostring(lua_state, lua_upvalueindex(1));
    const int using_colon_call = lua_istable(lua_state, 1) ? 1 : 0;
    const int rotation_index = using_colon_call ? 2 : 1;

    Actor *actor = lua_runtime_find_actor(lua_state, actor_id);
    StaticColorState *state = find_actor_static_color(actor);
    if (!state) {
        lua_pushboolean(lua_state, 0);
        return 1;
    }

    state->rotation = (float)luaL_checknumber(lua_state, rotation_index);
    lua_pushboolean(lua_state, 1);
    return 1;
}

static int lua_actor_component_static_color_get_rotation(lua_State *lua_state) {
    const char *actor_id = lua_tostring(lua_state, lua_upvalueindex(1));

    Actor *actor = lua_runtime_find_actor(lua_state, actor_id);
    StaticColorState *state = find_actor_static_color(actor);
    if (!state) {
        lua_pushnil(lua_state);
        return 1;
    }

    lua_pushnumber(lua_state, state->rotation);
    return 1;
}

static int lua_actor_component_static_color_set_color(lua_State *lua_state) {
    const char *actor_id = lua_tostring(lua_state, lua_upvalueindex(1));
    const int using_colon_call = lua_istable(lua_state, 1) ? 1 : 0;
    const int r_index = using_colon_call ? 2 : 1;
    const int g_index = using_colon_call ? 3 : 2;
    const int b_index = using_colon_call ? 4 : 3;
    const int a_index = using_colon_call ? 5 : 4;

    Actor *actor = lua_runtime_find_actor(lua_state, actor_id);
    StaticColorState *state = find_actor_static_color(actor);
    if (!state) {
        lua_pushboolean(lua_state, 0);
        return 1;
    }

    const int red = (int)luaL_checkinteger(lua_state, r_index);
    const int green = (int)luaL_checkinteger(lua_state, g_index);
    const int blue = (int)luaL_checkinteger(lua_state, b_index);
    const int alpha = (int)luaL_checkinteger(lua_state, a_index);
    if (red < 0 || red > 255 || green < 0 || green > 255 || blue < 0 || blue > 255 || alpha < 0 || alpha > 255) {
        lua_pushboolean(lua_state, 0);
        return 1;
    }

    state->color = (Color){
        (unsigned char)red,
        (unsigned char)green,
        (unsigned char)blue,
        (unsigned char)alpha,
    };
    lua_pushboolean(lua_state, 1);
    return 1;
}

static int lua_actor_component_static_color_get_color(lua_State *lua_state) {
    const char *actor_id = lua_tostring(lua_state, lua_upvalueindex(1));

    Actor *actor = lua_runtime_find_actor(lua_state, actor_id);
    StaticColorState *state = find_actor_static_color(actor);
    if (!state) {
        lua_pushnil(lua_state);
        lua_pushnil(lua_state);
        lua_pushnil(lua_state);
        lua_pushnil(lua_state);
        return 4;
    }

    lua_pushinteger(lua_state, state->color.r);
    lua_pushinteger(lua_state, state->color.g);
    lua_pushinteger(lua_state, state->color.b);
    lua_pushinteger(lua_state, state->color.a);
    return 4;
}

static int lua_actor_component_collider_set_offset(lua_State *lua_state) {
    const char *actor_id = lua_tostring(lua_state, lua_upvalueindex(1));
    const int using_colon_call = lua_istable(lua_state, 1) ? 1 : 0;
    const int x_index = using_colon_call ? 2 : 1;
    const int y_index = using_colon_call ? 3 : 2;

    Actor *actor = lua_runtime_find_actor(lua_state, actor_id);
    ColliderComponentData *state = find_actor_collider(actor);
    if (!state) {
        lua_pushboolean(lua_state, 0);
        return 1;
    }

    state->offset.x = (float)luaL_checknumber(lua_state, x_index);
    state->offset.y = (float)luaL_checknumber(lua_state, y_index);
    lua_pushboolean(lua_state, 1);
    return 1;
}

static int lua_actor_component_collider_get_offset(lua_State *lua_state) {
    const char *actor_id = lua_tostring(lua_state, lua_upvalueindex(1));

    Actor *actor = lua_runtime_find_actor(lua_state, actor_id);
    ColliderComponentData *state = find_actor_collider(actor);
    if (!state) {
        lua_pushnil(lua_state);
        lua_pushnil(lua_state);
        return 2;
    }

    lua_pushnumber(lua_state, state->offset.x);
    lua_pushnumber(lua_state, state->offset.y);
    return 2;
}

static int lua_actor_component_collider_set_size(lua_State *lua_state) {
    const char *actor_id = lua_tostring(lua_state, lua_upvalueindex(1));
    const int using_colon_call = lua_istable(lua_state, 1) ? 1 : 0;
    const int width_index = using_colon_call ? 2 : 1;
    const int height_index = using_colon_call ? 3 : 2;

    Actor *actor = lua_runtime_find_actor(lua_state, actor_id);
    ColliderComponentData *state = find_actor_collider(actor);
    if (!state) {
        lua_pushboolean(lua_state, 0);
        return 1;
    }

    const float width = (float)luaL_checknumber(lua_state, width_index);
    const float height = (float)luaL_checknumber(lua_state, height_index);
    if (width <= 0.0f || height <= 0.0f) {
        lua_pushboolean(lua_state, 0);
        return 1;
    }

    state->size.x = width;
    state->size.y = height;
    lua_pushboolean(lua_state, 1);
    return 1;
}

static int lua_actor_component_collider_get_size(lua_State *lua_state) {
    const char *actor_id = lua_tostring(lua_state, lua_upvalueindex(1));

    Actor *actor = lua_runtime_find_actor(lua_state, actor_id);
    ColliderComponentData *state = find_actor_collider(actor);
    if (!state) {
        lua_pushnil(lua_state);
        lua_pushnil(lua_state);
        return 2;
    }

    lua_pushnumber(lua_state, state->size.x);
    lua_pushnumber(lua_state, state->size.y);
    return 2;
}

static int lua_actor_component_collider_set_enabled(lua_State *lua_state) {
    const char *actor_id = lua_tostring(lua_state, lua_upvalueindex(1));
    const int using_colon_call = lua_istable(lua_state, 1) ? 1 : 0;
    const int enabled_index = using_colon_call ? 2 : 1;

    Actor *actor = lua_runtime_find_actor(lua_state, actor_id);
    ColliderComponentData *state = find_actor_collider(actor);
    if (!state) {
        lua_pushboolean(lua_state, 0);
        return 1;
    }

    state->enabled = lua_toboolean(lua_state, enabled_index) ? True : False;
    lua_pushboolean(lua_state, 1);
    return 1;
}

static int lua_actor_component_collider_get_enabled(lua_State *lua_state) {
    const char *actor_id = lua_tostring(lua_state, lua_upvalueindex(1));

    Actor *actor = lua_runtime_find_actor(lua_state, actor_id);
    ColliderComponentData *state = find_actor_collider(actor);
    if (!state) {
        lua_pushnil(lua_state);
        return 1;
    }

    lua_pushboolean(lua_state, state->enabled ? 1 : 0);
    return 1;
}

static int lua_actor_component_collider_set_is_trigger(lua_State *lua_state) {
    const char *actor_id = lua_tostring(lua_state, lua_upvalueindex(1));
    const int using_colon_call = lua_istable(lua_state, 1) ? 1 : 0;
    const int value_index = using_colon_call ? 2 : 1;

    Actor *actor = lua_runtime_find_actor(lua_state, actor_id);
    ColliderComponentData *state = find_actor_collider(actor);
    if (!state) {
        lua_pushboolean(lua_state, 0);
        return 1;
    }

    state->is_trigger = lua_toboolean(lua_state, value_index) ? True : False;
    lua_pushboolean(lua_state, 1);
    return 1;
}

static int lua_actor_component_collider_get_is_trigger(lua_State *lua_state) {
    const char *actor_id = lua_tostring(lua_state, lua_upvalueindex(1));

    Actor *actor = lua_runtime_find_actor(lua_state, actor_id);
    ColliderComponentData *state = find_actor_collider(actor);
    if (!state) {
        lua_pushnil(lua_state);
        return 1;
    }

    lua_pushboolean(lua_state, state->is_trigger ? 1 : 0);
    return 1;
}

static int lua_actor_component_collider_get_bounds(lua_State *lua_state) {
    const char *actor_id = lua_tostring(lua_state, lua_upvalueindex(1));

    Actor *actor = lua_runtime_find_actor(lua_state, actor_id);
    ColliderComponentData *state = find_actor_collider(actor);
    if (!state || !actor) {
        lua_pushnil(lua_state);
        lua_pushnil(lua_state);
        lua_pushnil(lua_state);
        lua_pushnil(lua_state);
        return 4;
    }

    const Rectangle bounds = collider_world_bounds(actor, state);
    lua_pushnumber(lua_state, bounds.x);
    lua_pushnumber(lua_state, bounds.y);
    lua_pushnumber(lua_state, bounds.width);
    lua_pushnumber(lua_state, bounds.height);
    return 4;
}

static int lua_actor_component_collider_overlaps_actor(lua_State *lua_state) {
    const char *actor_id = lua_tostring(lua_state, lua_upvalueindex(1));
    const int using_colon_call = lua_istable(lua_state, 1) ? 1 : 0;
    const int target_actor_index = using_colon_call ? 2 : 1;
    const char *target_actor_id = luaL_checkstring(lua_state, target_actor_index);

    Actor *actor = lua_runtime_find_actor(lua_state, actor_id);
    ColliderComponentData *state = find_actor_collider(actor);
    if (!state || !actor) {
        lua_pushboolean(lua_state, 0);
        return 1;
    }

    Actor *target_actor = lua_runtime_find_actor(lua_state, target_actor_id);
    ColliderComponentData *target_state = find_actor_collider(target_actor);
    if (!target_state || !target_actor) {
        lua_pushboolean(lua_state, 0);
        return 1;
    }

    lua_pushboolean(lua_state, collider_components_overlap(actor, state, target_actor, target_state) ? 1 : 0);
    return 1;
}

static int lua_actor_component_collider_overlaps_all(lua_State *lua_state) {
    const char *actor_id = lua_tostring(lua_state, lua_upvalueindex(1));

    Actor *actor = lua_runtime_find_actor(lua_state, actor_id);
    ColliderComponentData *state = find_actor_collider(actor);

    lua_newtable(lua_state);

    if (!state || !actor)
        return 1;

    if (!actor->enabled || !state->enabled)
        return 1;

    ScriptRuntime *runtime = lua_runtime_instance(lua_state);
    if (!runtime || !runtime->actor_registry)
        return 1;

    int output_index = 1;
    for (usize actor_index = 0; actor_index < runtime->actor_registry->actor_count; ++actor_index) {
        Actor *candidate = &runtime->actor_registry->actors[actor_index];
        if (!candidate || candidate == actor || !candidate->enabled)
            continue;

        ColliderComponentData *candidate_state = find_actor_collider(candidate);
        if (!candidate_state || !candidate_state->enabled)
            continue;

        if (!collider_components_overlap(actor, state, candidate, candidate_state))
            continue;

        lua_pushstring(lua_state, candidate->id ? candidate->id : "");
        lua_rawseti(lua_state, -2, output_index++);
    }

    return 1;
}

static int lua_actor_component_collider_overlaps_point(lua_State *lua_state) {
    const char *actor_id = lua_tostring(lua_state, lua_upvalueindex(1));
    const int using_colon_call = lua_istable(lua_state, 1) ? 1 : 0;
    const int x_index = using_colon_call ? 2 : 1;
    const int y_index = using_colon_call ? 3 : 2;

    Actor *actor = lua_runtime_find_actor(lua_state, actor_id);
    ColliderComponentData *state = find_actor_collider(actor);
    if (!state || !actor) {
        lua_pushboolean(lua_state, 0);
        return 1;
    }

    const Rectangle bounds = collider_world_bounds(actor, state);
    const Vector2 point = {
        (float)luaL_checknumber(lua_state, x_index),
        (float)luaL_checknumber(lua_state, y_index),
    };

    lua_pushboolean(lua_state, CheckCollisionPointRec(point, bounds) ? 1 : 0);
    return 1;
}

static result lua_actor_component_rigidbody_get(lua_State *lua_state, const char *actor_id, RigidbodyComponentData **out_rigidbody) {
    if (!out_rigidbody)
        return Err;

    *out_rigidbody = Null;
    Actor *actor = lua_runtime_find_actor(lua_state, actor_id);
    if (!actor)
        return Err;

    RigidbodyComponentData *rigidbody = find_actor_rigidbody(actor);
    if (!rigidbody)
        return Err;

    *out_rigidbody = rigidbody;
    return Ok;
}

static int lua_actor_rigidbody_force_mode(lua_State *lua_state, int index) {
    if (lua_gettop(lua_state) < index || !lua_isstring(lua_state, index))
        return 0;

    const char *mode = lua_tostring(lua_state, index);
    if (!mode)
        return 0;

    return (strcmp(mode, "impulse") == 0 || strcmp(mode, "Impulse") == 0) ? 1 : 0;
}

static int lua_actor_component_rigidbody_set_body_type(lua_State *lua_state) {
    const char *actor_id = lua_tostring(lua_state, lua_upvalueindex(1));
    const int using_colon_call = lua_istable(lua_state, 1) ? 1 : 0;
    const int value_index = using_colon_call ? 2 : 1;
    const char *value = luaL_checkstring(lua_state, value_index);

    RigidbodyComponentData *state = Null;
    if (lua_actor_component_rigidbody_get(lua_state, actor_id, &state) != Ok) {
        lua_pushboolean(lua_state, 0);
        return 1;
    }

    RigidbodyBodyType body_type = RigidbodyBodyTypeDynamic;
    if (rigidbody_body_type_from_string(value, &body_type) != Ok) {
        lua_pushboolean(lua_state, 0);
        return 1;
    }

    state->body_type = body_type;
    lua_pushboolean(lua_state, 1);
    return 1;
}

static int lua_actor_component_rigidbody_get_body_type(lua_State *lua_state) {
    const char *actor_id = lua_tostring(lua_state, lua_upvalueindex(1));

    RigidbodyComponentData *state = Null;
    if (lua_actor_component_rigidbody_get(lua_state, actor_id, &state) != Ok) {
        lua_pushnil(lua_state);
        return 1;
    }

    lua_pushstring(lua_state, rigidbody_body_type_to_string(state->body_type));
    return 1;
}

static int lua_actor_component_rigidbody_set_simulated(lua_State *lua_state) {
    const char *actor_id = lua_tostring(lua_state, lua_upvalueindex(1));
    const int using_colon_call = lua_istable(lua_state, 1) ? 1 : 0;
    const int value_index = using_colon_call ? 2 : 1;

    RigidbodyComponentData *state = Null;
    if (lua_actor_component_rigidbody_get(lua_state, actor_id, &state) != Ok) {
        lua_pushboolean(lua_state, 0);
        return 1;
    }

    state->simulated = lua_toboolean(lua_state, value_index) ? True : False;
    lua_pushboolean(lua_state, 1);
    return 1;
}

static int lua_actor_component_rigidbody_get_simulated(lua_State *lua_state) {
    const char *actor_id = lua_tostring(lua_state, lua_upvalueindex(1));

    RigidbodyComponentData *state = Null;
    if (lua_actor_component_rigidbody_get(lua_state, actor_id, &state) != Ok) {
        lua_pushnil(lua_state);
        return 1;
    }

    lua_pushboolean(lua_state, state->simulated ? 1 : 0);
    return 1;
}

static int lua_actor_component_rigidbody_set_use_gravity(lua_State *lua_state) {
    const char *actor_id = lua_tostring(lua_state, lua_upvalueindex(1));
    const int using_colon_call = lua_istable(lua_state, 1) ? 1 : 0;
    const int value_index = using_colon_call ? 2 : 1;

    RigidbodyComponentData *state = Null;
    if (lua_actor_component_rigidbody_get(lua_state, actor_id, &state) != Ok) {
        lua_pushboolean(lua_state, 0);
        return 1;
    }

    state->use_gravity = lua_toboolean(lua_state, value_index) ? True : False;
    lua_pushboolean(lua_state, 1);
    return 1;
}

static int lua_actor_component_rigidbody_get_use_gravity(lua_State *lua_state) {
    const char *actor_id = lua_tostring(lua_state, lua_upvalueindex(1));

    RigidbodyComponentData *state = Null;
    if (lua_actor_component_rigidbody_get(lua_state, actor_id, &state) != Ok) {
        lua_pushnil(lua_state);
        return 1;
    }

    lua_pushboolean(lua_state, state->use_gravity ? 1 : 0);
    return 1;
}

static int lua_actor_component_rigidbody_set_mass(lua_State *lua_state) {
    const char *actor_id = lua_tostring(lua_state, lua_upvalueindex(1));
    const int using_colon_call = lua_istable(lua_state, 1) ? 1 : 0;
    const int value_index = using_colon_call ? 2 : 1;

    RigidbodyComponentData *state = Null;
    if (lua_actor_component_rigidbody_get(lua_state, actor_id, &state) != Ok) {
        lua_pushboolean(lua_state, 0);
        return 1;
    }

    const float mass = (float)luaL_checknumber(lua_state, value_index);
    if (mass <= 0.0f) {
        lua_pushboolean(lua_state, 0);
        return 1;
    }

    state->mass = mass;
    state->inverse_mass = 1.0f / mass;
    lua_pushboolean(lua_state, 1);
    return 1;
}

static int lua_actor_component_rigidbody_get_mass(lua_State *lua_state) {
    const char *actor_id = lua_tostring(lua_state, lua_upvalueindex(1));

    RigidbodyComponentData *state = Null;
    if (lua_actor_component_rigidbody_get(lua_state, actor_id, &state) != Ok) {
        lua_pushnil(lua_state);
        return 1;
    }

    lua_pushnumber(lua_state, state->mass);
    return 1;
}

static int lua_actor_component_rigidbody_set_gravity_scale(lua_State *lua_state) {
    const char *actor_id = lua_tostring(lua_state, lua_upvalueindex(1));
    const int using_colon_call = lua_istable(lua_state, 1) ? 1 : 0;
    const int value_index = using_colon_call ? 2 : 1;

    RigidbodyComponentData *state = Null;
    if (lua_actor_component_rigidbody_get(lua_state, actor_id, &state) != Ok) {
        lua_pushboolean(lua_state, 0);
        return 1;
    }

    state->gravity_scale = (float)luaL_checknumber(lua_state, value_index);
    lua_pushboolean(lua_state, 1);
    return 1;
}

static int lua_actor_component_rigidbody_get_gravity_scale(lua_State *lua_state) {
    const char *actor_id = lua_tostring(lua_state, lua_upvalueindex(1));

    RigidbodyComponentData *state = Null;
    if (lua_actor_component_rigidbody_get(lua_state, actor_id, &state) != Ok) {
        lua_pushnil(lua_state);
        return 1;
    }

    lua_pushnumber(lua_state, state->gravity_scale);
    return 1;
}

static int lua_actor_component_rigidbody_set_linear_drag(lua_State *lua_state) {
    const char *actor_id = lua_tostring(lua_state, lua_upvalueindex(1));
    const int using_colon_call = lua_istable(lua_state, 1) ? 1 : 0;
    const int value_index = using_colon_call ? 2 : 1;

    RigidbodyComponentData *state = Null;
    if (lua_actor_component_rigidbody_get(lua_state, actor_id, &state) != Ok) {
        lua_pushboolean(lua_state, 0);
        return 1;
    }

    float value = (float)luaL_checknumber(lua_state, value_index);
    if (value < 0.0f)
        value = 0.0f;

    state->linear_drag = value;
    lua_pushboolean(lua_state, 1);
    return 1;
}

static int lua_actor_component_rigidbody_get_linear_drag(lua_State *lua_state) {
    const char *actor_id = lua_tostring(lua_state, lua_upvalueindex(1));
    RigidbodyComponentData *state = Null;
    if (lua_actor_component_rigidbody_get(lua_state, actor_id, &state) != Ok) {
        lua_pushnil(lua_state);
        return 1;
    }

    lua_pushnumber(lua_state, state->linear_drag);
    return 1;
}

static int lua_actor_component_rigidbody_set_angular_drag(lua_State *lua_state) {
    const char *actor_id = lua_tostring(lua_state, lua_upvalueindex(1));
    const int using_colon_call = lua_istable(lua_state, 1) ? 1 : 0;
    const int value_index = using_colon_call ? 2 : 1;

    RigidbodyComponentData *state = Null;
    if (lua_actor_component_rigidbody_get(lua_state, actor_id, &state) != Ok) {
        lua_pushboolean(lua_state, 0);
        return 1;
    }

    float value = (float)luaL_checknumber(lua_state, value_index);
    if (value < 0.0f)
        value = 0.0f;

    state->angular_drag = value;
    lua_pushboolean(lua_state, 1);
    return 1;
}

static int lua_actor_component_rigidbody_get_angular_drag(lua_State *lua_state) {
    const char *actor_id = lua_tostring(lua_state, lua_upvalueindex(1));
    RigidbodyComponentData *state = Null;
    if (lua_actor_component_rigidbody_get(lua_state, actor_id, &state) != Ok) {
        lua_pushnil(lua_state);
        return 1;
    }

    lua_pushnumber(lua_state, state->angular_drag);
    return 1;
}

static int lua_actor_component_rigidbody_set_velocity(lua_State *lua_state) {
    const char *actor_id = lua_tostring(lua_state, lua_upvalueindex(1));
    const int using_colon_call = lua_istable(lua_state, 1) ? 1 : 0;
    const int x_index = using_colon_call ? 2 : 1;
    const int y_index = using_colon_call ? 3 : 2;

    RigidbodyComponentData *state = Null;
    if (lua_actor_component_rigidbody_get(lua_state, actor_id, &state) != Ok) {
        lua_pushboolean(lua_state, 0);
        return 1;
    }

    state->velocity_x = (float)luaL_checknumber(lua_state, x_index);
    state->velocity_y = (float)luaL_checknumber(lua_state, y_index);
    lua_pushboolean(lua_state, 1);
    return 1;
}

static int lua_actor_component_rigidbody_get_velocity(lua_State *lua_state) {
    const char *actor_id = lua_tostring(lua_state, lua_upvalueindex(1));
    RigidbodyComponentData *state = Null;
    if (lua_actor_component_rigidbody_get(lua_state, actor_id, &state) != Ok) {
        lua_pushnil(lua_state);
        lua_pushnil(lua_state);
        return 2;
    }

    lua_pushnumber(lua_state, state->velocity_x);
    lua_pushnumber(lua_state, state->velocity_y);
    return 2;
}

static int lua_actor_component_rigidbody_set_angular_velocity(lua_State *lua_state) {
    const char *actor_id = lua_tostring(lua_state, lua_upvalueindex(1));
    const int using_colon_call = lua_istable(lua_state, 1) ? 1 : 0;
    const int value_index = using_colon_call ? 2 : 1;

    RigidbodyComponentData *state = Null;
    if (lua_actor_component_rigidbody_get(lua_state, actor_id, &state) != Ok) {
        lua_pushboolean(lua_state, 0);
        return 1;
    }

    state->angular_velocity = (float)luaL_checknumber(lua_state, value_index);
    lua_pushboolean(lua_state, 1);
    return 1;
}

static int lua_actor_component_rigidbody_get_angular_velocity(lua_State *lua_state) {
    const char *actor_id = lua_tostring(lua_state, lua_upvalueindex(1));
    RigidbodyComponentData *state = Null;
    if (lua_actor_component_rigidbody_get(lua_state, actor_id, &state) != Ok) {
        lua_pushnil(lua_state);
        return 1;
    }

    lua_pushnumber(lua_state, state->angular_velocity);
    return 1;
}

static int lua_actor_component_rigidbody_set_freeze_position(lua_State *lua_state) {
    const char *actor_id = lua_tostring(lua_state, lua_upvalueindex(1));
    const int using_colon_call = lua_istable(lua_state, 1) ? 1 : 0;
    const int x_index = using_colon_call ? 2 : 1;
    const int y_index = using_colon_call ? 3 : 2;

    RigidbodyComponentData *state = Null;
    if (lua_actor_component_rigidbody_get(lua_state, actor_id, &state) != Ok) {
        lua_pushboolean(lua_state, 0);
        return 1;
    }

    state->freeze_position_x = lua_toboolean(lua_state, x_index) ? True : False;
    state->freeze_position_y = lua_toboolean(lua_state, y_index) ? True : False;
    lua_pushboolean(lua_state, 1);
    return 1;
}

static int lua_actor_component_rigidbody_get_freeze_position(lua_State *lua_state) {
    const char *actor_id = lua_tostring(lua_state, lua_upvalueindex(1));
    RigidbodyComponentData *state = Null;
    if (lua_actor_component_rigidbody_get(lua_state, actor_id, &state) != Ok) {
        lua_pushnil(lua_state);
        lua_pushnil(lua_state);
        return 2;
    }

    lua_pushboolean(lua_state, state->freeze_position_x ? 1 : 0);
    lua_pushboolean(lua_state, state->freeze_position_y ? 1 : 0);
    return 2;
}

static int lua_actor_component_rigidbody_set_freeze_rotation(lua_State *lua_state) {
    const char *actor_id = lua_tostring(lua_state, lua_upvalueindex(1));
    const int using_colon_call = lua_istable(lua_state, 1) ? 1 : 0;
    const int value_index = using_colon_call ? 2 : 1;

    RigidbodyComponentData *state = Null;
    if (lua_actor_component_rigidbody_get(lua_state, actor_id, &state) != Ok) {
        lua_pushboolean(lua_state, 0);
        return 1;
    }

    state->freeze_rotation = lua_toboolean(lua_state, value_index) ? True : False;
    lua_pushboolean(lua_state, 1);
    return 1;
}

static int lua_actor_component_rigidbody_get_freeze_rotation(lua_State *lua_state) {
    const char *actor_id = lua_tostring(lua_state, lua_upvalueindex(1));
    RigidbodyComponentData *state = Null;
    if (lua_actor_component_rigidbody_get(lua_state, actor_id, &state) != Ok) {
        lua_pushnil(lua_state);
        return 1;
    }

    lua_pushboolean(lua_state, state->freeze_rotation ? 1 : 0);
    return 1;
}

static int lua_actor_component_rigidbody_add_force(lua_State *lua_state) {
    const char *actor_id = lua_tostring(lua_state, lua_upvalueindex(1));
    const int using_colon_call = lua_istable(lua_state, 1) ? 1 : 0;
    const int x_index = using_colon_call ? 2 : 1;
    const int y_index = using_colon_call ? 3 : 2;
    const int mode_index = using_colon_call ? 4 : 3;

    RigidbodyComponentData *state = Null;
    if (lua_actor_component_rigidbody_get(lua_state, actor_id, &state) != Ok) {
        lua_pushboolean(lua_state, 0);
        return 1;
    }

    const float force_x = (float)luaL_checknumber(lua_state, x_index);
    const float force_y = (float)luaL_checknumber(lua_state, y_index);
    const int impulse_mode = lua_actor_rigidbody_force_mode(lua_state, mode_index);

    if (impulse_mode) {
        if (state->mass <= 0.0f)
            state->mass = 1.0f;
        state->inverse_mass = 1.0f / state->mass;

        state->velocity_x += force_x * state->inverse_mass;
        state->velocity_y += force_y * state->inverse_mass;
    } else {
        state->force_x += force_x;
        state->force_y += force_y;
    }

    lua_pushboolean(lua_state, 1);
    return 1;
}

static int lua_actor_component_rigidbody_apply_force(lua_State *lua_state) {
    const char *actor_id = lua_tostring(lua_state, lua_upvalueindex(1));
    const int using_colon_call = lua_istable(lua_state, 1) ? 1 : 0;
    const int dir_x_index = using_colon_call ? 2 : 1;
    const int dir_y_index = using_colon_call ? 3 : 2;
    const int magnitude_index = using_colon_call ? 4 : 3;

    RigidbodyComponentData *state = Null;
    if (lua_actor_component_rigidbody_get(lua_state, actor_id, &state) != Ok) {
        lua_pushboolean(lua_state, 0);
        return 1;
    }

    float dir_x = (float)luaL_checknumber(lua_state, dir_x_index);
    float dir_y = (float)luaL_checknumber(lua_state, dir_y_index);
    const float magnitude = (float)luaL_checknumber(lua_state, magnitude_index);

    const float length = sqrtf((dir_x * dir_x) + (dir_y * dir_y));
    if (length <= 0.0001f || magnitude == 0.0f) {
        lua_pushboolean(lua_state, 1);
        return 1;
    }

    dir_x /= length;
    dir_y /= length;

    if (state->mass <= 0.0f)
        state->mass = 1.0f;
    state->inverse_mass = 1.0f / state->mass;

    state->velocity_x += (dir_x * magnitude) * state->inverse_mass;
    state->velocity_y += (dir_y * magnitude) * state->inverse_mass;
    lua_pushboolean(lua_state, 1);
    return 1;
}

static int lua_actor_component_rigidbody_add_torque(lua_State *lua_state) {
    const char *actor_id = lua_tostring(lua_state, lua_upvalueindex(1));
    const int using_colon_call = lua_istable(lua_state, 1) ? 1 : 0;
    const int value_index = using_colon_call ? 2 : 1;
    const int mode_index = using_colon_call ? 3 : 2;

    RigidbodyComponentData *state = Null;
    if (lua_actor_component_rigidbody_get(lua_state, actor_id, &state) != Ok) {
        lua_pushboolean(lua_state, 0);
        return 1;
    }

    const float torque = (float)luaL_checknumber(lua_state, value_index);
    const int impulse_mode = lua_actor_rigidbody_force_mode(lua_state, mode_index);

    if (impulse_mode) {
        if (state->mass <= 0.0f)
            state->mass = 1.0f;
        state->inverse_mass = 1.0f / state->mass;
        state->angular_velocity += torque * state->inverse_mass;
    } else {
        state->torque += torque;
    }

    lua_pushboolean(lua_state, 1);
    return 1;
}

static int lua_actor_component_rigidbody_clear_forces(lua_State *lua_state) {
    const char *actor_id = lua_tostring(lua_state, lua_upvalueindex(1));
    RigidbodyComponentData *state = Null;
    if (lua_actor_component_rigidbody_get(lua_state, actor_id, &state) != Ok) {
        lua_pushboolean(lua_state, 0);
        return 1;
    }

    state->force_x = 0.0f;
    state->force_y = 0.0f;
    state->torque = 0.0f;
    lua_pushboolean(lua_state, 1);
    return 1;
}

static int lua_actor_component_rigidbody_move_position(lua_State *lua_state) {
    const char *actor_id = lua_tostring(lua_state, lua_upvalueindex(1));
    const int using_colon_call = lua_istable(lua_state, 1) ? 1 : 0;
    const int x_index = using_colon_call ? 2 : 1;
    const int y_index = using_colon_call ? 3 : 2;
    const int z_index = using_colon_call ? 4 : 3;

    Actor *actor = lua_runtime_find_actor(lua_state, actor_id);
    RigidbodyComponentData *state = find_actor_rigidbody(actor);
    if (!actor || !state) {
        lua_pushboolean(lua_state, 0);
        return 1;
    }

    actor->transform.position.x = (float)luaL_checknumber(lua_state, x_index);
    actor->transform.position.y = (float)luaL_checknumber(lua_state, y_index);
    if (lua_gettop(lua_state) >= z_index)
        actor->transform.position.z = (float)luaL_checknumber(lua_state, z_index);

    state->velocity_x = 0.0f;
    state->velocity_y = 0.0f;
    lua_pushboolean(lua_state, 1);
    return 1;
}

static int lua_actor_component_rigidbody_move_rotation(lua_State *lua_state) {
    const char *actor_id = lua_tostring(lua_state, lua_upvalueindex(1));
    const int using_colon_call = lua_istable(lua_state, 1) ? 1 : 0;
    const int value_index = using_colon_call ? 2 : 1;

    Actor *actor = lua_runtime_find_actor(lua_state, actor_id);
    RigidbodyComponentData *state = find_actor_rigidbody(actor);
    if (!actor || !state) {
        lua_pushboolean(lua_state, 0);
        return 1;
    }

    actor->transform.rotation_euler.z = (float)luaL_checknumber(lua_state, value_index);
    state->angular_velocity = 0.0f;
    lua_pushboolean(lua_state, 1);
    return 1;
}

static int lua_actor_component_rigidbody_is_grounded(lua_State *lua_state) {
    const char *actor_id = lua_tostring(lua_state, lua_upvalueindex(1));
    RigidbodyComponentData *state = Null;
    if (lua_actor_component_rigidbody_get(lua_state, actor_id, &state) != Ok) {
        lua_pushnil(lua_state);
        return 1;
    }

    lua_pushboolean(lua_state, state->is_grounded ? 1 : 0);
    return 1;
}

static int lua_actor_component_animconf_set(lua_State *lua_state) {
    const char *actor_id = lua_tostring(lua_state, lua_upvalueindex(1));
    const int using_colon_call = lua_istable(lua_state, 1) ? 1 : 0;
    const int name_index = using_colon_call ? 2 : 1;
    const int value_index = using_colon_call ? 3 : 2;

    const char *param_name = luaL_checkstring(lua_state, name_index);
    const boolean value = lua_toboolean(lua_state, value_index) ? True : False;

    Actor *actor = lua_runtime_find_actor(lua_state, actor_id);
    AnimatedSpriteState *state = find_actor_animated_sprite(actor);
    if (!state) {
        lua_pushboolean(lua_state, 0);
        return 1;
    }

    lua_pushboolean(lua_state, animated_sprite_set_bool_param(state, param_name, value) == Ok ? 1 : 0);
    return 1;
}

static int lua_actor_component_animconf_get(lua_State *lua_state) {
    const char *actor_id = lua_tostring(lua_state, lua_upvalueindex(1));
    const int using_colon_call = lua_istable(lua_state, 1) ? 1 : 0;
    const int name_index = using_colon_call ? 2 : 1;

    const char *param_name = luaL_checkstring(lua_state, name_index);

    Actor *actor = lua_runtime_find_actor(lua_state, actor_id);
    AnimatedSpriteState *state = find_actor_animated_sprite(actor);
    if (!state) {
        lua_pushnil(lua_state);
        return 1;
    }

    boolean value = False;
    if (animated_sprite_get_bool_param_value(state, param_name, &value) != Ok) {
        lua_pushnil(lua_state);
        return 1;
    }

    lua_pushboolean(lua_state, value ? 1 : 0);
    return 1;
}

static int lua_actor_component_animconf_set_number(lua_State *lua_state) {
    const char *actor_id = lua_tostring(lua_state, lua_upvalueindex(1));
    const int using_colon_call = lua_istable(lua_state, 1) ? 1 : 0;
    const int name_index = using_colon_call ? 2 : 1;
    const int value_index = using_colon_call ? 3 : 2;

    const char *param_name = luaL_checkstring(lua_state, name_index);
    const float value = (float)luaL_checknumber(lua_state, value_index);

    Actor *actor = lua_runtime_find_actor(lua_state, actor_id);
    AnimatedSpriteState *state = find_actor_animated_sprite(actor);
    if (!state) {
        lua_pushboolean(lua_state, 0);
        return 1;
    }

    lua_pushboolean(lua_state, animated_sprite_set_number_param(state, param_name, value) == Ok ? 1 : 0);
    return 1;
}

static int lua_actor_component_animconf_get_number(lua_State *lua_state) {
    const char *actor_id = lua_tostring(lua_state, lua_upvalueindex(1));
    const int using_colon_call = lua_istable(lua_state, 1) ? 1 : 0;
    const int name_index = using_colon_call ? 2 : 1;

    const char *param_name = luaL_checkstring(lua_state, name_index);

    Actor *actor = lua_runtime_find_actor(lua_state, actor_id);
    AnimatedSpriteState *state = find_actor_animated_sprite(actor);
    if (!state) {
        lua_pushnil(lua_state);
        return 1;
    }

    float value = 0.0f;
    if (animated_sprite_get_number_param_value(state, param_name, &value) != Ok) {
        lua_pushnil(lua_state);
        return 1;
    }

    lua_pushnumber(lua_state, value);
    return 1;
}

static int lua_actor_component_animconf_set_flip_x(lua_State *lua_state) {
    const char *actor_id = lua_tostring(lua_state, lua_upvalueindex(1));
    const int using_colon_call = lua_istable(lua_state, 1) ? 1 : 0;
    const int value_index = using_colon_call ? 2 : 1;

    const boolean value = lua_toboolean(lua_state, value_index) ? True : False;

    Actor *actor = lua_runtime_find_actor(lua_state, actor_id);
    AnimatedSpriteState *state = find_actor_animated_sprite(actor);
    if (!state) {
        lua_pushboolean(lua_state, 0);
        return 1;
    }

    state->flip_x = value;
    lua_pushboolean(lua_state, 1);
    return 1;
}

static int lua_actor_component_animconf_get_flip_x(lua_State *lua_state) {
    const char *actor_id = lua_tostring(lua_state, lua_upvalueindex(1));

    Actor *actor = lua_runtime_find_actor(lua_state, actor_id);
    AnimatedSpriteState *state = find_actor_animated_sprite(actor);
    if (!state) {
        lua_pushnil(lua_state);
        return 1;
    }

    lua_pushboolean(lua_state, state->flip_x ? 1 : 0);
    return 1;
}

static int lua_actor_component_point_light_set_position(lua_State *lua_state) {
    const char *actor_id = lua_tostring(lua_state, lua_upvalueindex(1));
    const int using_colon_call = lua_istable(lua_state, 1) ? 1 : 0;
    const int x_index = using_colon_call ? 2 : 1;
    const int y_index = using_colon_call ? 3 : 2;
    const int z_index = using_colon_call ? 4 : 3;

    Actor *actor = lua_runtime_find_actor(lua_state, actor_id);
    PointLightComponentData *state = find_actor_point_light(actor);
    if (!state) {
        lua_pushboolean(lua_state, 0);
        return 1;
    }

    state->position.x = (float)luaL_checknumber(lua_state, x_index);
    state->position.y = (float)luaL_checknumber(lua_state, y_index);
    state->position.z = (float)luaL_checknumber(lua_state, z_index);
    lua_pushboolean(lua_state, 1);
    return 1;
}

static int lua_actor_component_point_light_get_position(lua_State *lua_state) {
    const char *actor_id = lua_tostring(lua_state, lua_upvalueindex(1));

    Actor *actor = lua_runtime_find_actor(lua_state, actor_id);
    PointLightComponentData *state = find_actor_point_light(actor);
    if (!state) {
        lua_pushnil(lua_state);
        lua_pushnil(lua_state);
        lua_pushnil(lua_state);
        return 3;
    }

    lua_pushnumber(lua_state, state->position.x);
    lua_pushnumber(lua_state, state->position.y);
    lua_pushnumber(lua_state, state->position.z);
    return 3;
}

static int lua_actor_component_point_light_set_color(lua_State *lua_state) {
    const char *actor_id = lua_tostring(lua_state, lua_upvalueindex(1));
    const int using_colon_call = lua_istable(lua_state, 1) ? 1 : 0;
    const int r_index = using_colon_call ? 2 : 1;
    const int g_index = using_colon_call ? 3 : 2;
    const int b_index = using_colon_call ? 4 : 3;
    const int a_index = using_colon_call ? 5 : 4;

    Actor *actor = lua_runtime_find_actor(lua_state, actor_id);
    PointLightComponentData *state = find_actor_point_light(actor);
    if (!state) {
        lua_pushboolean(lua_state, 0);
        return 1;
    }

    const int red = (int)luaL_checkinteger(lua_state, r_index);
    const int green = (int)luaL_checkinteger(lua_state, g_index);
    const int blue = (int)luaL_checkinteger(lua_state, b_index);
    const int alpha = (int)luaL_checkinteger(lua_state, a_index);
    if (red < 0 || red > 255 || green < 0 || green > 255 || blue < 0 || blue > 255 || alpha < 0 || alpha > 255) {
        lua_pushboolean(lua_state, 0);
        return 1;
    }

    state->color = (Color){
        (unsigned char)red,
        (unsigned char)green,
        (unsigned char)blue,
        (unsigned char)alpha,
    };
    lua_pushboolean(lua_state, 1);
    return 1;
}

static int lua_actor_component_point_light_get_color(lua_State *lua_state) {
    const char *actor_id = lua_tostring(lua_state, lua_upvalueindex(1));

    Actor *actor = lua_runtime_find_actor(lua_state, actor_id);
    PointLightComponentData *state = find_actor_point_light(actor);
    if (!state) {
        lua_pushnil(lua_state);
        lua_pushnil(lua_state);
        lua_pushnil(lua_state);
        lua_pushnil(lua_state);
        return 4;
    }

    lua_pushinteger(lua_state, state->color.r);
    lua_pushinteger(lua_state, state->color.g);
    lua_pushinteger(lua_state, state->color.b);
    lua_pushinteger(lua_state, state->color.a);
    return 4;
}

static int lua_actor_component_point_light_set_intensity(lua_State *lua_state) {
    const char *actor_id = lua_tostring(lua_state, lua_upvalueindex(1));
    const int using_colon_call = lua_istable(lua_state, 1) ? 1 : 0;
    const int value_index = using_colon_call ? 2 : 1;

    Actor *actor = lua_runtime_find_actor(lua_state, actor_id);
    PointLightComponentData *state = find_actor_point_light(actor);
    if (!state) {
        lua_pushboolean(lua_state, 0);
        return 1;
    }

    const float intensity = (float)luaL_checknumber(lua_state, value_index);
    if (intensity < 0.0f) {
        lua_pushboolean(lua_state, 0);
        return 1;
    }

    state->intensity = intensity;
    lua_pushboolean(lua_state, 1);
    return 1;
}

static int lua_actor_component_point_light_get_intensity(lua_State *lua_state) {
    const char *actor_id = lua_tostring(lua_state, lua_upvalueindex(1));

    Actor *actor = lua_runtime_find_actor(lua_state, actor_id);
    PointLightComponentData *state = find_actor_point_light(actor);
    if (!state) {
        lua_pushnil(lua_state);
        return 1;
    }

    lua_pushnumber(lua_state, state->intensity);
    return 1;
}

static int lua_actor_component_point_light_set_range(lua_State *lua_state) {
    const char *actor_id = lua_tostring(lua_state, lua_upvalueindex(1));
    const int using_colon_call = lua_istable(lua_state, 1) ? 1 : 0;
    const int value_index = using_colon_call ? 2 : 1;

    Actor *actor = lua_runtime_find_actor(lua_state, actor_id);
    PointLightComponentData *state = find_actor_point_light(actor);
    if (!state) {
        lua_pushboolean(lua_state, 0);
        return 1;
    }

    const float range = (float)luaL_checknumber(lua_state, value_index);
    if (range <= 0.0f) {
        lua_pushboolean(lua_state, 0);
        return 1;
    }

    state->range = range;
    lua_pushboolean(lua_state, 1);
    return 1;
}

static int lua_actor_component_point_light_get_range(lua_State *lua_state) {
    const char *actor_id = lua_tostring(lua_state, lua_upvalueindex(1));

    Actor *actor = lua_runtime_find_actor(lua_state, actor_id);
    PointLightComponentData *state = find_actor_point_light(actor);
    if (!state) {
        lua_pushnil(lua_state);
        return 1;
    }

    lua_pushnumber(lua_state, state->range);
    return 1;
}

static int lua_actor_component_point_light_set_enabled(lua_State *lua_state) {
    const char *actor_id = lua_tostring(lua_state, lua_upvalueindex(1));
    const int using_colon_call = lua_istable(lua_state, 1) ? 1 : 0;
    const int value_index = using_colon_call ? 2 : 1;

    Actor *actor = lua_runtime_find_actor(lua_state, actor_id);
    PointLightComponentData *state = find_actor_point_light(actor);
    if (!state) {
        lua_pushboolean(lua_state, 0);
        return 1;
    }

    state->enabled = lua_toboolean(lua_state, value_index) ? True : False;
    lua_pushboolean(lua_state, 1);
    return 1;
}

static int lua_actor_component_point_light_get_enabled(lua_State *lua_state) {
    const char *actor_id = lua_tostring(lua_state, lua_upvalueindex(1));

    Actor *actor = lua_runtime_find_actor(lua_state, actor_id);
    PointLightComponentData *state = find_actor_point_light(actor);
    if (!state) {
        lua_pushnil(lua_state);
        return 1;
    }

    lua_pushboolean(lua_state, state->enabled ? 1 : 0);
    return 1;
}

static int lua_actor_component_spot_light_set_position(lua_State *lua_state) {
    const char *actor_id = lua_tostring(lua_state, lua_upvalueindex(1));
    const int using_colon_call = lua_istable(lua_state, 1) ? 1 : 0;
    const int x_index = using_colon_call ? 2 : 1;
    const int y_index = using_colon_call ? 3 : 2;
    const int z_index = using_colon_call ? 4 : 3;

    Actor *actor = lua_runtime_find_actor(lua_state, actor_id);
    SpotLightComponentData *state = find_actor_spot_light(actor);
    if (!state) {
        lua_pushboolean(lua_state, 0);
        return 1;
    }

    state->position.x = (float)luaL_checknumber(lua_state, x_index);
    state->position.y = (float)luaL_checknumber(lua_state, y_index);
    state->position.z = (float)luaL_checknumber(lua_state, z_index);
    lua_pushboolean(lua_state, 1);
    return 1;
}

static int lua_actor_component_spot_light_get_position(lua_State *lua_state) {
    const char *actor_id = lua_tostring(lua_state, lua_upvalueindex(1));

    Actor *actor = lua_runtime_find_actor(lua_state, actor_id);
    SpotLightComponentData *state = find_actor_spot_light(actor);
    if (!state) {
        lua_pushnil(lua_state);
        lua_pushnil(lua_state);
        lua_pushnil(lua_state);
        return 3;
    }

    lua_pushnumber(lua_state, state->position.x);
    lua_pushnumber(lua_state, state->position.y);
    lua_pushnumber(lua_state, state->position.z);
    return 3;
}

static int lua_actor_component_spot_light_set_direction(lua_State *lua_state) {
    const char *actor_id = lua_tostring(lua_state, lua_upvalueindex(1));
    const int using_colon_call = lua_istable(lua_state, 1) ? 1 : 0;
    const int x_index = using_colon_call ? 2 : 1;
    const int y_index = using_colon_call ? 3 : 2;
    const int z_index = using_colon_call ? 4 : 3;

    Actor *actor = lua_runtime_find_actor(lua_state, actor_id);
    SpotLightComponentData *state = find_actor_spot_light(actor);
    if (!state) {
        lua_pushboolean(lua_state, 0);
        return 1;
    }

    const float x = (float)luaL_checknumber(lua_state, x_index);
    const float y = (float)luaL_checknumber(lua_state, y_index);
    const float z = (float)luaL_checknumber(lua_state, z_index);
    if (fabsf(x) < 0.0001f && fabsf(y) < 0.0001f && fabsf(z) < 0.0001f) {
        lua_pushboolean(lua_state, 0);
        return 1;
    }

    state->direction = (Vector3){x, y, z};
    lua_pushboolean(lua_state, 1);
    return 1;
}

static int lua_actor_component_spot_light_get_direction(lua_State *lua_state) {
    const char *actor_id = lua_tostring(lua_state, lua_upvalueindex(1));

    Actor *actor = lua_runtime_find_actor(lua_state, actor_id);
    SpotLightComponentData *state = find_actor_spot_light(actor);
    if (!state) {
        lua_pushnil(lua_state);
        lua_pushnil(lua_state);
        lua_pushnil(lua_state);
        return 3;
    }

    lua_pushnumber(lua_state, state->direction.x);
    lua_pushnumber(lua_state, state->direction.y);
    lua_pushnumber(lua_state, state->direction.z);
    return 3;
}

static int lua_actor_component_spot_light_set_color(lua_State *lua_state) {
    const char *actor_id = lua_tostring(lua_state, lua_upvalueindex(1));
    const int using_colon_call = lua_istable(lua_state, 1) ? 1 : 0;
    const int r_index = using_colon_call ? 2 : 1;
    const int g_index = using_colon_call ? 3 : 2;
    const int b_index = using_colon_call ? 4 : 3;
    const int a_index = using_colon_call ? 5 : 4;

    Actor *actor = lua_runtime_find_actor(lua_state, actor_id);
    SpotLightComponentData *state = find_actor_spot_light(actor);
    if (!state) {
        lua_pushboolean(lua_state, 0);
        return 1;
    }

    const int red = (int)luaL_checkinteger(lua_state, r_index);
    const int green = (int)luaL_checkinteger(lua_state, g_index);
    const int blue = (int)luaL_checkinteger(lua_state, b_index);
    const int alpha = (int)luaL_checkinteger(lua_state, a_index);
    if (red < 0 || red > 255 || green < 0 || green > 255 || blue < 0 || blue > 255 || alpha < 0 || alpha > 255) {
        lua_pushboolean(lua_state, 0);
        return 1;
    }

    state->color = (Color){(unsigned char)red, (unsigned char)green, (unsigned char)blue, (unsigned char)alpha};
    lua_pushboolean(lua_state, 1);
    return 1;
}

static int lua_actor_component_spot_light_get_color(lua_State *lua_state) {
    const char *actor_id = lua_tostring(lua_state, lua_upvalueindex(1));

    Actor *actor = lua_runtime_find_actor(lua_state, actor_id);
    SpotLightComponentData *state = find_actor_spot_light(actor);
    if (!state) {
        lua_pushnil(lua_state);
        lua_pushnil(lua_state);
        lua_pushnil(lua_state);
        lua_pushnil(lua_state);
        return 4;
    }

    lua_pushinteger(lua_state, state->color.r);
    lua_pushinteger(lua_state, state->color.g);
    lua_pushinteger(lua_state, state->color.b);
    lua_pushinteger(lua_state, state->color.a);
    return 4;
}

static int lua_actor_component_spot_light_set_intensity(lua_State *lua_state) {
    const char *actor_id = lua_tostring(lua_state, lua_upvalueindex(1));
    const int using_colon_call = lua_istable(lua_state, 1) ? 1 : 0;
    const int value_index = using_colon_call ? 2 : 1;

    Actor *actor = lua_runtime_find_actor(lua_state, actor_id);
    SpotLightComponentData *state = find_actor_spot_light(actor);
    if (!state) {
        lua_pushboolean(lua_state, 0);
        return 1;
    }

    const float value = (float)luaL_checknumber(lua_state, value_index);
    if (value < 0.0f) {
        lua_pushboolean(lua_state, 0);
        return 1;
    }

    state->intensity = value;
    lua_pushboolean(lua_state, 1);
    return 1;
}

static int lua_actor_component_spot_light_get_intensity(lua_State *lua_state) {
    const char *actor_id = lua_tostring(lua_state, lua_upvalueindex(1));

    Actor *actor = lua_runtime_find_actor(lua_state, actor_id);
    SpotLightComponentData *state = find_actor_spot_light(actor);
    if (!state) {
        lua_pushnil(lua_state);
        return 1;
    }

    lua_pushnumber(lua_state, state->intensity);
    return 1;
}

static int lua_actor_component_spot_light_set_range(lua_State *lua_state) {
    const char *actor_id = lua_tostring(lua_state, lua_upvalueindex(1));
    const int using_colon_call = lua_istable(lua_state, 1) ? 1 : 0;
    const int value_index = using_colon_call ? 2 : 1;

    Actor *actor = lua_runtime_find_actor(lua_state, actor_id);
    SpotLightComponentData *state = find_actor_spot_light(actor);
    if (!state) {
        lua_pushboolean(lua_state, 0);
        return 1;
    }

    const float value = (float)luaL_checknumber(lua_state, value_index);
    if (value <= 0.0f) {
        lua_pushboolean(lua_state, 0);
        return 1;
    }

    state->range = value;
    lua_pushboolean(lua_state, 1);
    return 1;
}

static int lua_actor_component_spot_light_get_range(lua_State *lua_state) {
    const char *actor_id = lua_tostring(lua_state, lua_upvalueindex(1));

    Actor *actor = lua_runtime_find_actor(lua_state, actor_id);
    SpotLightComponentData *state = find_actor_spot_light(actor);
    if (!state) {
        lua_pushnil(lua_state);
        return 1;
    }

    lua_pushnumber(lua_state, state->range);
    return 1;
}

static int lua_actor_component_spot_light_set_angle(lua_State *lua_state) {
    const char *actor_id = lua_tostring(lua_state, lua_upvalueindex(1));
    const int using_colon_call = lua_istable(lua_state, 1) ? 1 : 0;
    const int value_index = using_colon_call ? 2 : 1;

    Actor *actor = lua_runtime_find_actor(lua_state, actor_id);
    SpotLightComponentData *state = find_actor_spot_light(actor);
    if (!state) {
        lua_pushboolean(lua_state, 0);
        return 1;
    }

    const float value = (float)luaL_checknumber(lua_state, value_index);
    if (value <= 0.0f) {
        lua_pushboolean(lua_state, 0);
        return 1;
    }

    state->angle = value;
    lua_pushboolean(lua_state, 1);
    return 1;
}

static int lua_actor_component_spot_light_get_angle(lua_State *lua_state) {
    const char *actor_id = lua_tostring(lua_state, lua_upvalueindex(1));

    Actor *actor = lua_runtime_find_actor(lua_state, actor_id);
    SpotLightComponentData *state = find_actor_spot_light(actor);
    if (!state) {
        lua_pushnil(lua_state);
        return 1;
    }

    lua_pushnumber(lua_state, state->angle);
    return 1;
}

static int lua_actor_component_spot_light_set_enabled(lua_State *lua_state) {
    const char *actor_id = lua_tostring(lua_state, lua_upvalueindex(1));
    const int using_colon_call = lua_istable(lua_state, 1) ? 1 : 0;
    const int value_index = using_colon_call ? 2 : 1;

    Actor *actor = lua_runtime_find_actor(lua_state, actor_id);
    SpotLightComponentData *state = find_actor_spot_light(actor);
    if (!state) {
        lua_pushboolean(lua_state, 0);
        return 1;
    }

    state->enabled = lua_toboolean(lua_state, value_index) ? True : False;
    lua_pushboolean(lua_state, 1);
    return 1;
}

static int lua_actor_component_spot_light_get_enabled(lua_State *lua_state) {
    const char *actor_id = lua_tostring(lua_state, lua_upvalueindex(1));

    Actor *actor = lua_runtime_find_actor(lua_state, actor_id);
    SpotLightComponentData *state = find_actor_spot_light(actor);
    if (!state) {
        lua_pushnil(lua_state);
        return 1;
    }

    lua_pushboolean(lua_state, state->enabled ? 1 : 0);
    return 1;
}

static int lua_actor_component_direction_light_set_direction(lua_State *lua_state) {
    const char *actor_id = lua_tostring(lua_state, lua_upvalueindex(1));
    const int using_colon_call = lua_istable(lua_state, 1) ? 1 : 0;
    const int x_index = using_colon_call ? 2 : 1;
    const int y_index = using_colon_call ? 3 : 2;
    const int z_index = using_colon_call ? 4 : 3;

    Actor *actor = lua_runtime_find_actor(lua_state, actor_id);
    DirectionLightComponentData *state = find_actor_direction_light(actor);
    if (!state) {
        lua_pushboolean(lua_state, 0);
        return 1;
    }

    const float x = (float)luaL_checknumber(lua_state, x_index);
    const float y = (float)luaL_checknumber(lua_state, y_index);
    const float z = (float)luaL_checknumber(lua_state, z_index);
    if (fabsf(x) < 0.0001f && fabsf(y) < 0.0001f && fabsf(z) < 0.0001f) {
        lua_pushboolean(lua_state, 0);
        return 1;
    }

    state->direction = (Vector3){x, y, z};
    lua_pushboolean(lua_state, 1);
    return 1;
}

static int lua_actor_component_direction_light_get_direction(lua_State *lua_state) {
    const char *actor_id = lua_tostring(lua_state, lua_upvalueindex(1));

    Actor *actor = lua_runtime_find_actor(lua_state, actor_id);
    DirectionLightComponentData *state = find_actor_direction_light(actor);
    if (!state) {
        lua_pushnil(lua_state);
        lua_pushnil(lua_state);
        lua_pushnil(lua_state);
        return 3;
    }

    lua_pushnumber(lua_state, state->direction.x);
    lua_pushnumber(lua_state, state->direction.y);
    lua_pushnumber(lua_state, state->direction.z);
    return 3;
}

static int lua_actor_component_direction_light_set_color(lua_State *lua_state) {
    const char *actor_id = lua_tostring(lua_state, lua_upvalueindex(1));
    const int using_colon_call = lua_istable(lua_state, 1) ? 1 : 0;
    const int r_index = using_colon_call ? 2 : 1;
    const int g_index = using_colon_call ? 3 : 2;
    const int b_index = using_colon_call ? 4 : 3;
    const int a_index = using_colon_call ? 5 : 4;

    Actor *actor = lua_runtime_find_actor(lua_state, actor_id);
    DirectionLightComponentData *state = find_actor_direction_light(actor);
    if (!state) {
        lua_pushboolean(lua_state, 0);
        return 1;
    }

    const int red = (int)luaL_checkinteger(lua_state, r_index);
    const int green = (int)luaL_checkinteger(lua_state, g_index);
    const int blue = (int)luaL_checkinteger(lua_state, b_index);
    const int alpha = (int)luaL_checkinteger(lua_state, a_index);
    if (red < 0 || red > 255 || green < 0 || green > 255 || blue < 0 || blue > 255 || alpha < 0 || alpha > 255) {
        lua_pushboolean(lua_state, 0);
        return 1;
    }

    state->color = (Color){(unsigned char)red, (unsigned char)green, (unsigned char)blue, (unsigned char)alpha};
    lua_pushboolean(lua_state, 1);
    return 1;
}

static int lua_actor_component_direction_light_get_color(lua_State *lua_state) {
    const char *actor_id = lua_tostring(lua_state, lua_upvalueindex(1));

    Actor *actor = lua_runtime_find_actor(lua_state, actor_id);
    DirectionLightComponentData *state = find_actor_direction_light(actor);
    if (!state) {
        lua_pushnil(lua_state);
        lua_pushnil(lua_state);
        lua_pushnil(lua_state);
        lua_pushnil(lua_state);
        return 4;
    }

    lua_pushinteger(lua_state, state->color.r);
    lua_pushinteger(lua_state, state->color.g);
    lua_pushinteger(lua_state, state->color.b);
    lua_pushinteger(lua_state, state->color.a);
    return 4;
}

static int lua_actor_component_direction_light_set_intensity(lua_State *lua_state) {
    const char *actor_id = lua_tostring(lua_state, lua_upvalueindex(1));
    const int using_colon_call = lua_istable(lua_state, 1) ? 1 : 0;
    const int value_index = using_colon_call ? 2 : 1;

    Actor *actor = lua_runtime_find_actor(lua_state, actor_id);
    DirectionLightComponentData *state = find_actor_direction_light(actor);
    if (!state) {
        lua_pushboolean(lua_state, 0);
        return 1;
    }

    const float value = (float)luaL_checknumber(lua_state, value_index);
    if (value < 0.0f) {
        lua_pushboolean(lua_state, 0);
        return 1;
    }

    state->intensity = value;
    lua_pushboolean(lua_state, 1);
    return 1;
}

static int lua_actor_component_direction_light_get_intensity(lua_State *lua_state) {
    const char *actor_id = lua_tostring(lua_state, lua_upvalueindex(1));

    Actor *actor = lua_runtime_find_actor(lua_state, actor_id);
    DirectionLightComponentData *state = find_actor_direction_light(actor);
    if (!state) {
        lua_pushnil(lua_state);
        return 1;
    }

    lua_pushnumber(lua_state, state->intensity);
    return 1;
}

static int lua_actor_component_direction_light_set_enabled(lua_State *lua_state) {
    const char *actor_id = lua_tostring(lua_state, lua_upvalueindex(1));
    const int using_colon_call = lua_istable(lua_state, 1) ? 1 : 0;
    const int value_index = using_colon_call ? 2 : 1;

    Actor *actor = lua_runtime_find_actor(lua_state, actor_id);
    DirectionLightComponentData *state = find_actor_direction_light(actor);
    if (!state) {
        lua_pushboolean(lua_state, 0);
        return 1;
    }

    state->enabled = lua_toboolean(lua_state, value_index) ? True : False;
    lua_pushboolean(lua_state, 1);
    return 1;
}

static int lua_actor_component_direction_light_get_enabled(lua_State *lua_state) {
    const char *actor_id = lua_tostring(lua_state, lua_upvalueindex(1));

    Actor *actor = lua_runtime_find_actor(lua_state, actor_id);
    DirectionLightComponentData *state = find_actor_direction_light(actor);
    if (!state) {
        lua_pushnil(lua_state);
        return 1;
    }

    lua_pushboolean(lua_state, state->enabled ? 1 : 0);
    return 1;
}

static int lua_actor_get_component(lua_State *lua_state) {
    const char *actor_id = luaL_checkstring(lua_state, 1);
    const char *requested_component = luaL_checkstring(lua_state, 2);
    const char *resolved_component = resolve_component_name_alias(requested_component);

    if (!resolved_component) {
        lua_pushnil(lua_state);
        return 1;
    }

    Actor *actor = lua_runtime_find_actor(lua_state, actor_id);
    if (!actor) {
        lua_pushnil(lua_state);
        return 1;
    }

    if (strcmp(resolved_component, "AnimatedSprite") == 0) {
        if (!find_actor_animated_sprite(actor)) {
            lua_pushnil(lua_state);
            return 1;
        }

        lua_newtable(lua_state);

        lua_pushstring(lua_state, actor_id);
        lua_pushcclosure(lua_state, lua_actor_component_animconf_set, 1);
        lua_setfield(lua_state, -2, "set");

        lua_pushstring(lua_state, actor_id);
        lua_pushcclosure(lua_state, lua_actor_component_animconf_get, 1);
        lua_setfield(lua_state, -2, "get");

        lua_pushstring(lua_state, actor_id);
        lua_pushcclosure(lua_state, lua_actor_component_animconf_set_number, 1);
        lua_setfield(lua_state, -2, "set_number");

        lua_pushstring(lua_state, actor_id);
        lua_pushcclosure(lua_state, lua_actor_component_animconf_get_number, 1);
        lua_setfield(lua_state, -2, "get_number");

        lua_pushstring(lua_state, actor_id);
        lua_pushcclosure(lua_state, lua_actor_component_animconf_set_flip_x, 1);
        lua_setfield(lua_state, -2, "set_flip_x");

        lua_pushstring(lua_state, actor_id);
        lua_pushcclosure(lua_state, lua_actor_component_animconf_get_flip_x, 1);
        lua_setfield(lua_state, -2, "get_flip_x");

        return 1;
    }

    if (strcmp(resolved_component, "Collider") == 0) {
        if (!find_actor_collider(actor)) {
            lua_pushnil(lua_state);
            return 1;
        }

        lua_newtable(lua_state);

        lua_pushstring(lua_state, actor_id);
        lua_pushcclosure(lua_state, lua_actor_component_collider_set_offset, 1);
        lua_setfield(lua_state, -2, "set_offset");

        lua_pushstring(lua_state, actor_id);
        lua_pushcclosure(lua_state, lua_actor_component_collider_get_offset, 1);
        lua_setfield(lua_state, -2, "get_offset");

        lua_pushstring(lua_state, actor_id);
        lua_pushcclosure(lua_state, lua_actor_component_collider_set_size, 1);
        lua_setfield(lua_state, -2, "set_size");

        lua_pushstring(lua_state, actor_id);
        lua_pushcclosure(lua_state, lua_actor_component_collider_get_size, 1);
        lua_setfield(lua_state, -2, "get_size");

        lua_pushstring(lua_state, actor_id);
        lua_pushcclosure(lua_state, lua_actor_component_collider_set_enabled, 1);
        lua_setfield(lua_state, -2, "set_enabled");

        lua_pushstring(lua_state, actor_id);
        lua_pushcclosure(lua_state, lua_actor_component_collider_get_enabled, 1);
        lua_setfield(lua_state, -2, "get_enabled");

        lua_pushstring(lua_state, actor_id);
        lua_pushcclosure(lua_state, lua_actor_component_collider_set_is_trigger, 1);
        lua_setfield(lua_state, -2, "set_is_trigger");

        lua_pushstring(lua_state, actor_id);
        lua_pushcclosure(lua_state, lua_actor_component_collider_get_is_trigger, 1);
        lua_setfield(lua_state, -2, "get_is_trigger");

        lua_pushstring(lua_state, actor_id);
        lua_pushcclosure(lua_state, lua_actor_component_collider_get_bounds, 1);
        lua_setfield(lua_state, -2, "get_bounds");

        lua_pushstring(lua_state, actor_id);
        lua_pushcclosure(lua_state, lua_actor_component_collider_overlaps_actor, 1);
        lua_setfield(lua_state, -2, "overlaps_actor");

        lua_pushstring(lua_state, actor_id);
        lua_pushcclosure(lua_state, lua_actor_component_collider_overlaps_all, 1);
        lua_setfield(lua_state, -2, "overlaps_all");

        lua_pushstring(lua_state, actor_id);
        lua_pushcclosure(lua_state, lua_actor_component_collider_overlaps_point, 1);
        lua_setfield(lua_state, -2, "overlaps_point");

        return 1;
    }

    if (strcmp(resolved_component, "StaticColor") == 0) {
        if (!find_actor_static_color(actor)) {
            lua_pushnil(lua_state);
            return 1;
        }

        lua_newtable(lua_state);

        lua_pushstring(lua_state, actor_id);
        lua_pushcclosure(lua_state, lua_actor_component_static_color_set_position, 1);
        lua_setfield(lua_state, -2, "set_position");

        lua_pushstring(lua_state, actor_id);
        lua_pushcclosure(lua_state, lua_actor_component_static_color_get_position, 1);
        lua_setfield(lua_state, -2, "get_position");

        lua_pushstring(lua_state, actor_id);
        lua_pushcclosure(lua_state, lua_actor_component_static_color_set_size, 1);
        lua_setfield(lua_state, -2, "set_size");

        lua_pushstring(lua_state, actor_id);
        lua_pushcclosure(lua_state, lua_actor_component_static_color_get_size, 1);
        lua_setfield(lua_state, -2, "get_size");

        lua_pushstring(lua_state, actor_id);
        lua_pushcclosure(lua_state, lua_actor_component_static_color_set_anchor, 1);
        lua_setfield(lua_state, -2, "set_anchor");

        lua_pushstring(lua_state, actor_id);
        lua_pushcclosure(lua_state, lua_actor_component_static_color_set_anchor_center, 1);
        lua_setfield(lua_state, -2, "set_anchor_center");

        lua_pushstring(lua_state, actor_id);
        lua_pushcclosure(lua_state, lua_actor_component_static_color_get_anchor, 1);
        lua_setfield(lua_state, -2, "get_anchor");

        lua_pushstring(lua_state, actor_id);
        lua_pushcclosure(lua_state, lua_actor_component_static_color_set_rotation, 1);
        lua_setfield(lua_state, -2, "set_rotation");

        lua_pushstring(lua_state, actor_id);
        lua_pushcclosure(lua_state, lua_actor_component_static_color_get_rotation, 1);
        lua_setfield(lua_state, -2, "get_rotation");

        lua_pushstring(lua_state, actor_id);
        lua_pushcclosure(lua_state, lua_actor_component_static_color_set_color, 1);
        lua_setfield(lua_state, -2, "set_color");

        lua_pushstring(lua_state, actor_id);
        lua_pushcclosure(lua_state, lua_actor_component_static_color_get_color, 1);
        lua_setfield(lua_state, -2, "get_color");

        return 1;
    }

    if (strcmp(resolved_component, "Rigidbody") == 0) {
        if (!find_actor_rigidbody(actor)) {
            lua_pushnil(lua_state);
            return 1;
        }

        lua_newtable(lua_state);

        lua_pushstring(lua_state, actor_id);
        lua_pushcclosure(lua_state, lua_actor_component_rigidbody_set_body_type, 1);
        lua_setfield(lua_state, -2, "set_body_type");

        lua_pushstring(lua_state, actor_id);
        lua_pushcclosure(lua_state, lua_actor_component_rigidbody_get_body_type, 1);
        lua_setfield(lua_state, -2, "get_body_type");

        lua_pushstring(lua_state, actor_id);
        lua_pushcclosure(lua_state, lua_actor_component_rigidbody_set_simulated, 1);
        lua_setfield(lua_state, -2, "set_simulated");

        lua_pushstring(lua_state, actor_id);
        lua_pushcclosure(lua_state, lua_actor_component_rigidbody_get_simulated, 1);
        lua_setfield(lua_state, -2, "get_simulated");

        lua_pushstring(lua_state, actor_id);
        lua_pushcclosure(lua_state, lua_actor_component_rigidbody_set_use_gravity, 1);
        lua_setfield(lua_state, -2, "set_use_gravity");

        lua_pushstring(lua_state, actor_id);
        lua_pushcclosure(lua_state, lua_actor_component_rigidbody_get_use_gravity, 1);
        lua_setfield(lua_state, -2, "get_use_gravity");

        lua_pushstring(lua_state, actor_id);
        lua_pushcclosure(lua_state, lua_actor_component_rigidbody_set_mass, 1);
        lua_setfield(lua_state, -2, "set_mass");

        lua_pushstring(lua_state, actor_id);
        lua_pushcclosure(lua_state, lua_actor_component_rigidbody_get_mass, 1);
        lua_setfield(lua_state, -2, "get_mass");

        lua_pushstring(lua_state, actor_id);
        lua_pushcclosure(lua_state, lua_actor_component_rigidbody_set_gravity_scale, 1);
        lua_setfield(lua_state, -2, "set_gravity_scale");

        lua_pushstring(lua_state, actor_id);
        lua_pushcclosure(lua_state, lua_actor_component_rigidbody_get_gravity_scale, 1);
        lua_setfield(lua_state, -2, "get_gravity_scale");

        lua_pushstring(lua_state, actor_id);
        lua_pushcclosure(lua_state, lua_actor_component_rigidbody_set_linear_drag, 1);
        lua_setfield(lua_state, -2, "set_linear_drag");

        lua_pushstring(lua_state, actor_id);
        lua_pushcclosure(lua_state, lua_actor_component_rigidbody_get_linear_drag, 1);
        lua_setfield(lua_state, -2, "get_linear_drag");

        lua_pushstring(lua_state, actor_id);
        lua_pushcclosure(lua_state, lua_actor_component_rigidbody_set_angular_drag, 1);
        lua_setfield(lua_state, -2, "set_angular_drag");

        lua_pushstring(lua_state, actor_id);
        lua_pushcclosure(lua_state, lua_actor_component_rigidbody_get_angular_drag, 1);
        lua_setfield(lua_state, -2, "get_angular_drag");

        lua_pushstring(lua_state, actor_id);
        lua_pushcclosure(lua_state, lua_actor_component_rigidbody_set_velocity, 1);
        lua_setfield(lua_state, -2, "set_velocity");

        lua_pushstring(lua_state, actor_id);
        lua_pushcclosure(lua_state, lua_actor_component_rigidbody_get_velocity, 1);
        lua_setfield(lua_state, -2, "get_velocity");

        lua_pushstring(lua_state, actor_id);
        lua_pushcclosure(lua_state, lua_actor_component_rigidbody_set_angular_velocity, 1);
        lua_setfield(lua_state, -2, "set_angular_velocity");

        lua_pushstring(lua_state, actor_id);
        lua_pushcclosure(lua_state, lua_actor_component_rigidbody_get_angular_velocity, 1);
        lua_setfield(lua_state, -2, "get_angular_velocity");

        lua_pushstring(lua_state, actor_id);
        lua_pushcclosure(lua_state, lua_actor_component_rigidbody_set_freeze_position, 1);
        lua_setfield(lua_state, -2, "set_freeze_position");

        lua_pushstring(lua_state, actor_id);
        lua_pushcclosure(lua_state, lua_actor_component_rigidbody_get_freeze_position, 1);
        lua_setfield(lua_state, -2, "get_freeze_position");

        lua_pushstring(lua_state, actor_id);
        lua_pushcclosure(lua_state, lua_actor_component_rigidbody_set_freeze_rotation, 1);
        lua_setfield(lua_state, -2, "set_freeze_rotation");

        lua_pushstring(lua_state, actor_id);
        lua_pushcclosure(lua_state, lua_actor_component_rigidbody_get_freeze_rotation, 1);
        lua_setfield(lua_state, -2, "get_freeze_rotation");

        lua_pushstring(lua_state, actor_id);
        lua_pushcclosure(lua_state, lua_actor_component_rigidbody_add_force, 1);
        lua_setfield(lua_state, -2, "add_force");

        lua_pushstring(lua_state, actor_id);
        lua_pushcclosure(lua_state, lua_actor_component_rigidbody_apply_force, 1);
        lua_setfield(lua_state, -2, "apply_force");

        lua_pushstring(lua_state, actor_id);
        lua_pushcclosure(lua_state, lua_actor_component_rigidbody_add_torque, 1);
        lua_setfield(lua_state, -2, "add_torque");

        lua_pushstring(lua_state, actor_id);
        lua_pushcclosure(lua_state, lua_actor_component_rigidbody_clear_forces, 1);
        lua_setfield(lua_state, -2, "clear_forces");

        lua_pushstring(lua_state, actor_id);
        lua_pushcclosure(lua_state, lua_actor_component_rigidbody_move_position, 1);
        lua_setfield(lua_state, -2, "move_position");

        lua_pushstring(lua_state, actor_id);
        lua_pushcclosure(lua_state, lua_actor_component_rigidbody_move_rotation, 1);
        lua_setfield(lua_state, -2, "move_rotation");

        lua_pushstring(lua_state, actor_id);
        lua_pushcclosure(lua_state, lua_actor_component_rigidbody_is_grounded, 1);
        lua_setfield(lua_state, -2, "is_grounded");

        return 1;
    }

    if (strcmp(resolved_component, "PointLight") == 0) {
        if (!find_actor_point_light(actor)) {
            lua_pushnil(lua_state);
            return 1;
        }

        lua_newtable(lua_state);

        lua_pushstring(lua_state, actor_id);
        lua_pushcclosure(lua_state, lua_actor_component_point_light_set_position, 1);
        lua_setfield(lua_state, -2, "set_position");

        lua_pushstring(lua_state, actor_id);
        lua_pushcclosure(lua_state, lua_actor_component_point_light_get_position, 1);
        lua_setfield(lua_state, -2, "get_position");

        lua_pushstring(lua_state, actor_id);
        lua_pushcclosure(lua_state, lua_actor_component_point_light_set_color, 1);
        lua_setfield(lua_state, -2, "set_color");

        lua_pushstring(lua_state, actor_id);
        lua_pushcclosure(lua_state, lua_actor_component_point_light_get_color, 1);
        lua_setfield(lua_state, -2, "get_color");

        lua_pushstring(lua_state, actor_id);
        lua_pushcclosure(lua_state, lua_actor_component_point_light_set_intensity, 1);
        lua_setfield(lua_state, -2, "set_intensity");

        lua_pushstring(lua_state, actor_id);
        lua_pushcclosure(lua_state, lua_actor_component_point_light_get_intensity, 1);
        lua_setfield(lua_state, -2, "get_intensity");

        lua_pushstring(lua_state, actor_id);
        lua_pushcclosure(lua_state, lua_actor_component_point_light_set_range, 1);
        lua_setfield(lua_state, -2, "set_range");

        lua_pushstring(lua_state, actor_id);
        lua_pushcclosure(lua_state, lua_actor_component_point_light_get_range, 1);
        lua_setfield(lua_state, -2, "get_range");

        lua_pushstring(lua_state, actor_id);
        lua_pushcclosure(lua_state, lua_actor_component_point_light_set_enabled, 1);
        lua_setfield(lua_state, -2, "set_enabled");

        lua_pushstring(lua_state, actor_id);
        lua_pushcclosure(lua_state, lua_actor_component_point_light_get_enabled, 1);
        lua_setfield(lua_state, -2, "get_enabled");

        return 1;
    }

    if (strcmp(resolved_component, "SpotLight") == 0) {
        if (!find_actor_spot_light(actor)) {
            lua_pushnil(lua_state);
            return 1;
        }

        lua_newtable(lua_state);

        lua_pushstring(lua_state, actor_id);
        lua_pushcclosure(lua_state, lua_actor_component_spot_light_set_position, 1);
        lua_setfield(lua_state, -2, "set_position");

        lua_pushstring(lua_state, actor_id);
        lua_pushcclosure(lua_state, lua_actor_component_spot_light_get_position, 1);
        lua_setfield(lua_state, -2, "get_position");

        lua_pushstring(lua_state, actor_id);
        lua_pushcclosure(lua_state, lua_actor_component_spot_light_set_direction, 1);
        lua_setfield(lua_state, -2, "set_direction");

        lua_pushstring(lua_state, actor_id);
        lua_pushcclosure(lua_state, lua_actor_component_spot_light_get_direction, 1);
        lua_setfield(lua_state, -2, "get_direction");

        lua_pushstring(lua_state, actor_id);
        lua_pushcclosure(lua_state, lua_actor_component_spot_light_set_color, 1);
        lua_setfield(lua_state, -2, "set_color");

        lua_pushstring(lua_state, actor_id);
        lua_pushcclosure(lua_state, lua_actor_component_spot_light_get_color, 1);
        lua_setfield(lua_state, -2, "get_color");

        lua_pushstring(lua_state, actor_id);
        lua_pushcclosure(lua_state, lua_actor_component_spot_light_set_intensity, 1);
        lua_setfield(lua_state, -2, "set_intensity");

        lua_pushstring(lua_state, actor_id);
        lua_pushcclosure(lua_state, lua_actor_component_spot_light_get_intensity, 1);
        lua_setfield(lua_state, -2, "get_intensity");

        lua_pushstring(lua_state, actor_id);
        lua_pushcclosure(lua_state, lua_actor_component_spot_light_set_range, 1);
        lua_setfield(lua_state, -2, "set_range");

        lua_pushstring(lua_state, actor_id);
        lua_pushcclosure(lua_state, lua_actor_component_spot_light_get_range, 1);
        lua_setfield(lua_state, -2, "get_range");

        lua_pushstring(lua_state, actor_id);
        lua_pushcclosure(lua_state, lua_actor_component_spot_light_set_angle, 1);
        lua_setfield(lua_state, -2, "set_angle");

        lua_pushstring(lua_state, actor_id);
        lua_pushcclosure(lua_state, lua_actor_component_spot_light_get_angle, 1);
        lua_setfield(lua_state, -2, "get_angle");

        lua_pushstring(lua_state, actor_id);
        lua_pushcclosure(lua_state, lua_actor_component_spot_light_set_enabled, 1);
        lua_setfield(lua_state, -2, "set_enabled");

        lua_pushstring(lua_state, actor_id);
        lua_pushcclosure(lua_state, lua_actor_component_spot_light_get_enabled, 1);
        lua_setfield(lua_state, -2, "get_enabled");

        return 1;
    }

    if (strcmp(resolved_component, "DirectionLight") == 0) {
        if (!find_actor_direction_light(actor)) {
            lua_pushnil(lua_state);
            return 1;
        }

        lua_newtable(lua_state);

        lua_pushstring(lua_state, actor_id);
        lua_pushcclosure(lua_state, lua_actor_component_direction_light_set_direction, 1);
        lua_setfield(lua_state, -2, "set_direction");

        lua_pushstring(lua_state, actor_id);
        lua_pushcclosure(lua_state, lua_actor_component_direction_light_get_direction, 1);
        lua_setfield(lua_state, -2, "get_direction");

        lua_pushstring(lua_state, actor_id);
        lua_pushcclosure(lua_state, lua_actor_component_direction_light_set_color, 1);
        lua_setfield(lua_state, -2, "set_color");

        lua_pushstring(lua_state, actor_id);
        lua_pushcclosure(lua_state, lua_actor_component_direction_light_get_color, 1);
        lua_setfield(lua_state, -2, "get_color");

        lua_pushstring(lua_state, actor_id);
        lua_pushcclosure(lua_state, lua_actor_component_direction_light_set_intensity, 1);
        lua_setfield(lua_state, -2, "set_intensity");

        lua_pushstring(lua_state, actor_id);
        lua_pushcclosure(lua_state, lua_actor_component_direction_light_get_intensity, 1);
        lua_setfield(lua_state, -2, "get_intensity");

        lua_pushstring(lua_state, actor_id);
        lua_pushcclosure(lua_state, lua_actor_component_direction_light_set_enabled, 1);
        lua_setfield(lua_state, -2, "set_enabled");

        lua_pushstring(lua_state, actor_id);
        lua_pushcclosure(lua_state, lua_actor_component_direction_light_get_enabled, 1);
        lua_setfield(lua_state, -2, "get_enabled");

        return 1;
    }

    lua_pushnil(lua_state);
    return 1;
}

static int lua_actor_get_position(lua_State *lua_state) {
    const char *actor_id = luaL_checkstring(lua_state, 1);
    Actor *actor = lua_runtime_find_actor(lua_state, actor_id);
    if (!actor) {
        lua_pushnil(lua_state);
        lua_pushnil(lua_state);
        lua_pushnil(lua_state);
        return 3;
    }

    lua_pushnumber(lua_state, actor->transform.position.x);
    lua_pushnumber(lua_state, actor->transform.position.y);
    lua_pushnumber(lua_state, actor->transform.position.z);
    return 3;
}

static int lua_actor_get_anchor_position(lua_State *lua_state) {
    const char *actor_id = luaL_checkstring(lua_state, 1);
    Actor *actor = lua_runtime_find_actor(lua_state, actor_id);
    if (!actor) {
        lua_pushnil(lua_state);
        lua_pushnil(lua_state);
        lua_pushnil(lua_state);
        return 3;
    }

    const Vector3 anchor = actor_visual_anchor_position(actor);
    lua_pushnumber(lua_state, anchor.x);
    lua_pushnumber(lua_state, anchor.y);
    lua_pushnumber(lua_state, anchor.z);
    return 3;
}

static int lua_actor_destroy(lua_State *lua_state) {
    const char *actor_id = luaL_checkstring(lua_state, 1);
    lua_pushboolean(lua_state, runtime_destroy_actor(actor_id) == Ok ? 1 : 0);
    return 1;
}

static const luaL_Reg actor_methods[] = {
    {"get_position", lua_actor_get_position},
    {"get_anchor_position", lua_actor_get_anchor_position},
    {"get_component", lua_actor_get_component},
    {"destroy", lua_actor_destroy},
    {NULL, NULL},
};

int luaopen_engine_actor(lua_State *lua_state) {
    lua_newtable(lua_state);
    luaL_setfuncs(lua_state, actor_methods, 0);
    return 1;
}
