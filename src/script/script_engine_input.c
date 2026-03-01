#include "script_runtime_internal.h"

#include <ctype.h>

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

void reset_input_config(void) {
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

result load_input_config(const char *project_root) {
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

static const luaL_Reg input_methods[] = {
    {"current_schema", lua_input_current_schema},
    {"set_schema", lua_input_set_schema},
    {"accepted", lua_input_accepted},
    {"is_key_down", lua_input_is_key_down},
    {"was_key_pressed", lua_input_was_key_pressed},
    {NULL, NULL},
};

int luaopen_engine_input(lua_State *lua_state) {
    lua_newtable(lua_state);
    luaL_setfuncs(lua_state, input_methods, 0);
    return 1;
}