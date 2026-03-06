#include "script_runtime_internal.h"

static const char *AsyncThreadsRegistryKey = "__springengine_async_threads";

/* =========================================================================
   Thread method forward declarations
   ========================================================================= */

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

/* =========================================================================
   Internal helpers
   ========================================================================= */

/* Build a thread table from the function at fn_arg (absolute stack index).
 * Leaves the new thread table at the top of the stack.
 * The coroutine is stored in tbl._co; on return co's stack contains [fn]. */
static void async_create_thread_table(lua_State *L, int fn_arg) {
    fn_arg = lua_absindex(L, fn_arg);
    luaL_checktype(L, fn_arg, LUA_TFUNCTION);

    /* Create the Lua coroutine thread */
    lua_State *co = lua_newthread(L);   /* [..., co_val]          */
    int co_idx    = lua_gettop(L);      /* absolute position of co_val */

    /* Move the function onto co's stack */
    lua_pushvalue(L, fn_arg);           /* [..., co_val, fn_copy] */
    lua_xmove(L, co, 1);               /* co: [fn]; L: [..., co_val] */

    /* Build the thread table */
    lua_newtable(L);                    /* [..., co_val, tbl]     */

    /* tbl._co = co_val */
    lua_pushvalue(L, co_idx);           /* [..., co_val, tbl, co_val] */
    lua_setfield(L, -2, "_co");         /* [..., co_val, tbl]     */

    /* tbl._cancelled = false */
    lua_pushboolean(L, 0);
    lua_setfield(L, -2, "_cancelled");

    /* tbl._dead = false */
    lua_pushboolean(L, 0);
    lua_setfield(L, -2, "_dead");

    /* Add resume / cancel / is_alive / is_cancelled / status methods */
    luaL_setfuncs(L, thread_methods, 0);

    /* Remove co_val from under the table, leaving just [tbl] on top */
    lua_remove(L, co_idx);              /* [..., tbl]             */
}

/* Update the wait condition stored in the thread table at tbl_idx using
 * the values that were just yielded onto co's stack (nresults of them).
 * Pops all nresults from co when done. */
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
        /* Resume on the next frame */
        lua_pushliteral(L, "frame");
        lua_setfield(L, tbl_idx, "_wait_type");
        lua_pushnumber(L, (lua_Number)(runtime->time_frame_count + 1));
        lua_setfield(L, tbl_idx, "_wait_value");

    } else if (strcmp(signal, "frames") == 0) {
        /* Resume after n frames */
        int n = (nresults >= 2 && lua_isnumber(co, -nresults + 1))
                    ? (int)lua_tointeger(co, -nresults + 1) : 1;
        if (n < 1) n = 1;
        lua_pushliteral(L, "frame");  /* reuse "frame" type, just set target */
        lua_setfield(L, tbl_idx, "_wait_type");
        lua_pushnumber(L, (lua_Number)(runtime->time_frame_count + (lua_Number)n));
        lua_setfield(L, tbl_idx, "_wait_value");

    } else if (strcmp(signal, "seconds") == 0) {
        /* Resume after t unscaled seconds */
        float t = (nresults >= 2 && lua_isnumber(co, -nresults + 1))
                      ? (float)lua_tonumber(co, -nresults + 1) : 0.0f;
        if (t < 0.0f) t = 0.0f;
        lua_pushliteral(L, "seconds");
        lua_setfield(L, tbl_idx, "_wait_type");
        lua_pushnumber(L, (lua_Number)(runtime->time_unscaled_elapsed_time + t));
        lua_setfield(L, tbl_idx, "_wait_value");

    } else {
        /* Unknown signal — default to next frame */
        lua_pushliteral(L, "frame");
        lua_setfield(L, tbl_idx, "_wait_type");
        lua_pushnumber(L, (lua_Number)(runtime->time_frame_count + 1));
        lua_setfield(L, tbl_idx, "_wait_value");
    }

    lua_pop(co, nresults);
}

/* Append the thread table at tbl_idx into the auto-threads registry list. */
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

    lua_pop(L, 1); /* pop list */
}

/* =========================================================================
   Thread methods  (all take self=thread-table as arg[1])
   ========================================================================= */

/* thread:resume(...)
 * Advances the coroutine.  Returns true [+ yield values] on success,
 * or false, errmsg on failure. */
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

    /* Args to forward are at positions 2..top (position 1 is self) */
    int nargs = lua_gettop(L) - 1;

    if (nargs > 0) {
        if (!lua_checkstack(co, nargs + 1)) {
            lua_pushboolean(L, 0);
            lua_pushliteral(L, "stack overflow");
            return 2;
        }
        /* xmove pops the top nargs values from L and pushes them onto co */
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
        lua_insert(L, 2); /* [self, true, yield_val1, yield_val2, ...] */
        return 1 + nresults;

    } else if (status == LUA_OK) {
        lua_pop(co, nresults);
        lua_pushboolean(L, 1);
        lua_setfield(L, 1, "_dead");
        lua_pushboolean(L, 1); /* success, thread completed */
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

/* thread:cancel()
 * Marks the thread as cancelled.  The coroutine will not be resumed again
 * by the auto-tick, and thread:resume() will return false. */
static int lua_async_thread_cancel(lua_State *L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_pushboolean(L, 1);
    lua_setfield(L, 1, "_cancelled");
    return 0;
}

/* thread:is_alive()  →  boolean */
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

/* thread:is_cancelled()  →  boolean */
static int lua_async_thread_is_cancelled(lua_State *L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_getfield(L, 1, "_cancelled");
    return 1;
}

/* thread:status()  →  "pending" | "suspended" | "dead" | "cancelled" */
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
        /* A fresh coroutine (not yet started) has the function on its stack */
        lua_pushstring(L, lua_gettop(co) > 0 ? "pending" : "dead");
    } else {
        lua_pushliteral(L, "dead");
    }
    return 1;
}

