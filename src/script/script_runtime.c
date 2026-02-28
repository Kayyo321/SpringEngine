#include "script_runtime.h"

#include "common.h"

#include "lauxlib.h"
#include "lualib.h"
#include "raylib.h"
#include "tomlc17.h"

#include <ctype.h>
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
static const char *RUNTIME_REGISTRY_KEY = "__springengine_runtime";
static const char *CURRENT_ACTOR_REGISTRY_KEY = "__springengine_current_actor";

enum {
    InputMaxSchemas = 16,
    InputMaxActionsPerSchema = 128,
    InputMaxBindingsPerAction = 8,
    InputMaxKeysPerBinding = 8,
    InputMaxNameLength = 64,
};

typedef enum {
    InputBindingKeyDown,
    InputBindingKeyPressed,
    InputBindingChord,
} InputBindingKind;

typedef struct {
    InputBindingKind kind;
    usize key_count;
    int keys[InputMaxKeysPerBinding];
} InputBinding;

typedef struct {
    char name[InputMaxNameLength];
    usize binding_count;
    InputBinding bindings[InputMaxBindingsPerAction];
} InputAction;

typedef struct {
    char name[InputMaxNameLength];
    usize action_count;
    InputAction actions[InputMaxActionsPerSchema];
} InputSchema;

typedef struct {
    usize schema_count;
    usize active_schema_index;
    boolean has_active_schema;
    InputSchema schemas[InputMaxSchemas];
} InputConfig;

static InputConfig input_config;

static void reset_input_config(void) {
    memset(&input_config, 0, sizeof(input_config));
}

static boolean string_equals_ignore_case(const char *left, const char *right) {
    if (!left || !right)
        return False;

    while (*left && *right) {
        if (tolower((unsigned char)*left) != tolower((unsigned char)*right))
            return False;
        left++;
        right++;
    }

    return (*left == '\0' && *right == '\0') ? True : False;
}

static int key_code_from_name(const char *name) {
    if (!name || name[0] == '\0')
        return -1;

    if (name[1] == '\0' && isalpha((unsigned char)name[0])) {
        const char upper = (char)toupper((unsigned char)name[0]);
        return KEY_A + (upper - 'A');
    }

    if (string_equals_ignore_case(name, "space")) return KEY_SPACE;
    if (string_equals_ignore_case(name, "enter") || string_equals_ignore_case(name, "return")) return KEY_ENTER;
    if (string_equals_ignore_case(name, "tab")) return KEY_TAB;
    if (string_equals_ignore_case(name, "backspace")) return KEY_BACKSPACE;

    if (string_equals_ignore_case(name, "up")) return KEY_UP;
    if (string_equals_ignore_case(name, "down")) return KEY_DOWN;
    if (string_equals_ignore_case(name, "left")) return KEY_LEFT;
    if (string_equals_ignore_case(name, "right")) return KEY_RIGHT;

    if (string_equals_ignore_case(name, "escape") || string_equals_ignore_case(name, "esc")) return KEY_ESCAPE;
    if (string_equals_ignore_case(name, "left_control") || string_equals_ignore_case(name, "lctrl") || string_equals_ignore_case(name, "ctrl"))
        return KEY_LEFT_CONTROL;
    if (string_equals_ignore_case(name, "right_control") || string_equals_ignore_case(name, "rctrl")) return KEY_RIGHT_CONTROL;
    if (string_equals_ignore_case(name, "left_shift") || string_equals_ignore_case(name, "lshift")) return KEY_LEFT_SHIFT;
    if (string_equals_ignore_case(name, "right_shift") || string_equals_ignore_case(name, "rshift")) return KEY_RIGHT_SHIFT;
    if (string_equals_ignore_case(name, "left_alt") || string_equals_ignore_case(name, "lalt")) return KEY_LEFT_ALT;
    if (string_equals_ignore_case(name, "right_alt") || string_equals_ignore_case(name, "ralt")) return KEY_RIGHT_ALT;

    return -1;
}

static result copy_name(char *out_name, usize out_size, const char *name) {
    if (!out_name || out_size == 0 || !name || name[0] == '\0')
        return Err;

    if (snprintf(out_name, out_size, "%s", name) >= (int)out_size)
        return Err;

    return Ok;
}

