#include "script_runtime_internal.h"

#include "path_utils.h"
#include "vfs.h"

#include <stdio.h>
#include <string.h>

static result normalize_script_path(const char *path, char *out_path, usize out_size) {
    if (!path || path[0] == '\0' || !out_path || out_size == 0)
        return Err;

    if (is_absolute_path(path))
        return normalize_path(path, out_path, out_size);

    if (normalize_relative_path(path, out_path, out_size) != Ok)
        return Err;

    if (path_has_unsafe_components(out_path))
        return Err;

    return Ok;
}

static result resolve_read_path(lua_State *lua_state, const char *path, char *out_path, usize out_size) {
    if (!lua_state || !path || !out_path || out_size == 0)
        return Err;

    ScriptRuntime *runtime = lua_runtime_instance(lua_state);
    if (!runtime)
        return Err;

    char normalized_path[PATH_MAX] = {0};
    if (normalize_script_path(path, normalized_path, sizeof(normalized_path)) != Ok)
        return Err;

    return runtime_join_path(runtime->project_root, normalized_path, out_path, out_size);
}

static result resolve_write_path(lua_State *lua_state, const char *path, char *out_path, usize out_size) {
    if (!lua_state || !path || !out_path || out_size == 0)
        return Err;

    ScriptRuntime *runtime = lua_runtime_instance(lua_state);
    if (!runtime)
        return Err;

    char normalized_path[PATH_MAX] = {0};
    if (normalize_script_path(path, normalized_path, sizeof(normalized_path)) != Ok)
        return Err;

    if (vfs_is_archive_mode()) {
        if (!is_absolute_path(normalized_path))
            return Err;

        return normalize_path(normalized_path, out_path, out_size);
    }

    return runtime_join_path(runtime->project_root, normalized_path, out_path, out_size);
}

static result write_text_file(const char *path, const char *text, boolean append) {
    if (!path || !text)
        return Err;

    char parent_path[PATH_MAX] = {0};
    if (parent_directory(path, parent_path, sizeof(parent_path)) == Ok) {
        if (ensure_directory_recursive(parent_path) != Ok)
            return Err;
    }

    FILE *file = fopen(path, append ? "ab" : "wb");
    if (!file)
        return Err;

    const usize text_length = strlen(text);
    if (text_length > 0 && fwrite(text, 1, text_length, file) != text_length) {
        fclose(file);
        return Err;
    }

    if (fclose(file) != 0)
        return Err;

    return Ok;
}

static int lua_disk_resolve(lua_State *lua_state) {
    const char *path = luaL_checkstring(lua_state, 1);

    char resolved_path[PATH_MAX] = {0};
    if (resolve_read_path(lua_state, path, resolved_path, sizeof(resolved_path)) != Ok) {
        lua_pushnil(lua_state);
        return 1;
    }

    lua_pushstring(lua_state, resolved_path);
    return 1;
}

static int lua_disk_exists(lua_State *lua_state) {
    const char *path = luaL_checkstring(lua_state, 1);

    char resolved_path[PATH_MAX] = {0};
    if (resolve_read_path(lua_state, path, resolved_path, sizeof(resolved_path)) != Ok) {
        lua_pushboolean(lua_state, 0);
        return 1;
    }

    lua_pushboolean(lua_state, vfs_file_exists(resolved_path) ? 1 : 0);
    return 1;
}

static int lua_disk_read_text(lua_State *lua_state) {
    const char *path = luaL_checkstring(lua_state, 1);

    char resolved_path[PATH_MAX] = {0};
    if (resolve_read_path(lua_state, path, resolved_path, sizeof(resolved_path)) != Ok) {
        lua_pushnil(lua_state);
        return 1;
    }

    Heap text_heap = NullHeap;
    if (vfs_read_file_text(resolved_path, &text_heap) != Ok || !text_heap.pointer) {
        lua_pushnil(lua_state);
        return 1;
    }

    const char *text = (const char *)text_heap.pointer;
    const usize text_length = text_heap.size > 0 ? text_heap.size - 1 : 0;
    lua_pushlstring(lua_state, text, text_length);
    deallocate(text_heap);
    return 1;
}

static int lua_disk_write_text(lua_State *lua_state) {
    const char *path = luaL_checkstring(lua_state, 1);
    const char *text = luaL_checkstring(lua_state, 2);

    char resolved_path[PATH_MAX] = {0};
    if (resolve_write_path(lua_state, path, resolved_path, sizeof(resolved_path)) != Ok) {
        lua_pushboolean(lua_state, 0);
        return 1;
    }

    lua_pushboolean(lua_state, write_text_file(resolved_path, text, False) == Ok ? 1 : 0);
    return 1;
}

static int lua_disk_append_text(lua_State *lua_state) {
    const char *path = luaL_checkstring(lua_state, 1);
    const char *text = luaL_checkstring(lua_state, 2);

    char resolved_path[PATH_MAX] = {0};
    if (resolve_write_path(lua_state, path, resolved_path, sizeof(resolved_path)) != Ok) {
        lua_pushboolean(lua_state, 0);
        return 1;
    }

    lua_pushboolean(lua_state, write_text_file(resolved_path, text, True) == Ok ? 1 : 0);
    return 1;
}

static int lua_disk_save(lua_State *lua_state) {
    return lua_disk_write_text(lua_state);
}

static int lua_disk_ensure_directory(lua_State *lua_state) {
    const char *path = luaL_checkstring(lua_state, 1);

    char resolved_path[PATH_MAX] = {0};
    if (resolve_write_path(lua_state, path, resolved_path, sizeof(resolved_path)) != Ok) {
        lua_pushboolean(lua_state, 0);
        return 1;
    }

    lua_pushboolean(lua_state, ensure_directory_recursive(resolved_path) == Ok ? 1 : 0);
    return 1;
}

static const luaL_Reg disk_methods[] = {
    {"resolve", lua_disk_resolve},
    {"exists", lua_disk_exists},
    {"read_text", lua_disk_read_text},
    {"write_text", lua_disk_write_text},
    {"append_text", lua_disk_append_text},
    {"save", lua_disk_save},
    {"ensure_directory", lua_disk_ensure_directory},
    {NULL, NULL},
};

int luaopen_engine_disk(lua_State *lua_state) {
    lua_newtable(lua_state);
    luaL_setfuncs(lua_state, disk_methods, 0);
    return 1;
}
