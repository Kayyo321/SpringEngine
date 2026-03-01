#include "script_runtime_internal.h"

static int lua_scene_actor_destroy(lua_State *lua_state) {
    const char *actor_id = lua_tostring(lua_state, lua_upvalueindex(1));
    lua_pushboolean(lua_state, runtime_destroy_actor(actor_id) == Ok ? 1 : 0);
    return 1;
}

static boolean scene_actor_overlaps_caller(lua_State *lua_state, Actor *candidate) {
    if (!lua_state || !candidate)
        return False;

    Actor *caller = lua_runtime_current_actor(lua_state);
    if (!caller || caller == candidate)
        return False;

    ColliderComponentData *caller_collider = actor_find_collider_component(caller);
    ColliderComponentData *candidate_collider = actor_find_collider_component(candidate);
    if (!caller_collider || !candidate_collider)
        return False;

    return collider_components_overlap(caller, caller_collider, candidate, candidate_collider);
}

static void lua_scene_push_actor_table(lua_State *lua_state, Actor *actor) {
    if (!lua_state || !actor) {
        lua_pushnil(lua_state);
        return;
    }

    lua_newtable(lua_state);

    lua_pushstring(lua_state, actor->id ? actor->id : "");
    lua_setfield(lua_state, -2, "id");

    lua_pushboolean(lua_state, actor->enabled ? 1 : 0);
    lua_setfield(lua_state, -2, "enabled");

    lua_pushinteger(lua_state, actor->layer);
    lua_setfield(lua_state, -2, "layer");

    lua_newtable(lua_state);
    lua_pushnumber(lua_state, actor->transform.position.x);
    lua_setfield(lua_state, -2, "x");
    lua_pushnumber(lua_state, actor->transform.position.y);
    lua_setfield(lua_state, -2, "y");
    lua_pushnumber(lua_state, actor->transform.position.z);
    lua_setfield(lua_state, -2, "z");
    lua_setfield(lua_state, -2, "position");

    lua_newtable(lua_state);
    for (usize tag_index = 0; tag_index < actor->tag_count; ++tag_index) {
        lua_pushstring(lua_state, actor->tags[tag_index]);
        lua_rawseti(lua_state, -2, (int)tag_index + 1);
    }
    lua_setfield(lua_state, -2, "tags");

    lua_pushstring(lua_state, actor->id ? actor->id : "");
    lua_pushcclosure(lua_state, lua_scene_actor_destroy, 1);
    lua_setfield(lua_state, -2, "destroy");
}

static int lua_scene_find_by_id(lua_State *lua_state) {
    const char *actor_id = luaL_checkstring(lua_state, 1);
    Actor *actor = lua_runtime_find_actor(lua_state, actor_id);
    if (!actor) {
        lua_pushnil(lua_state);
        return 1;
    }

    lua_scene_push_actor_table(lua_state, actor);
    return 1;
}

static int lua_scene_find_first_by_layer(lua_State *lua_state) {
    const int layer = (int)luaL_checkinteger(lua_state, 1);
    const boolean include_disabled = lua_toboolean(lua_state, 2) ? True : False;

    ScriptRuntime *runtime = lua_runtime_instance(lua_state);
    if (!runtime || !runtime->actor_registry) {
        lua_pushnil(lua_state);
        return 1;
    }

    for (usize index = 0; index < runtime->actor_registry->actor_count; ++index) {
        Actor *actor = &runtime->actor_registry->actors[index];
        if (actor->layer != layer)
            continue;

        if (!include_disabled && !actor->enabled)
            continue;

        lua_scene_push_actor_table(lua_state, actor);
        return 1;
    }

    lua_pushnil(lua_state);
    return 1;
}

static int lua_scene_find_all_by_layer(lua_State *lua_state) {
    const int layer = (int)luaL_checkinteger(lua_state, 1);
    const boolean include_disabled = lua_toboolean(lua_state, 2) ? True : False;

    ScriptRuntime *runtime = lua_runtime_instance(lua_state);
    lua_newtable(lua_state);

    if (!runtime || !runtime->actor_registry)
        return 1;

    int output_index = 1;
    for (usize index = 0; index < runtime->actor_registry->actor_count; ++index) {
        Actor *actor = &runtime->actor_registry->actors[index];
        if (actor->layer != layer)
            continue;

        if (!include_disabled && !actor->enabled)
            continue;

        lua_scene_push_actor_table(lua_state, actor);
        lua_rawseti(lua_state, -2, output_index++);
    }

    return 1;
}

static int lua_scene_actor_count(lua_State *lua_state) {
    const boolean include_disabled = lua_toboolean(lua_state, 1) ? True : False;

    ScriptRuntime *runtime = lua_runtime_instance(lua_state);
    if (!runtime || !runtime->actor_registry) {
        lua_pushinteger(lua_state, 0);
        return 1;
    }

    usize count = 0;
    for (usize index = 0; index < runtime->actor_registry->actor_count; ++index) {
        Actor *actor = &runtime->actor_registry->actors[index];
        if (!include_disabled && !actor->enabled)
            continue;
        count++;
    }

    lua_pushinteger(lua_state, (lua_Integer)count);
    return 1;
}