static result parse_binding_kind(toml_datum_t binding_table, InputBindingKind *out_kind) {
    if (!out_kind)
        return Err;

    *out_kind = InputBindingKeyDown;

    toml_datum_t kind = toml_get(binding_table, "kind");
    if (kind.type != TOML_STRING || !kind.u.s || kind.u.s[0] == '\0')
        return Ok;

    if (string_equals_ignore_case(kind.u.s, "key_down")) {
        *out_kind = InputBindingKeyDown;
        return Ok;
    }

    if (string_equals_ignore_case(kind.u.s, "key_pressed")) {
        *out_kind = InputBindingKeyPressed;
        return Ok;
    }

    if (string_equals_ignore_case(kind.u.s, "chord")) {
        *out_kind = InputBindingChord;
        return Ok;
    }

    log_warn("Unknown input binding kind '%s'; defaulting to key_down", kind.u.s);
    return Ok;
}

static result parse_binding_keys(toml_datum_t binding_table, InputBinding *out_binding) {
    if (!out_binding)
        return Err;

    toml_datum_t keys = toml_get(binding_table, "keys");
    if (keys.type == TOML_STRING && keys.u.s && keys.u.s[0] != '\0') {
        const int key_code = key_code_from_name(keys.u.s);
        if (key_code < 0) {
            log_warn("Unknown input key '%s'", keys.u.s);
            return Err;
        }

        out_binding->keys[0] = key_code;
        out_binding->key_count = 1;
        return Ok;
    }

    if (keys.type != TOML_ARRAY) {
        log_warn("Input binding is missing keys array");
        return Err;
    }

    for (int index = 0; index < keys.u.arr.size; ++index) {
        toml_datum_t key_name = keys.u.arr.elem[index];
        if (key_name.type != TOML_STRING || !key_name.u.s || key_name.u.s[0] == '\0') {
            log_warn("Input binding keys[%d] must be a non-empty string", index);
            continue;
        }

        if (out_binding->key_count >= InputMaxKeysPerBinding) {
            log_warn("Input binding exceeded max %d keys; extras ignored", InputMaxKeysPerBinding);
            break;
        }

        const int key_code = key_code_from_name(key_name.u.s);
        if (key_code < 0) {
            log_warn("Unknown input key '%s'", key_name.u.s);
            continue;
        }

        out_binding->keys[out_binding->key_count++] = key_code;
    }

    return out_binding->key_count > 0 ? Ok : Err;
}

static result parse_binding_table(toml_datum_t binding_table, InputBinding *out_binding) {
    if (binding_table.type != TOML_TABLE || !out_binding)
        return Err;

    memset(out_binding, 0, sizeof(*out_binding));

    if (parse_binding_kind(binding_table, &out_binding->kind) != Ok)
        return Err;

    if (parse_binding_keys(binding_table, out_binding) != Ok)
        return Err;

    return Ok;
}

static result parse_action_bindings(const char *schema_name, const char *action_name, toml_datum_t action_value, InputAction *out_action) {
    if (!schema_name || !action_name || !out_action)
        return Err;

    if (copy_name(out_action->name, sizeof(out_action->name), action_name) != Ok) {
        log_warn("Action name too long in schema '%s': '%s'", schema_name, action_name);
        return Err;
    }

    if (action_value.type == TOML_TABLE) {
        if (parse_binding_table(action_value, &out_action->bindings[0]) != Ok)
            return Err;

        out_action->binding_count = 1;
        return Ok;
    }

    if (action_value.type == TOML_ARRAY) {
        for (int index = 0; index < action_value.u.arr.size; ++index) {
            if (out_action->binding_count >= InputMaxBindingsPerAction) {
                log_warn("Action '%s' in schema '%s' exceeded max %d bindings; extras ignored", action_name, schema_name, InputMaxBindingsPerAction);
                break;
            }

            toml_datum_t binding_table = action_value.u.arr.elem[index];
            if (parse_binding_table(binding_table, &out_action->bindings[out_action->binding_count]) != Ok) {
                log_warn("Invalid binding in action '%s' (schema '%s') at index %d", action_name, schema_name, index);
                continue;
            }

            out_action->binding_count++;
        }

        return out_action->binding_count > 0 ? Ok : Err;
    }

    if (action_value.type == TOML_STRING && action_value.u.s && action_value.u.s[0] != '\0') {
        InputBinding binding = {0};
        binding.kind = InputBindingKeyDown;
        const int key_code = key_code_from_name(action_value.u.s);
        if (key_code < 0)
            return Err;

        binding.keys[0] = key_code;
        binding.key_count = 1;
        out_action->bindings[0] = binding;
        out_action->binding_count = 1;
        return Ok;
    }

    log_warn("Action '%s' in schema '%s' must be a binding table or binding table array", action_name, schema_name);
    return Err;
}

