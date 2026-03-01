#include "script_runtime_internal.h"

static int lua_engine_log(lua_State *lua_state) {
    const char *message = luaL_checkstring(lua_state, 1);
    log_msg("[Lua] %s", message ? message : "");
    return 0;
}

static int lua_engine_warn(lua_State *lua_state) {
    const char *message = luaL_checkstring(lua_state, 1);
    log_warn("[Lua] %s", message ? message : "");
    return 0;
}

static int lua_engine_error(lua_State *lua_state) {
    const char *message = luaL_checkstring(lua_state, 1);
    log_err("[Lua] %s", message ? message : "");
    return 0;
}

static int lua_engine_version(lua_State *lua_state) {
    lua_pushstring(lua_state, Version);
    return 1;
}

int luaopen_engine(lua_State *lua_state) {
    lua_newtable(lua_state);

    lua_pushcfunction(lua_state, lua_engine_log);
    lua_setfield(lua_state, -2, "log");
    lua_pushcfunction(lua_state, lua_engine_warn);
    lua_setfield(lua_state, -2, "warn");
    lua_pushcfunction(lua_state, lua_engine_error);
    lua_setfield(lua_state, -2, "error");
    lua_pushcfunction(lua_state, lua_engine_version);
    lua_setfield(lua_state, -2, "version");

    luaL_requiref(lua_state, "Engine.Input", luaopen_engine_input, 0);
    lua_setfield(lua_state, -2, "Input");

    luaL_requiref(lua_state, "Engine.Time", luaopen_engine_time, 0);
    lua_setfield(lua_state, -2, "Time");

    luaL_requiref(lua_state, "Engine.Transform", luaopen_engine_transform, 0);
    lua_setfield(lua_state, -2, "Transform");

    luaL_requiref(lua_state, "Engine.Actor", luaopen_engine_actor, 0);
    lua_setfield(lua_state, -2, "Actor");

    luaL_requiref(lua_state, "Engine.Camera", luaopen_engine_camera, 0);
    lua_setfield(lua_state, -2, "Camera");

    luaL_requiref(lua_state, "Engine.Scene", luaopen_engine_scene, 0);
    lua_setfield(lua_state, -2, "Scene");

    luaL_requiref(lua_state, "Engine.DJ", luaopen_engine_dj, 0);
    lua_setfield(lua_state, -2, "DJ");

    luaL_requiref(lua_state, "Engine.UI", luaopen_engine_ui, 0);
    lua_setfield(lua_state, -2, "UI");

    return 1;
}
