#include "script_runtime.h"

#include "common.h"

#include "lauxlib.h"
#include "lualib.h"

#include <stdio.h>
#include <string.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

typedef struct {
    Heap heap;
    char module_path[PATH_MAX];
    int table_ref;
    boolean has_awake;
    boolean has_start;
    boolean has_update;
    boolean has_on_destroy;
    boolean initialized;
} ScriptComponentState;

static const char *DJ_REGISTRY_KEY = "__springengine_dj";
static const char *PROJECT_ROOT_REGISTRY_KEY = "__springengine_project_root";

static result runtime_join_path(const char *base, const char *path, char *out_path, usize out_size) {
    if (!base || !path || !out_path || out_size == 0)
        return Err;

    if (path[0] == '/') {
        if (snprintf(out_path, out_size, "%s", path) >= (int)out_size)
            return Err;

        return Ok;
    }

    if (snprintf(out_path, out_size, "%s/%s", base, path) >= (int)out_size)
        return Err;

    return Ok;
}

static DJ *lua_runtime_dj(lua_State *lua_state) {
    if (!lua_state)
        return Null;

    lua_getfield(lua_state, LUA_REGISTRYINDEX, DJ_REGISTRY_KEY);
    DJ *dj = (DJ *)lua_touserdata(lua_state, -1);
    lua_pop(lua_state, 1);
    return dj;
}

static const char *lua_runtime_project_root(lua_State *lua_state) {
    if (!lua_state)
        return Null;

    lua_getfield(lua_state, LUA_REGISTRYINDEX, PROJECT_ROOT_REGISTRY_KEY);
    const char *project_root = lua_tostring(lua_state, -1);
    lua_pop(lua_state, 1);
    return project_root;
}

static result resolve_audio_path(lua_State *lua_state, const char *path, char *out_path, usize out_size) {
    if (!lua_state || !path || !out_path || out_size == 0)
        return Err;

    const char *project_root = lua_runtime_project_root(lua_state);
    if (!project_root)
        return Err;

    return runtime_join_path(project_root, path, out_path, out_size);
}

static int lua_dj_load_sound(lua_State *lua_state) {
    const char *path = luaL_checkstring(lua_state, 1);
    const char *alias = luaL_checkstring(lua_state, 2);

    DJ *dj = lua_runtime_dj(lua_state);
    if (!dj)
        return luaL_error(lua_state, "DJ runtime is not initialized");

    char full_path[PATH_MAX] = {0};
    if (resolve_audio_path(lua_state, path, full_path, sizeof(full_path)) != Ok)
        return luaL_error(lua_state, "Failed to resolve DJ sound path '%s'", path);

    load_sound_source_as(dj, full_path, alias);
    return 0;
}

static int lua_dj_play_sound(lua_State *lua_state) {
    const char *alias = luaL_checkstring(lua_state, 1);

    DJ *dj = lua_runtime_dj(lua_state);
    if (!dj)
        return luaL_error(lua_state, "DJ runtime is not initialized");

    lua_pushinteger(lua_state, play_sound(dj, alias));
    return 1;
}

static int lua_dj_restart_sound(lua_State *lua_state) {
    const int channel = (int)luaL_checkinteger(lua_state, 1);

    DJ *dj = lua_runtime_dj(lua_state);
    if (!dj)
        return luaL_error(lua_state, "DJ runtime is not initialized");

    restart_sound(dj, channel);
    return 0;
}

static int lua_dj_stop_sound(lua_State *lua_state) {
    const int channel = (int)luaL_checkinteger(lua_state, 1);

    DJ *dj = lua_runtime_dj(lua_state);
    if (!dj)
        return luaL_error(lua_state, "DJ runtime is not initialized");

    stop_sound(dj, channel);
    return 0;
}

static int lua_dj_load_music(lua_State *lua_state) {
    const char *path = luaL_checkstring(lua_state, 1);
    const char *alias = luaL_checkstring(lua_state, 2);

    DJ *dj = lua_runtime_dj(lua_state);
    if (!dj)
        return luaL_error(lua_state, "DJ runtime is not initialized");

    char full_path[PATH_MAX] = {0};
    if (resolve_audio_path(lua_state, path, full_path, sizeof(full_path)) != Ok)
        return luaL_error(lua_state, "Failed to resolve DJ music path '%s'", path);

    load_music_source_as(dj, full_path, alias);
    return 0;
}

static int lua_dj_play_music(lua_State *lua_state) {
    const char *alias = luaL_checkstring(lua_state, 1);
    const boolean loop = lua_toboolean(lua_state, 2) ? True : False;

    DJ *dj = lua_runtime_dj(lua_state);
    if (!dj)
        return luaL_error(lua_state, "DJ runtime is not initialized");

    lua_pushinteger(lua_state, play_music(dj, alias, loop));
    return 1;
}