static result load_input_config(const char *project_root) {
    if (!project_root)
        return Err;

    reset_input_config();

    char input_config_path[PATH_MAX] = {0};
    if (snprintf(input_config_path, sizeof(input_config_path), "%s/input.conf", project_root) >= (int)sizeof(input_config_path)) {
        log_err("Input config path is too long for project '%s'", project_root);
        return Err;
    }

    toml_result_t parsed = toml_parse_file_ex(input_config_path);
    if (!parsed.ok) {
        log_err("Failed to parse input config '%s': %s", input_config_path, parsed.errmsg);
        toml_free(parsed);
        return Err;
    }

    toml_datum_t schema_table = toml_get(parsed.toptab, "Schema");
    if (schema_table.type != TOML_TABLE) {
        log_err("Input config '%s' is missing [Schema] table", input_config_path);
        toml_free(parsed);
        return Err;
    }

    toml_datum_t input_table = toml_get(parsed.toptab, "Input");
    const char *requested_active_schema = Null;
    if (input_table.type == TOML_TABLE) {
        toml_datum_t active_schema = toml_get(input_table, "active_schema");
        if (active_schema.type == TOML_STRING && active_schema.u.s && active_schema.u.s[0] != '\0')
            requested_active_schema = active_schema.u.s;
    }

    for (int schema_index = 0; schema_index < schema_table.u.tab.size; ++schema_index) {
        if (input_config.schema_count >= InputMaxSchemas) {
            log_warn("Input config exceeded max %d schemas; extras ignored", InputMaxSchemas);
            break;
        }

        toml_datum_t schema_value = schema_table.u.tab.value[schema_index];
        if (schema_value.type != TOML_TABLE)
            continue;

        const char *schema_name = schema_table.u.tab.key[schema_index];
        if (!schema_name || schema_name[0] == '\0')
            continue;

        InputSchema *schema = &input_config.schemas[input_config.schema_count];
        memset(schema, 0, sizeof(*schema));

        if (copy_name(schema->name, sizeof(schema->name), schema_name) != Ok) {
            log_warn("Schema name too long: '%s'", schema_name);
            continue;
        }

        for (int action_index = 0; action_index < schema_value.u.tab.size; ++action_index) {
            if (schema->action_count >= InputMaxActionsPerSchema) {
                log_warn("Schema '%s' exceeded max %d actions; extras ignored", schema_name, InputMaxActionsPerSchema);
                break;
            }

            const char *action_name = schema_value.u.tab.key[action_index];
            toml_datum_t action_value = schema_value.u.tab.value[action_index];
            if (!action_name || action_name[0] == '\0')
                continue;

            InputAction *action = &schema->actions[schema->action_count];
            memset(action, 0, sizeof(*action));
            if (parse_action_bindings(schema_name, action_name, action_value, action) != Ok)
                continue;

            schema->action_count++;
        }

        if (schema->action_count == 0) {
            log_warn("Schema '%s' has no valid actions", schema_name);
            continue;
        }

        if (requested_active_schema && string_equals_ignore_case(requested_active_schema, schema->name)) {
            input_config.active_schema_index = input_config.schema_count;
            input_config.has_active_schema = True;
        }

        input_config.schema_count++;
    }

    toml_free(parsed);

    if (input_config.schema_count == 0) {
        log_err("Input config '%s' has no valid schemas", input_config_path);
        return Err;
    }

    if (!input_config.has_active_schema) {
        input_config.active_schema_index = 0;
        input_config.has_active_schema = True;
    }

    log_msg("Loaded input config '%s' with %lu schema(s); active schema '%s'",
            input_config_path,
            input_config.schema_count,
            input_config.schemas[input_config.active_schema_index].name);

    return Ok;
}

static InputSchema *active_input_schema(void) {
    if (!input_config.has_active_schema || input_config.schema_count == 0 || input_config.active_schema_index >= input_config.schema_count)
        return Null;

    return &input_config.schemas[input_config.active_schema_index];
}

static InputAction *find_action(InputSchema *schema, const char *action_name) {
    if (!schema || !action_name || action_name[0] == '\0')
        return Null;

    for (usize index = 0; index < schema->action_count; ++index) {
        if (strcmp(schema->actions[index].name, action_name) == 0)
            return &schema->actions[index];
    }

    return Null;
}

static boolean is_binding_active(const InputBinding *binding) {
    if (!binding || binding->key_count == 0)
        return False;

    switch (binding->kind) {
        case InputBindingKeyDown:
            for (usize index = 0; index < binding->key_count; ++index) {
                if (IsKeyDown(binding->keys[index]))
                    return True;
            }
            return False;

        case InputBindingKeyPressed:
            for (usize index = 0; index < binding->key_count; ++index) {
                if (IsKeyPressed(binding->keys[index]))
                    return True;
            }
            return False;

        case InputBindingChord:
            for (usize index = 0; index < binding->key_count; ++index) {
                if (!IsKeyDown(binding->keys[index]))
                    return False;
            }
            return True;
    }

    return False;
}

