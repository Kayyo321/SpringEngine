#include "script_runtime_internal.h"

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

static const char *RUNTIME_REGISTRY_KEY = "__springengine_runtime";
static const char *CURRENT_ACTOR_REGISTRY_KEY = "__springengine_current_actor";

result runtime_join_path(const char *base, const char *path, char *out_path, usize out_size) {
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

Actor *lua_runtime_current_actor(lua_State *lua_state) {
    if (!lua_state)
        return Null;

    lua_getfield(lua_state, LUA_REGISTRYINDEX, CURRENT_ACTOR_REGISTRY_KEY);
    Actor *actor = (Actor *)lua_touserdata(lua_state, -1);
    lua_pop(lua_state, 1);
    return actor;
}

void lua_runtime_set_current_actor(lua_State *lua_state, Actor *actor) {
    if (!lua_state)
        return;

    if (actor)
        lua_pushlightuserdata(lua_state, actor);
    else
        lua_pushnil(lua_state);

    lua_setfield(lua_state, LUA_REGISTRYINDEX, CURRENT_ACTOR_REGISTRY_KEY);
}

ScriptRuntime *lua_runtime_instance(lua_State *lua_state) {
    if (!lua_state)
        return Null;

    lua_getfield(lua_state, LUA_REGISTRYINDEX, RUNTIME_REGISTRY_KEY);
    ScriptRuntime *runtime = (ScriptRuntime *)lua_touserdata(lua_state, -1);
    lua_pop(lua_state, 1);
    return runtime;
}

Actor *lua_runtime_find_actor(lua_State *lua_state, const char *actor_id) {
    if (!actor_id || actor_id[0] == '\0')
        return Null;

    ScriptRuntime *runtime = lua_runtime_instance(lua_state);
    if (!runtime || !runtime->actor_registry)
        return Null;

    for (usize index = 0; index < runtime->actor_registry->actor_count; ++index) {
        Actor *actor = &runtime->actor_registry->actors[index];
        if (actor->id && strcmp(actor->id, actor_id) == 0)
            return actor;
    }

    return Null;
}

Vector3 actor_visual_anchor_position(Actor *actor) {
    Vector3 anchor_position = {0.0f, 0.0f, 0.0f};
    if (!actor)
        return anchor_position;

    anchor_position.x = actor->transform.position.x;
    anchor_position.y = actor->transform.position.y;
    anchor_position.z = actor->transform.position.z;

    for (usize component_index = 0; component_index < actor->component_count; ++component_index) {
        ActorComponent *component = &actor->components[component_index];
        if (component->descriptor.kind != ComponentBuiltin || !component->descriptor.name)
            continue;

        if (strcmp(component->descriptor.name, "StaticSprite") != 0)
            continue;

        StaticSpriteState *sprite_state = (StaticSpriteState *)component->data;
        if (!sprite_state)
            continue;

        anchor_position.x += sprite_state->position.x;
        anchor_position.y += sprite_state->position.y;
        break;
    }

    return anchor_position;
}

Vector3 actor_world_position(Actor *actor) {
    Vector3 position = {0.0f, 0.0f, 0.0f};
    if (!actor)
        return position;

    position.x = actor->transform.position.x;
    position.y = actor->transform.position.y;
    position.z = actor->transform.position.z;
    return position;
}

static result register_preload_module(lua_State *lua_state, const char *module_name, lua_CFunction open_fn) {
    if (!lua_state || !module_name || !open_fn)
        return Err;

    lua_getglobal(lua_state, "package");
    if (!lua_istable(lua_state, -1)) {
        lua_pop(lua_state, 1);
        return Err;
    }

    lua_getfield(lua_state, -1, "preload");
    if (!lua_istable(lua_state, -1)) {
        lua_pop(lua_state, 2);
        return Err;
    }

    lua_pushcfunction(lua_state, open_fn);
    lua_setfield(lua_state, -2, module_name);

    lua_pop(lua_state, 2);
    return Ok;
}

static result register_engine_libraries(ScriptRuntime *runtime) {
    if (!runtime || !runtime->lua_state)
        return Err;

    lua_pushlightuserdata(runtime->lua_state, runtime);
    lua_setfield(runtime->lua_state, LUA_REGISTRYINDEX, RUNTIME_REGISTRY_KEY);

    if (register_preload_module(runtime->lua_state, "Engine.Input", luaopen_engine_input) != Ok)
        return Err;
    if (register_preload_module(runtime->lua_state, "Engine.Time", luaopen_engine_time) != Ok)
        return Err;
    if (register_preload_module(runtime->lua_state, "Engine.Transform", luaopen_engine_transform) != Ok)
        return Err;
    if (register_preload_module(runtime->lua_state, "Engine.Actor", luaopen_engine_actor) != Ok)
        return Err;
    if (register_preload_module(runtime->lua_state, "Engine.Camera", luaopen_engine_camera) != Ok)
        return Err;
    if (register_preload_module(runtime->lua_state, "Engine.Scene", luaopen_engine_scene) != Ok)
        return Err;
    if (register_preload_module(runtime->lua_state, "Engine.DJ", luaopen_engine_dj) != Ok)
        return Err;
    if (register_preload_module(runtime->lua_state, "Engine", luaopen_engine) != Ok)
        return Err;

    return Ok;
}

static boolean table_has_function(lua_State *lua_state, int table_index, const char *field_name) {
    lua_getfield(lua_state, table_index, field_name);
    const boolean has_function = lua_isfunction(lua_state, -1) ? True : False;
    lua_pop(lua_state, 1);
    return has_function;
}

static result call_script_method(lua_State *lua_state, int table_ref, const char *method_name, Actor *actor, const char *module_path) {
    const char *actor_id = actor && actor->id ? actor->id : "<unknown>";

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

    lua_runtime_set_current_actor(lua_state, actor);

    lua_pushvalue(lua_state, -2);

    if (lua_pcall(lua_state, 1, 0, 0) != LUA_OK) {
        const char *error_message = lua_tostring(lua_state, -1);
        log_err(
            "Lua script method '%s' failed for actor '%s' (module '%s'): %s",
            method_name,
            actor_id ? actor_id : "<unknown>",
            module_path ? module_path : "<unknown>",
            error_message ? error_message : "<unknown error>");
        lua_runtime_set_current_actor(lua_state, Null);
        lua_pop(lua_state, 2);
        return Err;
    }

    lua_runtime_set_current_actor(lua_state, Null);
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
    runtime->actor_registry = Null;
    runtime->current_actor = Null;
    runtime->time_raw_delta_time = 1.0f / 60.0f;
    runtime->time_delta_time = 1.0f / 60.0f;
    runtime->time_unscaled_elapsed_time = 0.0f;
    runtime->time_elapsed_time = 0.0f;
    runtime->time_scale = 1.0f;
    runtime->time_max_delta_time = 0.25f;
    runtime->time_fixed_delta_time = 0.0f;
    runtime->time_frame_count = 0;
    runtime->time_paused = False;

    if (load_input_config(project_root) != Ok)
        return Err;

    runtime->lua_state = luaL_newstate();
    if (!runtime->lua_state) {
        log_err("Failed to create Lua state");
        return Err;
    }

    luaL_openlibs(runtime->lua_state);

    if (register_engine_libraries(runtime) != Ok) {
        lua_close(runtime->lua_state);
        runtime->lua_state = Null;
        return Err;
    }

    return Ok;
}

void script_runtime_begin_frame(ScriptRuntime *runtime) {
    if (!runtime)
        return;

    float raw_delta_time = (float)GetFrameTime();
    if (raw_delta_time < 0.0f)
        raw_delta_time = 0.0f;

    runtime->time_raw_delta_time = raw_delta_time;
    runtime->time_delta_time = runtime_time_apply_rules(runtime, raw_delta_time);
    runtime->time_unscaled_elapsed_time += runtime->time_raw_delta_time;
    runtime->time_elapsed_time += runtime->time_delta_time;
    runtime->time_frame_count++;
}

void script_runtime_bind_registry(ScriptRuntime *runtime, ActorRegistry *actor_registry) {
    if (!runtime)
        return;

    runtime->actor_registry = actor_registry;
}

void script_runtime_dispose(ScriptRuntime *runtime) {
    if (!runtime)
        return;

    if (runtime->lua_state)
        lua_close(runtime->lua_state);

    reset_input_config();

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

    if (state->has_awake && call_script_method(lua_state, state->table_ref, "awake", actor, state->module_path) != Ok)
        return Err;

    if (state->has_start && call_script_method(lua_state, state->table_ref, "start", actor, state->module_path) != Ok)
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

    if (call_script_method(runtime->lua_state, state->table_ref, "update", actor, state->module_path) != Ok)
        state->has_update = False;
}

void script_component_destroy(Actor *actor, ActorComponent *component, ScriptRuntime *runtime) {
    if (!component || !component->data)
        return;

    ScriptComponentState *state = (ScriptComponentState *)component->data;

    if (runtime && runtime->lua_state && state->table_ref != LUA_NOREF) {
        if (state->has_on_destroy)
            (void)call_script_method(runtime->lua_state, state->table_ref, "on_destroy", actor, state->module_path);

        luaL_unref(runtime->lua_state, LUA_REGISTRYINDEX, state->table_ref);
        state->table_ref = LUA_NOREF;
    }

    script_component_state_dispose(state);
    component->data = Null;
}