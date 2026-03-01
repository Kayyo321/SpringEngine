#include "script_runtime_internal.h"

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

static const luaL_Reg actor_methods[] = {
    {"get_position", lua_actor_get_position},
    {"get_anchor_position", lua_actor_get_anchor_position},
    {NULL, NULL},
};

int luaopen_engine_actor(lua_State *lua_state) {
    lua_newtable(lua_state);
    luaL_setfuncs(lua_state, actor_methods, 0);
    return 1;
}