static int lua_dj_restart_music(lua_State *lua_state) {
    const int channel = (int)luaL_checkinteger(lua_state, 1);

    DJ *dj = lua_runtime_dj(lua_state);
    if (!dj)
        return luaL_error(lua_state, "DJ runtime is not initialized");

    restart_music(dj, channel);
    return 0;
}

static int lua_dj_stop_music(lua_State *lua_state) {
    const int channel = (int)luaL_checkinteger(lua_state, 1);

    DJ *dj = lua_runtime_dj(lua_state);
    if (!dj)
        return luaL_error(lua_state, "DJ runtime is not initialized");

    stop_music(dj, channel);
    return 0;
}

static result register_dj_library(ScriptRuntime *runtime) {
    if (!runtime || !runtime->lua_state || !runtime->dj)
        return Ok;

    static const luaL_Reg dj_methods[] = {
        {"load_sound", lua_dj_load_sound},
        {"play_sound", lua_dj_play_sound},
        {"restart_sound", lua_dj_restart_sound},
        {"stop_sound", lua_dj_stop_sound},
        {"load_music", lua_dj_load_music},
        {"play_music", lua_dj_play_music},
        {"restart_music", lua_dj_restart_music},
        {"stop_music", lua_dj_stop_music},
        {NULL, NULL},
    };

    lua_pushlightuserdata(runtime->lua_state, runtime->dj);
    lua_setfield(runtime->lua_state, LUA_REGISTRYINDEX, DJ_REGISTRY_KEY);

    lua_pushstring(runtime->lua_state, runtime->project_root);
    lua_setfield(runtime->lua_state, LUA_REGISTRYINDEX, PROJECT_ROOT_REGISTRY_KEY);

    lua_newtable(runtime->lua_state);
    luaL_setfuncs(runtime->lua_state, dj_methods, 0);
    lua_setglobal(runtime->lua_state, "DJ");

    return Ok;
}

static boolean table_has_function(lua_State *lua_state, int table_index, const char *field_name) {
    lua_getfield(lua_state, table_index, field_name);
    const boolean has_function = lua_isfunction(lua_state, -1) ? True : False;
    lua_pop(lua_state, 1);
    return has_function;
}

static result call_script_method(lua_State *lua_state, int table_ref, const char *method_name, const char *actor_id, const char *module_path) {
    lua_rawgeti(lua_state, LUA_REGISTRYINDEX, table_ref);
    if (!lua_istable(lua_state, -1)) {
        log_err("Script state for module '%s' is not a table", module_path ? module_path : "<unknown>");
        lua_pop(lua_state, 1);
        return Err;
    }

    lua_getfield(lua_state, -1, method_name);
    if (!lua_isfunction(lua_state, -1)) {
        lua_pop(lua_state, 2);
        return Ok;
    }

    lua_pushvalue(lua_state, -2);

    if (lua_pcall(lua_state, 1, 0, 0) != LUA_OK) {
        const char *error_message = lua_tostring(lua_state, -1);
        log_err(
            "Lua script method '%s' failed for actor '%s' (module '%s'): %s",
            method_name,
            actor_id ? actor_id : "<unknown>",
            module_path ? module_path : "<unknown>",
            error_message ? error_message : "<unknown error>");
        lua_pop(lua_state, 2);
        return Err;
    }

    lua_pop(lua_state, 1);
    return Ok;
}

result script_runtime_init(ScriptRuntime *runtime, const char *project_root, DJ *dj) {
    if (!runtime || !project_root)
        return Err;

    memset(runtime, 0, sizeof(*runtime));

    if (snprintf(runtime->project_root, sizeof(runtime->project_root), "%s", project_root) >= (int)sizeof(runtime->project_root)) {
        log_err("Project path too long for script runtime: '%s'", project_root);
        return Err;
    }

    runtime->dj = dj;

    runtime->lua_state = luaL_newstate();
    if (!runtime->lua_state) {
        log_err("Failed to create Lua state");
        return Err;
    }

    luaL_openlibs(runtime->lua_state);

    if (register_dj_library(runtime) != Ok) {
        lua_close(runtime->lua_state);
        runtime->lua_state = Null;
        return Err;
    }

    return Ok;
}

void script_runtime_dispose(ScriptRuntime *runtime) {
    if (!runtime)
        return;

    if (runtime->lua_state)
        lua_close(runtime->lua_state);

    memset(runtime, 0, sizeof(*runtime));
}