/* =========================================================================
   Module-level functions
   ========================================================================= */

/* async.make_thread(fn)  →  thread
 * Creates a new thread from fn.  The thread is NOT started automatically;
 * call thread:resume() to advance it. */
static int lua_async_make_thread(lua_State *L) {
    luaL_checktype(L, 1, LUA_TFUNCTION);
    async_create_thread_table(L, 1);
    return 1;
}

/* async.start(fn)  →  thread
 * Creates a new thread, performs the first resume immediately, and
 * registers it for automatic per-frame updates.
 *
 * Inside the coroutine, yield with a signal to control timing:
 *   async.yield()            -- resume next frame
 *   async.yield_frame()      -- same
 *   async.yield_frames(n)    -- resume after n frames
 *   async.yield_seconds(t)   -- resume after t unscaled seconds
 */
static int lua_async_start(lua_State *L) {
    luaL_checktype(L, 1, LUA_TFUNCTION);
    ScriptRuntime *runtime = lua_runtime_instance(L);

    /* Build thread table (fn is at position 1, stack becomes [fn, tbl]) */
    async_create_thread_table(L, 1);
    int tbl_abs = lua_gettop(L);

    /* Get the coroutine for the first resume */
    lua_getfield(L, tbl_abs, "_co");
    lua_State *co = lua_tothread(L, -1);
    lua_pop(L, 1);

    if (!co) {
        return 1; /* return the (invalid) thread table anyway */
    }

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
        /* Thread completed synchronously on the first resume */
        lua_pop(co, nresults);
        lua_pushboolean(L, 1);
        lua_setfield(L, tbl_abs, "_dead");

    } else {
        /* Error on first resume */
        if (nresults > 0) {
            const char *err = lua_tostring(co, -1);
            log_err("[Lua Async] async.start error: %s", err ? err : "(no message)");
            lua_pop(co, nresults);
        }
        lua_pushboolean(L, 1);
        lua_setfield(L, tbl_abs, "_dead");
    }

    return 1; /* return thread table */
}

/* async.yield() / async.yield_frame()
 * Yields the current coroutine and requests resumption on the next frame.
 * Must be called from within a coroutine. */
static int lua_async_yield_frame(lua_State *L) {
    lua_pushliteral(L, "frame");
    return lua_yield(L, 1);
}

/* async.yield_frames(n)
 * Yields the current coroutine and requests resumption after n frames.
 * Must be called from within a coroutine. */
static int lua_async_yield_frames(lua_State *L) {
    lua_Integer n = luaL_checkinteger(L, 1);
    if (n < 1) n = 1;
    lua_pushliteral(L, "frames");
    lua_pushinteger(L, n);
    return lua_yield(L, 2);
}

/* async.yield_seconds(t)
 * Yields the current coroutine and requests resumption after t unscaled seconds.
 * Must be called from within a coroutine. */
static int lua_async_yield_seconds(lua_State *L) {
    lua_Number t = luaL_checknumber(L, 1);
    if (t < 0.0) t = 0.0;
    lua_pushliteral(L, "seconds");
    lua_pushnumber(L, t);
    return lua_yield(L, 2);
}

/* =========================================================================
   Per-frame auto-thread tick  (called from script_runtime_begin_frame)
   ========================================================================= */

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
        lua_rawgeti(L, list_idx, i); /* push thread table */

        if (!lua_istable(L, -1)) {
            lua_pop(L, 1);
            continue;
        }

        int tbl = lua_gettop(L);

        /* ---- check cancelled / dead ---- */
        lua_getfield(L, tbl, "_cancelled");
        int cancelled = lua_toboolean(L, -1);
        lua_pop(L, 1);

        lua_getfield(L, tbl, "_dead");
        int dead = lua_toboolean(L, -1);
        lua_pop(L, 1);

        if (cancelled || dead) {
            lua_pop(L, 1); /* discard thread table */
            continue;
        }

        /* ---- check wait condition ---- */
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
            should_resume = 1; /* unknown type — resume immediately */
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
            /* Compact: store at write_pos (rawseti pops the table) */
            lua_rawseti(L, list_idx, write_pos++);
        } else {
            lua_pop(L, 1); /* discard thread table */
        }
    }

    /* Nil out tail entries left over from compaction */
    for (int i = write_pos; i <= len; i++) {
        lua_pushnil(L);
        lua_rawseti(L, list_idx, i);
    }

    lua_pop(L, 1); /* pop list table */
}

/* =========================================================================
   Module open
   ========================================================================= */

static const luaL_Reg async_module_methods[] = {
    {"make_thread",   lua_async_make_thread},
    {"start",         lua_async_start},
    {"yield",         lua_async_yield_frame},   /* convenience alias */
    {"yield_frame",   lua_async_yield_frame},
    {"yield_frames",  lua_async_yield_frames},
    {"yield_seconds", lua_async_yield_seconds},
    {NULL, NULL},
};

int luaopen_engine_async(lua_State *L) {
    /* Ensure the auto-threads list exists in the registry */
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
