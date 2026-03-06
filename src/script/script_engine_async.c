#include "script_runtime_internal.h"

static const char *AsyncThreadsRegistryKey = "__springengine_async_threads";

static int lua_async_thread_resume(lua_State *L);
static int lua_async_thread_cancel(lua_State *L);
static int lua_async_thread_is_alive(lua_State *L);
static int lua_async_thread_is_cancelled(lua_State *L);
static int lua_async_thread_status(lua_State *L);

static const luaL_Reg thread_methods[] = {
    {"resume",       lua_async_thread_resume},
    {"cancel",       lua_async_thread_cancel},
    {"is_alive",     lua_async_thread_is_alive},
    {"is_cancelled", lua_async_thread_is_cancelled},
    {"status",       lua_async_thread_status},
    {NULL, NULL},
};

static void async_create_thread_table(lua_State *L, int fn_arg) {
    fn_arg = lua_absindex(L, fn_arg);
    luaL_checktype(L, fn_arg, LUA_TFUNCTION);

    lua_State *co = lua_newthread(L);
    int co_idx    = lua_gettop(L);

    lua_pushvalue(L, fn_arg);
    lua_xmove(L, co, 1);

    lua_newtable(L);

    lua_pushvalue(L, co_idx);
    lua_setfield(L, -2, "_co");

    lua_pushboolean(L, 0);
    lua_setfield(L, -2, "_cancelled");

    lua_pushboolean(L, 0);
    lua_setfield(L, -2, "_dead");

    luaL_setfuncs(L, thread_methods, 0);

    lua_remove(L, co_idx);
}

static void async_set_wait_from_yield(lua_State *L,
                                      int          tbl_idx,
                                      lua_State   *co,
                                      int          nresults,
                                      ScriptRuntime *runtime) {
    tbl_idx = lua_absindex(L, tbl_idx);

    const char *signal = NULL;
    if (nresults >= 1 && lua_isstring(co, -nresults))
        signal = lua_tostring(co, -nresults);

    if (!signal || strcmp(signal, "frame") == 0) {
        lua_pushliteral(L, "frame");
        lua_setfield(L, tbl_idx, "_wait_type");
        lua_pushnumber(L, (lua_Number)(runtime->time_frame_count + 1));
        lua_setfield(L, tbl_idx, "_wait_value");

    } else if (strcmp(signal, "frames") == 0) {
        int n = (nresults >= 2 && lua_isnumber(co, -nresults + 1))
                    ? (int)lua_tointeger(co, -nresults + 1) : 1;
        if (n < 1) n = 1;
        lua_pushliteral(L, "frame");
        lua_setfield(L, tbl_idx, "_wait_type");
        lua_pushnumber(L, (lua_Number)(runtime->time_frame_count + (lua_Number)n));
        lua_setfield(L, tbl_idx, "_wait_value");

    } else if (strcmp(signal, "seconds") == 0) {
        float t = (nresults >= 2 && lua_isnumber(co, -nresults + 1))
                      ? (float)lua_tonumber(co, -nresults + 1) : 0.0f;
        if (t < 0.0f) t = 0.0f;
        lua_pushliteral(L, "seconds");
        lua_setfield(L, tbl_idx, "_wait_type");
        lua_pushnumber(L, (lua_Number)(runtime->time_unscaled_elapsed_time + t));
        lua_setfield(L, tbl_idx, "_wait_value");

    } else {
        lua_pushliteral(L, "frame");
        lua_setfield(L, tbl_idx, "_wait_type");
        lua_pushnumber(L, (lua_Number)(runtime->time_frame_count + 1));
        lua_setfield(L, tbl_idx, "_wait_value");
    }

    lua_pop(co, nresults);
}

static void async_register_auto_thread(lua_State *L, int tbl_idx) {
    tbl_idx = lua_absindex(L, tbl_idx);

    lua_getfield(L, LUA_REGISTRYINDEX, AsyncThreadsRegistryKey);
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushvalue(L, -1);
        lua_setfield(L, LUA_REGISTRYINDEX, AsyncThreadsRegistryKey);
    }

    int list_idx = lua_gettop(L);
    int len = (int)lua_rawlen(L, list_idx);
    lua_pushvalue(L, tbl_idx);
    lua_rawseti(L, list_idx, len + 1);

    lua_pop(L, 1);
}

