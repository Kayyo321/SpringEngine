#include "script_runtime_internal.h"

static result lua_collider_get_current(lua_State *lua_state, Actor **out_actor, ColliderComponentData **out_collider) {
    if (!out_actor || !out_collider)
        return Err;

    *out_actor = Null;
    *out_collider = Null;

    Actor *actor = lua_runtime_current_actor(lua_state);
    if (!actor)
        return Err;

    ColliderComponentData *collider = actor_find_collider_component(actor);
    if (!collider)
        return Err;

    *out_actor = actor;
    *out_collider = collider;
    return Ok;
}

static int lua_collider_set_offset(lua_State *lua_state) {
    Actor *actor = Null;
    ColliderComponentData *collider = Null;
    if (lua_collider_get_current(lua_state, &actor, &collider) != Ok)
        return 0;

    (void)actor;

    collider->offset.x = (float)luaL_checknumber(lua_state, 1);
    collider->offset.y = (float)luaL_checknumber(lua_state, 2);
    return 0;
}

static int lua_collider_get_offset(lua_State *lua_state) {
    Actor *actor = Null;
    ColliderComponentData *collider = Null;
    if (lua_collider_get_current(lua_state, &actor, &collider) != Ok) {
        lua_pushnumber(lua_state, 0.0);
        lua_pushnumber(lua_state, 0.0);
        return 2;
    }

    lua_pushnumber(lua_state, collider->offset.x);
    lua_pushnumber(lua_state, collider->offset.y);
    return 2;
}

static int lua_collider_set_size(lua_State *lua_state) {
    Actor *actor = Null;
    ColliderComponentData *collider = Null;
    if (lua_collider_get_current(lua_state, &actor, &collider) != Ok)
        return 0;

    (void)actor;

    const float width = (float)luaL_checknumber(lua_state, 1);
    const float height = (float)luaL_checknumber(lua_state, 2);
    if (width > 0.0f)
        collider->size.x = width;
    if (height > 0.0f)
        collider->size.y = height;
    return 0;
}

static int lua_collider_get_size(lua_State *lua_state) {
    Actor *actor = Null;
    ColliderComponentData *collider = Null;
    if (lua_collider_get_current(lua_state, &actor, &collider) != Ok) {
        lua_pushnumber(lua_state, 0.0);
        lua_pushnumber(lua_state, 0.0);
        return 2;
    }

    lua_pushnumber(lua_state, collider->size.x);
    lua_pushnumber(lua_state, collider->size.y);
    return 2;
}

static int lua_collider_set_enabled(lua_State *lua_state) {
    Actor *actor = Null;
    ColliderComponentData *collider = Null;
    if (lua_collider_get_current(lua_state, &actor, &collider) != Ok)
        return 0;

    (void)actor;

    collider->enabled = lua_toboolean(lua_state, 1) ? True : False;
    return 0;
}

static int lua_collider_get_enabled(lua_State *lua_state) {
    Actor *actor = Null;
    ColliderComponentData *collider = Null;
    if (lua_collider_get_current(lua_state, &actor, &collider) != Ok) {
        lua_pushboolean(lua_state, 0);
        return 1;
    }

    lua_pushboolean(lua_state, collider->enabled ? 1 : 0);
    return 1;
}

static int lua_collider_set_is_trigger(lua_State *lua_state) {
    Actor *actor = Null;
    ColliderComponentData *collider = Null;
    if (lua_collider_get_current(lua_state, &actor, &collider) != Ok)
        return 0;

    (void)actor;

    collider->is_trigger = lua_toboolean(lua_state, 1) ? True : False;
    return 0;
}

static int lua_collider_get_is_trigger(lua_State *lua_state) {
    Actor *actor = Null;
    ColliderComponentData *collider = Null;
    if (lua_collider_get_current(lua_state, &actor, &collider) != Ok) {
        lua_pushboolean(lua_state, 0);
        return 1;
    }

    lua_pushboolean(lua_state, collider->is_trigger ? 1 : 0);
    return 1;
}

static int lua_collider_get_bounds(lua_State *lua_state) {
    Actor *actor = Null;
    ColliderComponentData *collider = Null;
    if (lua_collider_get_current(lua_state, &actor, &collider) != Ok) {
        lua_pushnumber(lua_state, 0.0);
        lua_pushnumber(lua_state, 0.0);
        lua_pushnumber(lua_state, 0.0);
        lua_pushnumber(lua_state, 0.0);
        return 4;
    }

    const Rectangle bounds = collider_world_bounds(actor, collider);
    lua_pushnumber(lua_state, bounds.x);
    lua_pushnumber(lua_state, bounds.y);
    lua_pushnumber(lua_state, bounds.width);
    lua_pushnumber(lua_state, bounds.height);
    return 4;
}

static int lua_collider_overlaps_actor(lua_State *lua_state) {
    Actor *current_actor = Null;
    ColliderComponentData *current_collider = Null;
    if (lua_collider_get_current(lua_state, &current_actor, &current_collider) != Ok) {
        lua_pushboolean(lua_state, 0);
        return 1;
    }

    const char *target_actor_id = luaL_checkstring(lua_state, 1);
    Actor *target_actor = lua_runtime_find_actor(lua_state, target_actor_id);
    if (!target_actor) {
        lua_pushboolean(lua_state, 0);
        return 1;
    }

    ColliderComponentData *target_collider = actor_find_collider_component(target_actor);
    if (!target_collider) {
        lua_pushboolean(lua_state, 0);
        return 1;
    }

    lua_pushboolean(lua_state, collider_components_overlap(current_actor, current_collider, target_actor, target_collider) ? 1 : 0);
    return 1;
}

static int lua_collider_overlaps_point(lua_State *lua_state) {
    Actor *actor = Null;
    ColliderComponentData *collider = Null;
    if (lua_collider_get_current(lua_state, &actor, &collider) != Ok) {
        lua_pushboolean(lua_state, 0);
        return 1;
    }

    const float x = (float)luaL_checknumber(lua_state, 1);
    const float y = (float)luaL_checknumber(lua_state, 2);
    const Vector2 point = {x, y};
    const Rectangle bounds = collider_world_bounds(actor, collider);
    lua_pushboolean(lua_state, CheckCollisionPointRec(point, bounds) ? 1 : 0);
    return 1;
}

static const luaL_Reg collider_methods[] = {
    {"set_offset", lua_collider_set_offset},
    {"get_offset", lua_collider_get_offset},
    {"set_size", lua_collider_set_size},
    {"get_size", lua_collider_get_size},
    {"set_enabled", lua_collider_set_enabled},
    {"get_enabled", lua_collider_get_enabled},
    {"set_is_trigger", lua_collider_set_is_trigger},
    {"get_is_trigger", lua_collider_get_is_trigger},
    {"get_bounds", lua_collider_get_bounds},
    {"overlaps_actor", lua_collider_overlaps_actor},
    {"overlaps_point", lua_collider_overlaps_point},
    {NULL, NULL},
};

int luaopen_engine_collider(lua_State *lua_state) {
    lua_newtable(lua_state);
    luaL_setfuncs(lua_state, collider_methods, 0);
    return 1;
}
