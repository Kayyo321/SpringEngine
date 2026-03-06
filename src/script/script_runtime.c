#include "script_runtime_internal.h"

#include "vfs.h"

typedef struct {
    Heap heap;
    char module_path[PATH_MAX];
    char module_alias[128];
    int table_ref;
    boolean has_awake;
    boolean has_start;
    boolean has_update;
    boolean has_on_destroy;
    boolean initialized;
} ScriptComponentState;

static const char *RuntimeActorRegistryKey = "__springengine_runtime";
static const char *CurrentActorRegistryKey = "__springengine_current_actor";
static const char *ComponentCacheRegistryKey = "__springengine_component_cache";

result runtime_join_path(const char *base, const char *path, char *out_path, usize out_size) {
    return vfs_resolve_path(base, path, out_path, out_size);
}

Actor *lua_runtime_current_actor(lua_State *lua_state) {
    if (!lua_state)
        return Null;

    lua_getfield(lua_state, LUA_REGISTRYINDEX, CurrentActorRegistryKey);
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

    lua_setfield(lua_state, LUA_REGISTRYINDEX, CurrentActorRegistryKey);
}

ScriptRuntime *lua_runtime_instance(lua_State *lua_state) {
    if (!lua_state)
        return Null;

    lua_getfield(lua_state, LUA_REGISTRYINDEX, RuntimeActorRegistryKey);
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
            {
                if (strcmp(component->descriptor.name, "StaticColor") != 0)
                    continue;

                StaticColorState *color_state = (StaticColorState *)component->data;
                if (!color_state)
                    continue;

                anchor_position.x += color_state->position.x;
                anchor_position.y += color_state->position.y;
                break;
            }

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
    lua_setfield(runtime->lua_state, LUA_REGISTRYINDEX, RuntimeActorRegistryKey);

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
    if (register_preload_module(runtime->lua_state, "Engine.Collider", luaopen_engine_collider) != Ok)
        return Err;
    if (register_preload_module(runtime->lua_state, "Engine.Rigidbody", luaopen_engine_rigidbody) != Ok)
        return Err;
    if (register_preload_module(runtime->lua_state, "Engine.Scene", luaopen_engine_scene) != Ok)
        return Err;
    if (register_preload_module(runtime->lua_state, "Engine.DJ", luaopen_engine_dj) != Ok)
        return Err;
    if (register_preload_module(runtime->lua_state, "Engine.UI", luaopen_engine_ui) != Ok)
        return Err;
    if (register_preload_module(runtime->lua_state, "Engine.Disk", luaopen_engine_disk) != Ok)
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

static result lua_load_script_from_vfs(lua_State *lua_state, const char *script_path) {
    if (!lua_state || !script_path || script_path[0] == '\0')
        return Err;

    Heap script_heap = NullHeap;
    if (vfs_read_file_bytes(script_path, &script_heap) != Ok) {
        log_err("Failed to read Lua script '%s'", script_path);
        return Err;
    }

    char chunk_name[PATH_MAX + 2] = {0};
    if (snprintf(chunk_name, sizeof(chunk_name), "@%s", script_path) >= (int)sizeof(chunk_name)) {
        deallocate(script_heap);
        return Err;
    }

    const int load_result = luaL_loadbufferx(lua_state, (const char *)script_heap.pointer, script_heap.size, chunk_name, Null);
    deallocate(script_heap);
    return load_result == LUA_OK ? Ok : Err;
}

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

static ScriptComponentState *find_actor_script_component_state(Actor *actor, const char *module_path) {
    if (!actor || !module_path || module_path[0] == '\0')
        return Null;

    for (usize component_index = 0; component_index < actor->component_count; ++component_index) {
        ActorComponent *component = &actor->components[component_index];
        if (component->descriptor.kind != ComponentScript || !component->data)
            continue;

        ScriptComponentState *state = (ScriptComponentState *)component->data;
        if (strcmp(state->module_path, module_path) == 0)
            return state;

        if (state->module_alias[0] != '\0' && strcmp(state->module_alias, module_path) == 0)
            return state;
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

static ColliderComponentData *find_actor_collider(Actor *actor) {
    ActorComponent *component = find_builtin_component(actor, "Collider");
    if (!component)
        return Null;

    return (ColliderComponentData *)component->data;
}

static int lua_component_collider_set_offset(lua_State *lua_state) {
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

static int lua_component_collider_get_offset(lua_State *lua_state) {
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

static int lua_component_collider_set_size(lua_State *lua_state) {
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

static int lua_component_collider_get_size(lua_State *lua_state) {
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

static int lua_component_collider_set_enabled(lua_State *lua_state) {
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

static int lua_component_collider_get_enabled(lua_State *lua_state) {
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

static int lua_component_collider_set_is_trigger(lua_State *lua_state) {
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

static int lua_component_collider_get_is_trigger(lua_State *lua_state) {
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

static int lua_component_collider_get_bounds(lua_State *lua_state) {
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

static int lua_component_collider_overlaps_actor(lua_State *lua_state) {
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

static int lua_component_collider_overlaps_all(lua_State *lua_state) {
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

static int lua_component_collider_overlaps_point(lua_State *lua_state) {
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

static int lua_component_animconf_set(lua_State *lua_state) {
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

static int lua_component_animconf_get(lua_State *lua_state) {
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

static int lua_component_animconf_set_number(lua_State *lua_state) {
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

static int lua_component_animconf_get_number(lua_State *lua_state) {
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

static int lua_component_animconf_set_flip_x(lua_State *lua_state) {
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

static int lua_component_animconf_get_flip_x(lua_State *lua_state) {
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

static int lua_component_script_call(lua_State *lua_state) {
    const char *actor_id = lua_tostring(lua_state, lua_upvalueindex(1));
    const char *module_path = lua_tostring(lua_state, lua_upvalueindex(2));
    const int using_colon_call = lua_istable(lua_state, 1) ? 1 : 0;
    const int method_index = using_colon_call ? 2 : 1;
    const int first_arg_index = using_colon_call ? 3 : 2;
    const int original_arg_top = lua_gettop(lua_state);

    const char *method_name = luaL_checkstring(lua_state, method_index);

    Actor *actor = lua_runtime_find_actor(lua_state, actor_id ? actor_id : "");
    ScriptComponentState *state = find_actor_script_component_state(actor, module_path);
    if (!state || state->table_ref == LUA_NOREF) {
        lua_pushboolean(lua_state, 0);
        return 1;
    }

    lua_rawgeti(lua_state, LUA_REGISTRYINDEX, state->table_ref);
    if (!lua_istable(lua_state, -1)) {
        lua_pop(lua_state, 1);
        lua_pushboolean(lua_state, 0);
        return 1;
    }

    lua_getfield(lua_state, -1, method_name);
    if (!lua_isfunction(lua_state, -1)) {
        lua_pop(lua_state, 2);
        lua_pushboolean(lua_state, 0);
        return 1;
    }

    lua_pushvalue(lua_state, -2);

    int forwarded_arg_count = 0;
    for (int arg_index = first_arg_index; arg_index <= original_arg_top; ++arg_index) {
        lua_pushvalue(lua_state, arg_index);
        forwarded_arg_count++;
    }

    lua_runtime_set_current_actor(lua_state, actor);
    if (lua_pcall(lua_state, 1 + forwarded_arg_count, 0, 0) != LUA_OK) {
        const char *error_message = lua_tostring(lua_state, -1);
        log_err(
            "Lua script call '%s' failed for actor '%s' (module '%s'): %s",
            method_name,
            actor_id ? actor_id : "<unknown>",
            module_path ? module_path : "<unknown>",
            error_message ? error_message : "<unknown error>");
        lua_runtime_set_current_actor(lua_state, Null);
        lua_pop(lua_state, 2);
        lua_pushboolean(lua_state, 0);
        return 1;
    }
    lua_runtime_set_current_actor(lua_state, Null);

    lua_pop(lua_state, 1);
    lua_pushboolean(lua_state, 1);
    return 1;
}

static void lua_component_set_actor_method(lua_State *lua_state, const char *actor_id, const char *field_name, lua_CFunction method) {
    lua_pushstring(lua_state, actor_id ? actor_id : "");
    lua_pushcclosure(lua_state, method, 1);
    lua_setfield(lua_state, -2, field_name);
}

static result lua_runtime_push_component_cache(lua_State *lua_state) {
    if (!lua_state)
        return Err;

    lua_getfield(lua_state, LUA_REGISTRYINDEX, ComponentCacheRegistryKey);
    if (!lua_istable(lua_state, -1)) {
        lua_pop(lua_state, 1);
        lua_newtable(lua_state);
        lua_pushvalue(lua_state, -1);
        lua_setfield(lua_state, LUA_REGISTRYINDEX, ComponentCacheRegistryKey);
    }

    return Ok;
}

static boolean lua_runtime_fetch_cached_component(lua_State *lua_state, const char *actor_id, const char *component_name) {
    if (!lua_state || !actor_id || !component_name)
        return False;

    if (lua_runtime_push_component_cache(lua_state) != Ok)
        return False;

    lua_getfield(lua_state, -1, actor_id);
    if (!lua_istable(lua_state, -1)) {
        lua_pop(lua_state, 2);
        return False;
    }

    lua_getfield(lua_state, -1, component_name);
    if (lua_isnil(lua_state, -1)) {
        lua_pop(lua_state, 3);
        return False;
    }

    lua_remove(lua_state, -2);
    lua_remove(lua_state, -2);
    return True;
}

static result lua_runtime_store_cached_component(lua_State *lua_state, const char *actor_id, const char *component_name) {
    if (!lua_state || !actor_id || !component_name)
        return Err;

    if (lua_runtime_push_component_cache(lua_state) != Ok)
        return Err;

    lua_getfield(lua_state, -1, actor_id);
    if (!lua_istable(lua_state, -1)) {
        lua_pop(lua_state, 1);
        lua_newtable(lua_state);
        lua_pushvalue(lua_state, -1);
        lua_setfield(lua_state, -3, actor_id);
    }

    lua_pushvalue(lua_state, -3);
    lua_setfield(lua_state, -2, component_name);
    lua_pop(lua_state, 2);
    return Ok;
}

static result lua_script_self_build_animated_sprite_proxy(lua_State *lua_state, Actor *actor, const char *actor_id) {
    if (!lua_state) {
        lua_pushnil(lua_state);
        return Err;
    }

    if (!find_actor_animated_sprite(actor)) {
        lua_pushnil(lua_state);
        return Ok;
    }

    lua_newtable(lua_state);
    lua_component_set_actor_method(lua_state, actor_id, "set", lua_component_animconf_set);
    lua_component_set_actor_method(lua_state, actor_id, "get", lua_component_animconf_get);
    lua_component_set_actor_method(lua_state, actor_id, "set_number", lua_component_animconf_set_number);
    lua_component_set_actor_method(lua_state, actor_id, "get_number", lua_component_animconf_get_number);
    lua_component_set_actor_method(lua_state, actor_id, "set_flip_x", lua_component_animconf_set_flip_x);
    lua_component_set_actor_method(lua_state, actor_id, "get_flip_x", lua_component_animconf_get_flip_x);
    return Ok;
}

static result lua_script_self_build_collider_proxy(lua_State *lua_state, Actor *actor, const char *actor_id) {
    if (!lua_state) {
        lua_pushnil(lua_state);
        return Err;
    }

    if (!find_actor_collider(actor)) {
        lua_pushnil(lua_state);
        return Ok;
    }

    lua_newtable(lua_state);
    lua_component_set_actor_method(lua_state, actor_id, "set_offset", lua_component_collider_set_offset);
    lua_component_set_actor_method(lua_state, actor_id, "get_offset", lua_component_collider_get_offset);
    lua_component_set_actor_method(lua_state, actor_id, "set_size", lua_component_collider_set_size);
    lua_component_set_actor_method(lua_state, actor_id, "get_size", lua_component_collider_get_size);
    lua_component_set_actor_method(lua_state, actor_id, "set_enabled", lua_component_collider_set_enabled);
    lua_component_set_actor_method(lua_state, actor_id, "get_enabled", lua_component_collider_get_enabled);
    lua_component_set_actor_method(lua_state, actor_id, "set_is_trigger", lua_component_collider_set_is_trigger);
    lua_component_set_actor_method(lua_state, actor_id, "get_is_trigger", lua_component_collider_get_is_trigger);
    lua_component_set_actor_method(lua_state, actor_id, "get_bounds", lua_component_collider_get_bounds);
    lua_component_set_actor_method(lua_state, actor_id, "overlaps_actor", lua_component_collider_overlaps_actor);
    lua_component_set_actor_method(lua_state, actor_id, "overlaps_all", lua_component_collider_overlaps_all);
    lua_component_set_actor_method(lua_state, actor_id, "overlaps_point", lua_component_collider_overlaps_point);
    return Ok;
}

static result lua_script_self_push_actor_component(lua_State *lua_state, const char *actor_id, const char *component_name) {
    if (!lua_state || !actor_id || !component_name)
        return Err;

    luaL_requiref(lua_state, "Engine.Actor", luaopen_engine_actor, 1);
    lua_getfield(lua_state, -1, "get_component");
    lua_pushstring(lua_state, actor_id);
    lua_pushstring(lua_state, component_name);

    if (lua_pcall(lua_state, 2, 1, 0) != LUA_OK) {
        lua_pop(lua_state, 2);
        lua_pushnil(lua_state);
        return Err;
    }

    lua_remove(lua_state, -2);
    return Ok;
}

static result lua_script_self_build_script_proxy(lua_State *lua_state, Actor *actor, const char *actor_id, const char *module_path) {
    if (!lua_state) {
        lua_pushnil(lua_state);
        return Err;
    }

    ScriptComponentState *script_state = find_actor_script_component_state(actor, module_path);
    if (!script_state || script_state->table_ref == LUA_NOREF) {
        lua_pushnil(lua_state);
        return Ok;
    }

    lua_newtable(lua_state);
    lua_pushstring(lua_state, actor_id ? actor_id : "");
    lua_pushstring(lua_state, module_path ? module_path : "");
    lua_pushcclosure(lua_state, lua_component_script_call, 2);
    lua_setfield(lua_state, -2, "call");
    return Ok;
}

static result lua_script_self_build_component_proxy(lua_State *lua_state, Actor *actor, const char *actor_id, const char *component_name) {
    if (!lua_state || !actor || !actor_id || !component_name)
        return Err;

    if (strcmp(component_name, "AnimatedSprite") == 0)
        return lua_script_self_build_animated_sprite_proxy(lua_state, actor, actor_id);

    if (strcmp(component_name, "Collider") == 0)
        return lua_script_self_build_collider_proxy(lua_state, actor, actor_id);

    if (find_builtin_component(actor, component_name))
        return lua_script_self_push_actor_component(lua_state, actor_id, component_name);

    return lua_script_self_build_script_proxy(lua_state, actor, actor_id, component_name);
}

static void lua_script_self_cache_component_proxy(lua_State *lua_state, Actor *actor, const char *actor_id, const char *component_name) {
    if (!lua_state || !actor || !actor_id || !component_name)
        return;

    const char *resolved_component = resolve_component_name_alias(component_name);
    if (!resolved_component)
        return;

    if (lua_runtime_fetch_cached_component(lua_state, actor_id, resolved_component)) {
        lua_pop(lua_state, 1);
        return;
    }

    if (lua_script_self_build_component_proxy(lua_state, actor, actor_id, resolved_component) != Ok) {
        if (!lua_isnil(lua_state, -1))
            lua_pop(lua_state, 1);
        return;
    }

    if (lua_isnil(lua_state, -1)) {
        lua_pop(lua_state, 1);
        return;
    }

    if (lua_runtime_store_cached_component(lua_state, actor_id, resolved_component) != Ok) {
        lua_pop(lua_state, 1);
        return;
    }

    lua_pop(lua_state, 1);
}

static void lua_script_self_prime_actor_component_cache(lua_State *lua_state, Actor *actor, const char *actor_id) {
    if (!lua_state || !actor || !actor_id)
        return;

    for (usize component_index = 0; component_index < actor->component_count; ++component_index) {
        ActorComponent *component = &actor->components[component_index];
        if (component->descriptor.kind == ComponentBuiltin && component->descriptor.name) {
            lua_script_self_cache_component_proxy(lua_state, actor, actor_id, component->descriptor.name);
            continue;
        }

        if (component->descriptor.kind == ComponentScript && component->data) {
            ScriptComponentState *state = (ScriptComponentState *)component->data;
            if (state->module_path[0] != '\0')
                lua_script_self_cache_component_proxy(lua_state, actor, actor_id, state->module_path);
            if (state->module_alias[0] != '\0')
                lua_script_self_cache_component_proxy(lua_state, actor, actor_id, state->module_alias);
        }
    }
}

static int lua_script_self_get_component(lua_State *lua_state) {
    const char *default_actor_id = lua_tostring(lua_state, lua_upvalueindex(1));
    const int using_colon_call = lua_istable(lua_state, 1) ? 1 : 0;
    const int component_index = using_colon_call ? 2 : 1;
    const int actor_index = using_colon_call ? 3 : 2;

    const char *requested_component = luaL_checkstring(lua_state, component_index);
    const char *resolved_component = resolve_component_name_alias(requested_component);
    if (!resolved_component) {
        lua_pushnil(lua_state);
        return 1;
    }

    const char *target_actor_id = default_actor_id;
    if (lua_gettop(lua_state) >= actor_index && !lua_isnil(lua_state, actor_index))
        target_actor_id = luaL_checkstring(lua_state, actor_index);

    Actor *actor = lua_runtime_find_actor(lua_state, target_actor_id ? target_actor_id : "");
    if (!actor) {
        lua_pushnil(lua_state);
        return 1;
    }

    const char *cache_actor_id = target_actor_id ? target_actor_id : "";
    if (lua_runtime_fetch_cached_component(lua_state, cache_actor_id, resolved_component))
        return 1;

    if (lua_script_self_build_component_proxy(lua_state, actor, cache_actor_id, resolved_component) != Ok)
        return 1;

    if (lua_isnil(lua_state, -1))
        return 1;

    if (lua_runtime_store_cached_component(lua_state, cache_actor_id, resolved_component) != Ok)
        return 1;

    return 1;
}

static int lua_script_self_destroy(lua_State *lua_state) {
    const char *default_actor_id = lua_tostring(lua_state, lua_upvalueindex(1));
    const int using_colon_call = lua_istable(lua_state, 1) ? 1 : 0;
    const int actor_index = using_colon_call ? 2 : 1;

    const char *target_actor_id = default_actor_id;
    if (lua_gettop(lua_state) >= actor_index && !lua_isnil(lua_state, actor_index))
        target_actor_id = luaL_checkstring(lua_state, actor_index);

    lua_pushboolean(lua_state, runtime_destroy_actor(target_actor_id ? target_actor_id : "") == Ok ? 1 : 0);
    return 1;
}

static int lua_script_material_set(lua_State *lua_state) {
    const char *material_alias = lua_tostring(lua_state, lua_upvalueindex(1));
    const int using_colon_call = lua_istable(lua_state, 1) ? 1 : 0;
    const int property_index = using_colon_call ? 2 : 1;
    const int value_index = using_colon_call ? 3 : 2;

    const char *property_name = luaL_checkstring(lua_state, property_index);
    const float value = (float)luaL_checknumber(lua_state, value_index);

    lua_pushboolean(lua_state, runtime_material_set_property(material_alias ? material_alias : "", property_name, value) == Ok ? 1 : 0);
    return 1;
}

static int lua_script_self_get_material_by_name(lua_State *lua_state) {
    const int using_colon_call = lua_istable(lua_state, 1) ? 1 : 0;
    const int alias_index = using_colon_call ? 2 : 1;
    const char *material_alias = luaL_checkstring(lua_state, alias_index);

    if (!runtime_material_alias_exists(material_alias)) {
        lua_pushnil(lua_state);
        return 1;
    }

    lua_newtable(lua_state);
    lua_pushstring(lua_state, material_alias);
    lua_pushcclosure(lua_state, lua_script_material_set, 1);
    lua_setfield(lua_state, -2, "set");
    return 1;
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

void *script_component_state_create(const char *module_path, const char *module_alias) {
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

    if (module_alias && module_alias[0] != '\0') {
        if (snprintf(state->module_alias, sizeof(state->module_alias), "%s", module_alias) >= (int)sizeof(state->module_alias)) {
            log_err("Script module alias is too long: '%s'", module_alias);
            deallocate(state->heap);
            return Null;
        }
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

    if (lua_load_script_from_vfs(lua_state, script_path) != Ok) {
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

    lua_pushstring(lua_state, actor->id ? actor->id : "");
    lua_pushcclosure(lua_state, lua_script_self_get_component, 1);
    lua_setfield(lua_state, -2, "get_component");

    lua_pushstring(lua_state, actor->id ? actor->id : "");
    lua_pushcclosure(lua_state, lua_script_self_destroy, 1);
    lua_setfield(lua_state, -2, "destroy");

    lua_pushstring(lua_state, actor->id ? actor->id : "");
    lua_pushcclosure(lua_state, lua_script_self_get_material_by_name, 1);
    lua_setfield(lua_state, -2, "get_material_by_name");

    state->has_awake = table_has_function(lua_state, -1, "awake");
    state->has_start = table_has_function(lua_state, -1, "start");
    state->has_update = table_has_function(lua_state, -1, "update");
    state->has_on_destroy = table_has_function(lua_state, -1, "on_destroy");

    state->table_ref = luaL_ref(lua_state, LUA_REGISTRYINDEX);
    state->initialized = True;

    lua_script_self_prime_actor_component_cache(lua_state, actor, actor->id ? actor->id : "");

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

result script_runtime_invoke_ui_callback(ScriptRuntime *runtime, const char *callback_ref, const char *document_id, const char *node_id) {
    if (!runtime || !runtime->lua_state || !callback_ref || callback_ref[0] == '\0')
        return Err;

    const char *separator = strchr(callback_ref, ':');
    if (!separator || separator == callback_ref || separator[1] == '\0') {
        log_err("UI callback '%s' must use format 'path.lua:function_name'", callback_ref);
        return Err;
    }

    char module_path[PATH_MAX] = {0};
    const usize module_path_length = (usize)(separator - callback_ref);
    if (module_path_length + 1 > sizeof(module_path)) {
        log_err("UI callback module path is too long: '%s'", callback_ref);
        return Err;
    }

    memcpy(module_path, callback_ref, module_path_length);
    module_path[module_path_length] = '\0';

    const char *function_name = separator + 1;

    char resolved_script_path[PATH_MAX] = {0};
    if (runtime_join_path(runtime->project_root, module_path, resolved_script_path, sizeof(resolved_script_path)) != Ok) {
        log_err("Failed to resolve UI callback script path '%s'", module_path);
        return Err;
    }

    lua_State *lua_state = runtime->lua_state;

    if (lua_load_script_from_vfs(lua_state, resolved_script_path) != Ok) {
        const char *error_message = lua_tostring(lua_state, -1);
        log_err("Failed to load UI callback script '%s': %s", resolved_script_path, error_message ? error_message : "<unknown error>");
        lua_pop(lua_state, 1);
        return Err;
    }

    if (lua_pcall(lua_state, 0, 1, 0) != LUA_OK) {
        const char *error_message = lua_tostring(lua_state, -1);
        log_err("Failed to execute UI callback script '%s': %s", resolved_script_path, error_message ? error_message : "<unknown error>");
        lua_pop(lua_state, 1);
        return Err;
    }

    if (!lua_istable(lua_state, -1)) {
        log_err("UI callback script '%s' must return a table", resolved_script_path);
        lua_pop(lua_state, 1);
        return Err;
    }

    lua_getfield(lua_state, -1, function_name);
    if (!lua_isfunction(lua_state, -1)) {
        log_err("UI callback '%s' was not found in '%s'", function_name, resolved_script_path);
        lua_pop(lua_state, 2);
        return Err;
    }

    lua_pushvalue(lua_state, -2);
    lua_pushstring(lua_state, document_id ? document_id : "");
    lua_pushstring(lua_state, node_id ? node_id : "");

    if (lua_pcall(lua_state, 3, 0, 0) != LUA_OK) {
        const char *error_message = lua_tostring(lua_state, -1);
        log_err(
            "UI callback '%s' failed (document='%s', node='%s'): %s",
            callback_ref,
            document_id ? document_id : "",
            node_id ? node_id : "",
            error_message ? error_message : "<unknown error>");
        lua_pop(lua_state, 2);
        return Err;
    }

    lua_pop(lua_state, 1);
    return Ok;
}