static int lua_async_thread_resume(lua_State *L) {
    luaL_checktype(L, 1, LUA_TTABLE);

    lua_getfield(L, 1, "_cancelled");
    int cancelled = lua_toboolean(L, -1);
    lua_pop(L, 1);

    lua_getfield(L, 1, "_dead");
    int dead = lua_toboolean(L, -1);
    lua_pop(L, 1);

    if (cancelled) {
        lua_pushboolean(L, 0);
        lua_pushliteral(L, "thread is cancelled");
        return 2;
    }
    if (dead) {
        lua_pushboolean(L, 0);
        lua_pushliteral(L, "thread is dead");
        return 2;
    }

    lua_getfield(L, 1, "_co");
    lua_State *co = lua_tothread(L, -1);
    lua_pop(L, 1);

    if (!co) {
        lua_pushboolean(L, 0);
        lua_pushliteral(L, "invalid coroutine");
        return 2;
    }

    int nargs = lua_gettop(L) - 1;

    if (nargs > 0) {
        if (!lua_checkstack(co, nargs + 1)) {
            lua_pushboolean(L, 0);
            lua_pushliteral(L, "stack overflow");
            return 2;
        }
        lua_xmove(L, co, nargs);
    }

    int nresults = 0;
    int status   = lua_resume(co, L, nargs, &nresults);

    if (status == LUA_YIELD) {
        if (nresults > 0 && lua_checkstack(L, nresults + 1)) {
            lua_xmove(co, L, nresults);
        } else {
            lua_pop(co, nresults);
            nresults = 0;
        }
        lua_pushboolean(L, 1);
        lua_insert(L, 2);
        return 1 + nresults;

    } else if (status == LUA_OK) {
        lua_pop(co, nresults);
        lua_pushboolean(L, 1);
        lua_setfield(L, 1, "_dead");
        lua_pushboolean(L, 1);
        return 1;

    } else {
        lua_pushboolean(L, 1);
        lua_setfield(L, 1, "_dead");
        if (nresults > 0) {
            const char *err = lua_tostring(co, -1);
            log_err("[Lua Async] thread error: %s", err ? err : "(no message)");
            lua_pop(co, nresults);
        }
        lua_pushboolean(L, 0);
        lua_pushliteral(L, "thread error");
        return 2;
    }
}

static int lua_async_thread_cancel(lua_State *L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_pushboolean(L, 1);
    lua_setfield(L, 1, "_cancelled");
    return 0;
}

static int lua_async_thread_is_alive(lua_State *L) {
    luaL_checktype(L, 1, LUA_TTABLE);

    lua_getfield(L, 1, "_cancelled");
    int cancelled = lua_toboolean(L, -1);
    lua_pop(L, 1);

    lua_getfield(L, 1, "_dead");
    int dead = lua_toboolean(L, -1);
    lua_pop(L, 1);

    lua_pushboolean(L, (!cancelled && !dead) ? 1 : 0);
    return 1;
}

static int lua_async_thread_is_cancelled(lua_State *L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_getfield(L, 1, "_cancelled");
    return 1;
}

static int lua_async_thread_status(lua_State *L) {
    luaL_checktype(L, 1, LUA_TTABLE);

    lua_getfield(L, 1, "_cancelled");
    if (lua_toboolean(L, -1)) {
        lua_pop(L, 1);
        lua_pushliteral(L, "cancelled");
        return 1;
    }
    lua_pop(L, 1);

    lua_getfield(L, 1, "_dead");
    if (lua_toboolean(L, -1)) {
        lua_pop(L, 1);
        lua_pushliteral(L, "dead");
        return 1;
    }
    lua_pop(L, 1);

    lua_getfield(L, 1, "_co");
    lua_State *co = lua_tothread(L, -1);
    lua_pop(L, 1);

    if (!co) {
        lua_pushliteral(L, "dead");
        return 1;
    }

    int s = lua_status(co);
    if (s == LUA_YIELD) {
        lua_pushliteral(L, "suspended");
    } else if (s == LUA_OK) {
        lua_pushstring(L, lua_gettop(co) > 0 ? "pending" : "dead");
    } else {
        lua_pushliteral(L, "dead");
    }
    return 1;
}

static int lua_async_make_thread(lua_State *L) {
    luaL_checktype(L, 1, LUA_TFUNCTION);
    async_create_thread_table(L, 1);
    return 1;
}

static int lua_async_start(lua_State *L) {
    luaL_checktype(L, 1, LUA_TFUNCTION);
    ScriptRuntime *runtime = lua_runtime_instance(L);

    async_create_thread_table(L, 1);
    int tbl_abs = lua_gettop(L);

    lua_getfield(L, tbl_abs, "_co");
    lua_State *co = lua_tothread(L, -1);
    lua_pop(L, 1);

    if (!co)
        return 1;

    int nresults = 0;
    int status   = lua_resume(co, L, 0, &nresults);

    if (status == LUA_YIELD) {
        if (runtime) {
            async_set_wait_from_yield(L, tbl_abs, co, nresults, runtime);
        } else {
            lua_pop(co, nresults);
        }
        async_register_auto_thread(L, tbl_abs);

    } else if (status == LUA_OK) {
        lua_pop(co, nresults);
        lua_pushboolean(L, 1);
        lua_setfield(L, tbl_abs, "_dead");

    } else {
        if (nresults > 0) {
            const char *err = lua_tostring(co, -1);
            log_err("[Lua Async] async.start error: %s", err ? err : "(no message)");
            lua_pop(co, nresults);
        }
        lua_pushboolean(L, 1);
        lua_setfield(L, tbl_abs, "_dead");
    }

    return 1;
}