static boolean input_action_accepted(const char *action_name) {
    InputSchema *schema = active_input_schema();
    if (!schema)
        return False;

    InputAction *action = find_action(schema, action_name);
    if (!action)
        return False;

    for (usize index = 0; index < action->binding_count; ++index) {
        if (is_binding_active(&action->bindings[index]))
            return True;
    }

    return False;
}

static int lua_input_current_schema(lua_State *lua_state) {
    InputSchema *schema = active_input_schema();
    if (!schema) {
        lua_pushnil(lua_state);
        return 1;
    }

    lua_pushstring(lua_state, schema->name);
    return 1;
}

static int lua_input_set_schema(lua_State *lua_state) {
    const char *schema_name = luaL_checkstring(lua_state, 1);
    if (!schema_name || schema_name[0] == '\0') {
        lua_pushboolean(lua_state, 0);
        return 1;
    }

    for (usize index = 0; index < input_config.schema_count; ++index) {
        if (string_equals_ignore_case(input_config.schemas[index].name, schema_name)) {
            input_config.active_schema_index = index;
            input_config.has_active_schema = True;
            lua_pushboolean(lua_state, 1);
            return 1;
        }
    }

    lua_pushboolean(lua_state, 0);
    return 1;
}

static int lua_input_accepted(lua_State *lua_state) {
    const char *action_name = luaL_checkstring(lua_state, 1);
    lua_pushboolean(lua_state, input_action_accepted(action_name) ? 1 : 0);
    return 1;
}

static int lua_input_is_key_down(lua_State *lua_state) {
    const char *key_name = luaL_checkstring(lua_state, 1);
    const int key_code = key_code_from_name(key_name);
    lua_pushboolean(lua_state, (key_code >= 0 && IsKeyDown(key_code)) ? 1 : 0);
    return 1;
}

static int lua_input_was_key_pressed(lua_State *lua_state) {
    const char *key_name = luaL_checkstring(lua_state, 1);
    const int key_code = key_code_from_name(key_name);
    lua_pushboolean(lua_state, (key_code >= 0 && IsKeyPressed(key_code)) ? 1 : 0);
    return 1;
}

static Actor *lua_runtime_current_actor(lua_State *lua_state) {
    if (!lua_state)
        return Null;

    lua_getfield(lua_state, LUA_REGISTRYINDEX, CURRENT_ACTOR_REGISTRY_KEY);
    Actor *actor = (Actor *)lua_touserdata(lua_state, -1);
    lua_pop(lua_state, 1);
    return actor;
}

static void lua_runtime_set_current_actor(lua_State *lua_state, Actor *actor) {
    if (!lua_state)
        return;

    if (actor)
        lua_pushlightuserdata(lua_state, actor);
    else
        lua_pushnil(lua_state);

    lua_setfield(lua_state, LUA_REGISTRYINDEX, CURRENT_ACTOR_REGISTRY_KEY);
}

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

static result register_input_library(ScriptRuntime *runtime) {
    if (!runtime || !runtime->lua_state)
        return Err;

    static const luaL_Reg input_methods[] = {
        {"current_schema", lua_input_current_schema},
        {"set_schema", lua_input_set_schema},
        {"accepted", lua_input_accepted},
        {"is_key_down", lua_input_is_key_down},
        {"was_key_pressed", lua_input_was_key_pressed},
        {NULL, NULL},
    };

    lua_pushlightuserdata(runtime->lua_state, runtime);
    lua_setfield(runtime->lua_state, LUA_REGISTRYINDEX, RUNTIME_REGISTRY_KEY);

    lua_newtable(runtime->lua_state);
    luaL_setfuncs(runtime->lua_state, input_methods, 0);
    lua_setglobal(runtime->lua_state, "Input");

    return Ok;
}

static result register_transform_library(ScriptRuntime *runtime) {
    if (!runtime || !runtime->lua_state)
        return Err;

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

    lua_newtable(runtime->lua_state);
    luaL_setfuncs(runtime->lua_state, transform_methods, 0);
    lua_setglobal(runtime->lua_state, "Transform");
    return Ok;
}

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
    runtime->current_actor = Null;

    if (load_input_config(project_root) != Ok)
        return Err;

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

    if (register_input_library(runtime) != Ok) {
        lua_close(runtime->lua_state);
        runtime->lua_state = Null;
        return Err;
    }

    if (register_transform_library(runtime) != Ok) {
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
