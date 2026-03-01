#include "script_runtime_internal.h"

static const char *resolve_component_name_alias(const char *component_name) {
    if (!component_name || component_name[0] == '\0')
        return Null;

    if (strcmp(component_name, "AnimConf") == 0)
        return "AnimatedSprite";

    return component_name;
}

static ActorComponent *find_builtin_component(Actor *actor, const char *component_name) {
    if (!actor || !component_name || component_name[0] == '\0')
        return Null;

    for (usize component_index = 0; component_index < actor->component_count; ++component_index) {
        ActorComponent *component = &actor->components[component_index];
        if (component->descriptor.kind != ComponentBuiltin || !component->descriptor.name)
            continue;

        if (strcmp(component->descriptor.name, component_name) == 0)
            return component;
    }

    return Null;
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
