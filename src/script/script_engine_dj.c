#include "script_runtime_internal.h"

static result resolve_audio_path(lua_State *lua_state, const char *path, char *out_path, usize out_size) {
    if (!lua_state || !path || !out_path || out_size == 0)
        return Err;

    ScriptRuntime *runtime = lua_runtime_instance(lua_state);
    if (!runtime)
        return Err;

    return runtime_join_path(runtime->project_root, path, out_path, out_size);
}

static int lua_dj_load_sound(lua_State *lua_state) {
    const char *path = luaL_checkstring(lua_state, 1);
    const char *alias = luaL_checkstring(lua_state, 2);
    const float default_volume = (float)luaL_optnumber(lua_state, 3, 1.0);

    ScriptRuntime *runtime = lua_runtime_instance(lua_state);
    if (!runtime || !runtime->dj)
        return luaL_error(lua_state, "DJ runtime is not initialized");

    char full_path[PATH_MAX] = {0};
    if (resolve_audio_path(lua_state, path, full_path, sizeof(full_path)) != Ok)
        return luaL_error(lua_state, "Failed to resolve DJ sound path '%s'", path);

    load_sound_source_as_with_volume(runtime->dj, full_path, alias, default_volume);
    return 0;
}

static int lua_dj_play_sound(lua_State *lua_state) {
    const char *alias = luaL_checkstring(lua_state, 1);

    ScriptRuntime *runtime = lua_runtime_instance(lua_state);
    if (!runtime || !runtime->dj)
        return luaL_error(lua_state, "DJ runtime is not initialized");

    lua_pushinteger(lua_state, play_sound(runtime->dj, alias));
    return 1;
}

static int lua_dj_restart_sound(lua_State *lua_state) {
    const int channel = (int)luaL_checkinteger(lua_state, 1);

    ScriptRuntime *runtime = lua_runtime_instance(lua_state);
    if (!runtime || !runtime->dj)
        return luaL_error(lua_state, "DJ runtime is not initialized");

    restart_sound(runtime->dj, channel);
    return 0;
}

static int lua_dj_stop_sound(lua_State *lua_state) {
    const int channel = (int)luaL_checkinteger(lua_state, 1);

    ScriptRuntime *runtime = lua_runtime_instance(lua_state);
    if (!runtime || !runtime->dj)
        return luaL_error(lua_state, "DJ runtime is not initialized");

    stop_sound(runtime->dj, channel);
    return 0;
}

static int lua_dj_set_sound_channel_volume(lua_State *lua_state) {
    const int channel = (int)luaL_checkinteger(lua_state, 1);
    const float volume = (float)luaL_checknumber(lua_state, 2);

    ScriptRuntime *runtime = lua_runtime_instance(lua_state);
    if (!runtime || !runtime->dj)
        return luaL_error(lua_state, "DJ runtime is not initialized");

    set_sound_channel_volume(runtime->dj, channel, volume);
    return 0;
}

static int lua_dj_get_sound_channel_volume(lua_State *lua_state) {
    const int channel = (int)luaL_checkinteger(lua_state, 1);

    ScriptRuntime *runtime = lua_runtime_instance(lua_state);
    if (!runtime || !runtime->dj)
        return luaL_error(lua_state, "DJ runtime is not initialized");

    lua_pushnumber(lua_state, (lua_Number)get_sound_channel_volume(runtime->dj, channel));
    return 1;
}

static int lua_dj_load_music(lua_State *lua_state) {
    const char *path = luaL_checkstring(lua_state, 1);
    const char *alias = luaL_checkstring(lua_state, 2);
    const float default_volume = (float)luaL_optnumber(lua_state, 3, 1.0);

    ScriptRuntime *runtime = lua_runtime_instance(lua_state);
    if (!runtime || !runtime->dj)
        return luaL_error(lua_state, "DJ runtime is not initialized");

    char full_path[PATH_MAX] = {0};
    if (resolve_audio_path(lua_state, path, full_path, sizeof(full_path)) != Ok)
        return luaL_error(lua_state, "Failed to resolve DJ music path '%s'", path);

    load_music_source_as_with_volume(runtime->dj, full_path, alias, default_volume);
    return 0;
}

static int lua_dj_play_music(lua_State *lua_state) {
    const char *alias = luaL_checkstring(lua_state, 1);
    const boolean loop = lua_toboolean(lua_state, 2) ? True : False;

    ScriptRuntime *runtime = lua_runtime_instance(lua_state);
    if (!runtime || !runtime->dj)
        return luaL_error(lua_state, "DJ runtime is not initialized");

    lua_pushinteger(lua_state, play_music(runtime->dj, alias, loop));
    return 1;
}

static int lua_dj_restart_music(lua_State *lua_state) {
    const int channel = (int)luaL_checkinteger(lua_state, 1);

    ScriptRuntime *runtime = lua_runtime_instance(lua_state);
    if (!runtime || !runtime->dj)
        return luaL_error(lua_state, "DJ runtime is not initialized");

    restart_music(runtime->dj, channel);
    return 0;
}

static int lua_dj_stop_music(lua_State *lua_state) {
    const int channel = (int)luaL_checkinteger(lua_state, 1);

    ScriptRuntime *runtime = lua_runtime_instance(lua_state);
    if (!runtime || !runtime->dj)
        return luaL_error(lua_state, "DJ runtime is not initialized");

    stop_music(runtime->dj, channel);
    return 0;
}

static int lua_dj_set_music_channel_volume(lua_State *lua_state) {
    const int channel = (int)luaL_checkinteger(lua_state, 1);
    const float volume = (float)luaL_checknumber(lua_state, 2);

    ScriptRuntime *runtime = lua_runtime_instance(lua_state);
    if (!runtime || !runtime->dj)
        return luaL_error(lua_state, "DJ runtime is not initialized");

    set_music_channel_volume(runtime->dj, channel, volume);
    return 0;
}

static int lua_dj_get_music_channel_volume(lua_State *lua_state) {
    const int channel = (int)luaL_checkinteger(lua_state, 1);

    ScriptRuntime *runtime = lua_runtime_instance(lua_state);
    if (!runtime || !runtime->dj)
        return luaL_error(lua_state, "DJ runtime is not initialized");

    lua_pushnumber(lua_state, (lua_Number)get_music_channel_volume(runtime->dj, channel));
    return 1;
}

static const luaL_Reg dj_methods[] = {
    {"load_sound", lua_dj_load_sound},
    {"play_sound", lua_dj_play_sound},
    {"restart_sound", lua_dj_restart_sound},
    {"stop_sound", lua_dj_stop_sound},
    {"set_sound_channel_volume", lua_dj_set_sound_channel_volume},
    {"get_sound_channel_volume", lua_dj_get_sound_channel_volume},
    {"load_music", lua_dj_load_music},
    {"play_music", lua_dj_play_music},
    {"restart_music", lua_dj_restart_music},
    {"stop_music", lua_dj_stop_music},
    {"set_music_channel_volume", lua_dj_set_music_channel_volume},
    {"get_music_channel_volume", lua_dj_get_music_channel_volume},
    {NULL, NULL},
};

int luaopen_engine_dj(lua_State *lua_state) {
    lua_newtable(lua_state);
    luaL_setfuncs(lua_state, dj_methods, 0);
    return 1;
}