void *script_component_state_create(const char *module_path) {
    if (!module_path || module_path[0] == '\0')
        return Null;

    Heap state_heap = allocate(1, sizeof(ScriptComponentState));
    ScriptComponentState *state = (ScriptComponentState *)state_heap.pointer;
    if (!state)
        return Null;

    memset(state, 0, sizeof(*state));
    state->heap = state_heap;

    if (snprintf(state->module_path, sizeof(state->module_path), "%s", module_path) >= (int)sizeof(state->module_path)) {
        log_err("Script module path is too long: '%s'", module_path);
        deallocate(state->heap);
        return Null;
    }

    state->table_ref = LUA_NOREF;
    return state;
}

void script_component_state_dispose(void *state_ptr) {
    if (!state_ptr)
        return;

    ScriptComponentState *state = (ScriptComponentState *)state_ptr;
    if (state->heap.pointer)
        deallocate(state->heap);
}

result script_component_initialize(Actor *actor, ActorComponent *component, void *context) {
    if (!actor || !component || !context || !component->data)
        return Err;

    ScriptRuntime *runtime = (ScriptRuntime *)context;
    ScriptComponentState *state = (ScriptComponentState *)component->data;

    if (!runtime->lua_state) {
        log_err("Lua runtime is not initialized for actor '%s'", actor->id ? actor->id : "<unknown>");
        return Err;
    }

    char script_path[PATH_MAX] = {0};
    if (runtime_join_path(runtime->project_root, state->module_path, script_path, sizeof(script_path)) != Ok) {
        log_err("Failed to resolve script path '%s'", state->module_path);
        return Err;
    }

    lua_State *lua_state = runtime->lua_state;

    if (luaL_loadfile(lua_state, script_path) != LUA_OK) {
        const char *error_message = lua_tostring(lua_state, -1);
        log_err("Failed to load Lua script '%s': %s", script_path, error_message ? error_message : "<unknown error>");
        lua_pop(lua_state, 1);
        return Err;
    }

    if (lua_pcall(lua_state, 0, 1, 0) != LUA_OK) {
        const char *error_message = lua_tostring(lua_state, -1);
        log_err("Failed to execute Lua script '%s': %s", script_path, error_message ? error_message : "<unknown error>");
        lua_pop(lua_state, 1);
        return Err;
    }

    if (!lua_istable(lua_state, -1)) {
        log_err("Lua script '%s' must return a table with lifecycle functions", script_path);
        lua_pop(lua_state, 1);
        return Err;
    }

    state->has_awake = table_has_function(lua_state, -1, "awake");
    state->has_start = table_has_function(lua_state, -1, "start");
    state->has_update = table_has_function(lua_state, -1, "update");
    state->has_on_destroy = table_has_function(lua_state, -1, "on_destroy");

    state->table_ref = luaL_ref(lua_state, LUA_REGISTRYINDEX);
    state->initialized = True;

    if (!state->has_awake && !state->has_start && !state->has_update && !state->has_on_destroy) {
        log_warn("Lua script '%s' has no lifecycle functions (awake/start/update/on_destroy)", state->module_path);
    }

    if (state->has_awake && call_script_method(lua_state, state->table_ref, "awake", actor->id, state->module_path) != Ok)
        return Err;

    if (state->has_start && call_script_method(lua_state, state->table_ref, "start", actor->id, state->module_path) != Ok)
        return Err;

    log_msg("Loaded script component '%s' on actor '%s'", state->module_path, actor->id ? actor->id : "<unknown>");
    return Ok;
}

void script_component_update(Actor *actor, ActorComponent *component, ScriptRuntime *runtime) {
    if (!actor || !component || !runtime || !runtime->lua_state || !component->data)
        return;

    ScriptComponentState *state = (ScriptComponentState *)component->data;
    if (!state->initialized || !state->has_update || state->table_ref == LUA_NOREF)
        return;

    if (call_script_method(runtime->lua_state, state->table_ref, "update", actor->id, state->module_path) != Ok)
        state->has_update = False;
}

void script_component_destroy(Actor *actor, ActorComponent *component, ScriptRuntime *runtime) {
    if (!component || !component->data)
        return;

    ScriptComponentState *state = (ScriptComponentState *)component->data;

    if (runtime && runtime->lua_state && state->table_ref != LUA_NOREF) {
        if (state->has_on_destroy)
            (void)call_script_method(runtime->lua_state, state->table_ref, "on_destroy", actor ? actor->id : "<unknown>", state->module_path);

        luaL_unref(runtime->lua_state, LUA_REGISTRYINDEX, state->table_ref);
        state->table_ref = LUA_NOREF;
    }

    script_component_state_dispose(state);
    component->data = Null;
}