static int lua_scene_find_first_by_tag(lua_State *lua_state) {
    const char *tag = luaL_checkstring(lua_state, 1);
    const boolean include_disabled = lua_toboolean(lua_state, 2) ? True : False;
    const boolean overlapping_only = lua_toboolean(lua_state, 3) ? True : False;

    ScriptRuntime *runtime = lua_runtime_instance(lua_state);
    if (!runtime || !runtime->actor_registry) {
        lua_pushnil(lua_state);
        return 1;
    }

    for (usize index = 0; index < runtime->actor_registry->actor_count; ++index) {
        Actor *actor = &runtime->actor_registry->actors[index];
        if (!include_disabled && !actor->enabled)
            continue;

        if (!actor_has_tag(actor, tag))
            continue;

        if (overlapping_only && !scene_actor_overlaps_caller(lua_state, actor))
            continue;

        lua_scene_push_actor_table(lua_state, actor);
        return 1;
    }

    lua_pushnil(lua_state);
    return 1;
}

static int lua_scene_find_all_by_tag(lua_State *lua_state) {
    const char *tag = luaL_checkstring(lua_state, 1);
    const boolean include_disabled = lua_toboolean(lua_state, 2) ? True : False;
    const boolean overlapping_only = lua_toboolean(lua_state, 3) ? True : False;

    ScriptRuntime *runtime = lua_runtime_instance(lua_state);
    lua_newtable(lua_state);

    if (!runtime || !runtime->actor_registry)
        return 1;

    int output_index = 1;
    for (usize index = 0; index < runtime->actor_registry->actor_count; ++index) {
        Actor *actor = &runtime->actor_registry->actors[index];
        if (!include_disabled && !actor->enabled)
            continue;

        if (!actor_has_tag(actor, tag))
            continue;

        if (overlapping_only && !scene_actor_overlaps_caller(lua_state, actor))
            continue;

        lua_scene_push_actor_table(lua_state, actor);
        lua_rawseti(lua_state, -2, output_index++);
    }

    return 1;
}

static int lua_scene_load(lua_State *lua_state) {
    const char *scene_path = luaL_checkstring(lua_state, 1);
    lua_pushboolean(lua_state, runtime_request_scene_load(scene_path) == Ok ? 1 : 0);
    return 1;
}

static int lua_scene_current(lua_State *lua_state) {
    const char *scene_path = runtime_current_scene_path();
    if (!scene_path) {
        lua_pushnil(lua_state);
        return 1;
    }

    lua_pushstring(lua_state, scene_path);
    return 1;
}

static int lua_scene_instantiate_prefab(lua_State *lua_state) {
    const char *prefab_ref_id = luaL_checkstring(lua_state, 1);
    const int argument_count = lua_gettop(lua_state);
    const boolean has_position = argument_count >= 4 ? True : False;

    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    if (has_position) {
        x = (float)luaL_checknumber(lua_state, 2);
        y = (float)luaL_checknumber(lua_state, 3);
        z = (float)luaL_checknumber(lua_state, 4);
    }

    char actor_id[128] = {0};
    if (runtime_instantiate_prefab(prefab_ref_id, has_position, x, y, z, actor_id, sizeof(actor_id)) != Ok) {
        lua_pushnil(lua_state);
        return 1;
    }

    lua_newtable(lua_state);
    lua_pushstring(lua_state, actor_id);
    lua_setfield(lua_state, -2, "id");

    lua_pushstring(lua_state, actor_id);
    lua_pushcclosure(lua_state, lua_scene_actor_destroy, 1);
    lua_setfield(lua_state, -2, "destroy");

    return 1;
}

static int lua_scene_set_destroy_on_load(lua_State *lua_state) {
    const char *actor_id = luaL_checkstring(lua_state, 1);
    const boolean destroy_on_load = lua_toboolean(lua_state, 2) ? True : False;
    lua_pushboolean(lua_state, runtime_set_actor_destroy_on_load(actor_id, destroy_on_load) == Ok ? 1 : 0);
    return 1;
}

static int lua_scene_get_destroy_on_load(lua_State *lua_state) {
    const char *actor_id = luaL_checkstring(lua_state, 1);
    boolean destroy_on_load = False;
    if (runtime_get_actor_destroy_on_load(actor_id, &destroy_on_load) != Ok) {
        lua_pushnil(lua_state);
        return 1;
    }

    lua_pushboolean(lua_state, destroy_on_load ? 1 : 0);
    return 1;
}

static const luaL_Reg scene_methods[] = {
    {"find_by_id", lua_scene_find_by_id},
    {"find_first_by_layer", lua_scene_find_first_by_layer},
    {"find_all_by_layer", lua_scene_find_all_by_layer},
    {"find_first_by_tag", lua_scene_find_first_by_tag},
    {"find_all_by_tag", lua_scene_find_all_by_tag},
    {"actor_count", lua_scene_actor_count},
    {"instantiate_prefab", lua_scene_instantiate_prefab},
    {"set_destroy_on_load", lua_scene_set_destroy_on_load},
    {"get_destroy_on_load", lua_scene_get_destroy_on_load},
    {"load", lua_scene_load},
    {"current", lua_scene_current},
    {NULL, NULL},
};

int luaopen_engine_scene(lua_State *lua_state) {
    lua_newtable(lua_state);
    luaL_setfuncs(lua_state, scene_methods, 0);
    return 1;
}
