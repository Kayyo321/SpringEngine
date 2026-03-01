#include "script_runtime_internal.h"

#include <string.h>

static result parse_handle(const char *handle, char *out_document_id, usize out_document_size, char *out_node_id, usize out_node_size) {
    if (!handle || !out_document_id || !out_node_id || out_document_size == 0 || out_node_size == 0)
        return Err;

    const char *separator = strstr(handle, "::");
    if (!separator || separator == handle || separator[2] == '\0')
        return Err;

    const usize document_length = (usize)(separator - handle);
    if (document_length + 1 > out_document_size)
        return Err;

    memcpy(out_document_id, handle, document_length);
    out_document_id[document_length] = '\0';

    const char *node_id = separator + 2;
    if (snprintf(out_node_id, out_node_size, "%s", node_id) >= (int)out_node_size)
        return Err;

    return Ok;
}

static int lua_ui_find(lua_State *lua_state) {
    const char *document_id = luaL_checkstring(lua_state, 1);
    const char *node_id = luaL_checkstring(lua_state, 2);

    if (runtime_ui_node_exists(document_id, node_id) != Ok) {
        lua_pushnil(lua_state);
        return 1;
    }

    char handle[512] = {0};
    if (snprintf(handle, sizeof(handle), "%s::%s", document_id, node_id) >= (int)sizeof(handle)) {
        lua_pushnil(lua_state);
        return 1;
    }

    lua_pushstring(lua_state, handle);
    return 1;
}

static int lua_ui_set_visible(lua_State *lua_state) {
    const char *handle = luaL_checkstring(lua_state, 1);
    const boolean visible = lua_toboolean(lua_state, 2) ? True : False;

    char document_id[256] = {0};
    char node_id[256] = {0};
    if (parse_handle(handle, document_id, sizeof(document_id), node_id, sizeof(node_id)) != Ok) {
        lua_pushboolean(lua_state, 0);
        return 1;
    }

    lua_pushboolean(lua_state, runtime_ui_set_visible(document_id, node_id, visible) == Ok ? 1 : 0);
    return 1;
}

static int lua_ui_set_text(lua_State *lua_state) {
    const char *handle = luaL_checkstring(lua_state, 1);
    const char *text = luaL_checkstring(lua_state, 2);

    char document_id[256] = {0};
    char node_id[256] = {0};
    if (parse_handle(handle, document_id, sizeof(document_id), node_id, sizeof(node_id)) != Ok) {
        lua_pushboolean(lua_state, 0);
        return 1;
    }

    lua_pushboolean(lua_state, runtime_ui_set_text(document_id, node_id, text) == Ok ? 1 : 0);
    return 1;
}

static const luaL_Reg ui_methods[] = {
    {"find", lua_ui_find},
    {"set_visible", lua_ui_set_visible},
    {"set_text", lua_ui_set_text},
    {NULL, NULL},
};

int luaopen_engine_ui(lua_State *lua_state) {
    lua_newtable(lua_state);
    luaL_setfuncs(lua_state, ui_methods, 0);
    return 1;
}