static int lua_async_yield_frame(lua_State *L) {
    lua_pushliteral(L, "frame");
    return lua_yield(L, 1);
}

static int lua_async_yield_frames(lua_State *L) {
    lua_Integer n = luaL_checkinteger(L, 1);
    if (n < 1) n = 1;
    lua_pushliteral(L, "frames");
    lua_pushinteger(L, n);
    return lua_yield(L, 2);
}

static int lua_async_yield_seconds(lua_State *L) {
    lua_Number t = luaL_checknumber(L, 1);
    if (t < 0.0) t = 0.0;
    lua_pushliteral(L, "seconds");
    lua_pushnumber(L, t);
    return lua_yield(L, 2);
}

void script_async_tick(ScriptRuntime *runtime) {
    if (!runtime || !runtime->lua_state)
        return;

    lua_State *L = runtime->lua_state;

    lua_getfield(L, LUA_REGISTRYINDEX, AsyncThreadsRegistryKey);
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        return;
    }

    int list_idx  = lua_gettop(L);
    int len       = (int)lua_rawlen(L, list_idx);
    int write_pos = 1;

    for (int i = 1; i <= len; i++) {
        lua_rawgeti(L, list_idx, i);

        if (!lua_istable(L, -1)) {
            lua_pop(L, 1);
            continue;
        }

        int tbl = lua_gettop(L);

        lua_getfield(L, tbl, "_cancelled");
        int cancelled = lua_toboolean(L, -1);
        lua_pop(L, 1);

        lua_getfield(L, tbl, "_dead");
        int dead = lua_toboolean(L, -1);
        lua_pop(L, 1);

        if (cancelled || dead) {
            lua_pop(L, 1);
            continue;
        }

        lua_getfield(L, tbl, "_wait_type");
        const char *wait_type = lua_isnil(L, -1) ? NULL : lua_tostring(L, -1);
        lua_pop(L, 1);

        lua_getfield(L, tbl, "_wait_value");
        lua_Number wait_value = lua_tonumber(L, -1);
        lua_pop(L, 1);

        int should_resume = 0;
        if (!wait_type || strcmp(wait_type, "frame") == 0) {
            should_resume = ((lua_Number)runtime->time_frame_count >= wait_value);
        } else if (strcmp(wait_type, "seconds") == 0) {
            should_resume = ((lua_Number)runtime->time_unscaled_elapsed_time >= wait_value);
        } else {
            should_resume = 1;
        }

        boolean keep = True;

        if (should_resume) {
            lua_getfield(L, tbl, "_co");
            lua_State *co = lua_tothread(L, -1);
            lua_pop(L, 1);

            if (!co) {
                keep = False;
            } else {
                int nresults = 0;
                int status   = lua_resume(co, L, 0, &nresults);

                if (status == LUA_YIELD) {
                    async_set_wait_from_yield(L, tbl, co, nresults, runtime);

                } else if (status == LUA_OK) {
                    lua_pop(co, nresults);
                    lua_pushboolean(L, 1);
                    lua_setfield(L, tbl, "_dead");
                    keep = False;

                } else {
                    if (nresults > 0) {
                        const char *err = lua_tostring(co, -1);
                        log_err("[Lua Async] auto-thread error: %s", err ? err : "(no message)");
                        lua_pop(co, nresults);
                    }
                    lua_pushboolean(L, 1);
                    lua_setfield(L, tbl, "_dead");
                    keep = False;
                }
            }
        }

        if (keep) {
            lua_rawseti(L, list_idx, write_pos++);
        } else {
            lua_pop(L, 1);
        }
    }

    for (int i = write_pos; i <= len; i++) {
        lua_pushnil(L);
        lua_rawseti(L, list_idx, i);
    }

    lua_pop(L, 1);
}

static const luaL_Reg async_module_methods[] = {
    {"make_thread",   lua_async_make_thread},
    {"start",         lua_async_start},
    {"yield",         lua_async_yield_frame},
    {"yield_frame",   lua_async_yield_frame},
    {"yield_frames",  lua_async_yield_frames},
    {"yield_seconds", lua_async_yield_seconds},
    {NULL, NULL},
};

int luaopen_engine_async(lua_State *L) {
    lua_getfield(L, LUA_REGISTRYINDEX, AsyncThreadsRegistryKey);
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_setfield(L, LUA_REGISTRYINDEX, AsyncThreadsRegistryKey);
    } else {
        lua_pop(L, 1);
    }

    lua_newtable(L);
    luaL_setfuncs(L, async_module_methods, 0);
    return 1;
}
