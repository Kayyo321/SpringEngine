#include "script_runtime_internal.h"

static int lua_transform_translate(lua_State *lua_state) {
    Actor *actor = lua_runtime_current_actor(lua_state);
    if (!actor)
        return 0;

    const float dx = (float)luaL_optnumber(lua_state, 1, 0.0);
    const float dy = (float)luaL_optnumber(lua_state, 2, 0.0);
    const float dz = (float)luaL_optnumber(lua_state, 3, 0.0);

    actor->transform.position.x += dx;
    actor->transform.position.y += dy;
    actor->transform.position.z += dz;
    return 0;
}

static int lua_transform_set_position(lua_State *lua_state) {
    Actor *actor = lua_runtime_current_actor(lua_state);
    if (!actor)
        return 0;

    actor->transform.position.x = (float)luaL_checknumber(lua_state, 1);
    actor->transform.position.y = (float)luaL_checknumber(lua_state, 2);
    actor->transform.position.z = (float)luaL_checknumber(lua_state, 3);
    return 0;
}

static int lua_transform_get_position(lua_State *lua_state) {
    Actor *actor = lua_runtime_current_actor(lua_state);
    if (!actor) {
        lua_pushnumber(lua_state, 0.0);
        lua_pushnumber(lua_state, 0.0);
        lua_pushnumber(lua_state, 0.0);
        return 3;
    }

    lua_pushnumber(lua_state, actor->transform.position.x);
    lua_pushnumber(lua_state, actor->transform.position.y);
    lua_pushnumber(lua_state, actor->transform.position.z);
    return 3;
}

static int lua_transform_set_rotation_euler(lua_State *lua_state) {
    Actor *actor = lua_runtime_current_actor(lua_state);
    if (!actor)
        return 0;

    actor->transform.rotation_euler.x = (float)luaL_checknumber(lua_state, 1);
    actor->transform.rotation_euler.y = (float)luaL_checknumber(lua_state, 2);
    actor->transform.rotation_euler.z = (float)luaL_checknumber(lua_state, 3);
    return 0;
}

static int lua_transform_get_rotation_euler(lua_State *lua_state) {
    Actor *actor = lua_runtime_current_actor(lua_state);
    if (!actor) {
        lua_pushnumber(lua_state, 0.0);
        lua_pushnumber(lua_state, 0.0);
        lua_pushnumber(lua_state, 0.0);
        return 3;
    }

    lua_pushnumber(lua_state, actor->transform.rotation_euler.x);
    lua_pushnumber(lua_state, actor->transform.rotation_euler.y);
    lua_pushnumber(lua_state, actor->transform.rotation_euler.z);
    return 3;
}

static int lua_transform_set_scale(lua_State *lua_state) {
    Actor *actor = lua_runtime_current_actor(lua_state);
    if (!actor)
        return 0;

    actor->transform.scale.x = (float)luaL_checknumber(lua_state, 1);
    actor->transform.scale.y = (float)luaL_checknumber(lua_state, 2);
    actor->transform.scale.z = (float)luaL_checknumber(lua_state, 3);
    return 0;
}

static int lua_transform_get_scale(lua_State *lua_state) {
    Actor *actor = lua_runtime_current_actor(lua_state);
    if (!actor) {
        lua_pushnumber(lua_state, 1.0);
        lua_pushnumber(lua_state, 1.0);
        lua_pushnumber(lua_state, 1.0);
        return 3;
    }

    lua_pushnumber(lua_state, actor->transform.scale.x);
    lua_pushnumber(lua_state, actor->transform.scale.y);
    lua_pushnumber(lua_state, actor->transform.scale.z);
    return 3;
}

static const luaL_Reg transform_methods[] = {
    {"translate", lua_transform_translate},
    {"set_position", lua_transform_set_position},
    {"get_position", lua_transform_get_position},
    {"set_rotation_euler", lua_transform_set_rotation_euler},
    {"get_rotation_euler", lua_transform_get_rotation_euler},
    {"set_scale", lua_transform_set_scale},
    {"get_scale", lua_transform_get_scale},
    {NULL, NULL},
};

int luaopen_engine_transform(lua_State *lua_state) {
    lua_newtable(lua_state);
    luaL_setfuncs(lua_state, transform_methods, 0);
    return 1;
}
