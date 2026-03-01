#include "script_runtime_internal.h"

float runtime_time_apply_rules(ScriptRuntime *runtime, float raw_delta_time) {
    if (!runtime)
        return raw_delta_time;

    float delta_time = raw_delta_time;

    if (delta_time < 0.0f)
        delta_time = 0.0f;

    if (runtime->time_max_delta_time > 0.0f && delta_time > runtime->time_max_delta_time)
        delta_time = runtime->time_max_delta_time;

    if (runtime->time_fixed_delta_time > 0.0f)
        delta_time = runtime->time_fixed_delta_time;

    if (runtime->time_paused)
        return 0.0f;

    if (runtime->time_scale < 0.0f)
        runtime->time_scale = 0.0f;

    delta_time *= runtime->time_scale;

    if (runtime->time_max_delta_time > 0.0f && delta_time > runtime->time_max_delta_time)
        delta_time = runtime->time_max_delta_time;

    return delta_time;
}

static int lua_time_delta_time(lua_State *lua_state) {
    ScriptRuntime *runtime = lua_runtime_instance(lua_state);
    const float delta_time = runtime ? runtime->time_delta_time : (float)GetFrameTime();
    lua_pushnumber(lua_state, delta_time);
    return 1;
}

static int lua_time_unscaled_delta_time(lua_State *lua_state) {
    ScriptRuntime *runtime = lua_runtime_instance(lua_state);
    const float delta_time = runtime ? runtime->time_raw_delta_time : (float)GetFrameTime();
    lua_pushnumber(lua_state, delta_time);
    return 1;
}

static int lua_time_elapsed_time(lua_State *lua_state) {
    ScriptRuntime *runtime = lua_runtime_instance(lua_state);
    const float elapsed = runtime ? runtime->time_elapsed_time : (float)GetTime();
    lua_pushnumber(lua_state, elapsed);
    return 1;
}

static int lua_time_unscaled_elapsed_time(lua_State *lua_state) {
    ScriptRuntime *runtime = lua_runtime_instance(lua_state);
    const float elapsed = runtime ? runtime->time_unscaled_elapsed_time : (float)GetTime();
    lua_pushnumber(lua_state, elapsed);
    return 1;
}

static int lua_time_since_startup(lua_State *lua_state) {
    lua_pushnumber(lua_state, (lua_Number)GetTime());
    return 1;
}

static int lua_time_fps(lua_State *lua_state) {
    lua_pushinteger(lua_state, (lua_Integer)GetFPS());
    return 1;
}

static int lua_time_frame_count(lua_State *lua_state) {
    ScriptRuntime *runtime = lua_runtime_instance(lua_state);
    lua_pushinteger(lua_state, (lua_Integer)(runtime ? runtime->time_frame_count : 0));
    return 1;
}

static int lua_time_time_scale(lua_State *lua_state) {
    ScriptRuntime *runtime = lua_runtime_instance(lua_state);
    lua_pushnumber(lua_state, runtime ? runtime->time_scale : 1.0f);
    return 1;
}

static int lua_time_set_time_scale(lua_State *lua_state) {
    ScriptRuntime *runtime = lua_runtime_instance(lua_state);
    if (!runtime) {
        lua_pushboolean(lua_state, 0);
        return 1;
    }

    float scale = (float)luaL_checknumber(lua_state, 1);
    if (scale < 0.0f)
        scale = 0.0f;

    runtime->time_scale = scale;
    lua_pushboolean(lua_state, 1);
    return 1;
}

static int lua_time_max_delta_time(lua_State *lua_state) {
    ScriptRuntime *runtime = lua_runtime_instance(lua_state);
    lua_pushnumber(lua_state, runtime ? runtime->time_max_delta_time : 0.0f);
    return 1;
}

static int lua_time_set_max_delta_time(lua_State *lua_state) {
    ScriptRuntime *runtime = lua_runtime_instance(lua_state);
    if (!runtime) {
        lua_pushboolean(lua_state, 0);
        return 1;
    }

    float max_delta_time = (float)luaL_checknumber(lua_state, 1);
    if (max_delta_time < 0.0f)
        max_delta_time = 0.0f;

    runtime->time_max_delta_time = max_delta_time;
    lua_pushboolean(lua_state, 1);
    return 1;
}

static int lua_time_fixed_delta_time(lua_State *lua_state) {
    ScriptRuntime *runtime = lua_runtime_instance(lua_state);
    lua_pushnumber(lua_state, runtime ? runtime->time_fixed_delta_time : 0.0f);
    return 1;
}

static int lua_time_set_fixed_delta_time(lua_State *lua_state) {
    ScriptRuntime *runtime = lua_runtime_instance(lua_state);
    if (!runtime) {
        lua_pushboolean(lua_state, 0);
        return 1;
    }

    float fixed_delta_time = (float)luaL_checknumber(lua_state, 1);
    if (fixed_delta_time < 0.0f)
        fixed_delta_time = 0.0f;

    runtime->time_fixed_delta_time = fixed_delta_time;
    lua_pushboolean(lua_state, 1);
    return 1;
}

static int lua_time_is_paused(lua_State *lua_state) {
    ScriptRuntime *runtime = lua_runtime_instance(lua_state);
    lua_pushboolean(lua_state, (runtime && runtime->time_paused) ? 1 : 0);
    return 1;
}

static int lua_time_set_paused(lua_State *lua_state) {
    ScriptRuntime *runtime = lua_runtime_instance(lua_state);
    if (!runtime) {
        lua_pushboolean(lua_state, 0);
        return 1;
    }

    runtime->time_paused = lua_toboolean(lua_state, 1) ? True : False;
    lua_pushboolean(lua_state, 1);
    return 1;
}

static int lua_time_pause(lua_State *lua_state) {
    ScriptRuntime *runtime = lua_runtime_instance(lua_state);
    if (!runtime) {
        lua_pushboolean(lua_state, 0);
        return 1;
    }

    runtime->time_paused = True;
    lua_pushboolean(lua_state, 1);
    return 1;
}

static int lua_time_resume(lua_State *lua_state) {
    ScriptRuntime *runtime = lua_runtime_instance(lua_state);
    if (!runtime) {
        lua_pushboolean(lua_state, 0);
        return 1;
    }

    runtime->time_paused = False;
    lua_pushboolean(lua_state, 1);
    return 1;
}

static int lua_time_seconds(lua_State *lua_state) {
    lua_pushnumber(lua_state, luaL_checknumber(lua_state, 1));
    return 1;
}

static int lua_time_milliseconds(lua_State *lua_state) {
    const lua_Number milliseconds = luaL_checknumber(lua_state, 1);
    lua_pushnumber(lua_state, milliseconds / 1000.0);
    return 1;
}

static int lua_time_minutes(lua_State *lua_state) {
    const lua_Number minutes = luaL_checknumber(lua_state, 1);
    lua_pushnumber(lua_state, minutes * 60.0);
    return 1;
}

static int lua_time_hours(lua_State *lua_state) {
    const lua_Number hours = luaL_checknumber(lua_state, 1);
    lua_pushnumber(lua_state, hours * 3600.0);
    return 1;
}

static int lua_time_clamp(lua_State *lua_state) {
    lua_Number value = luaL_checknumber(lua_state, 1);
    lua_Number min_value = luaL_checknumber(lua_state, 2);
    lua_Number max_value = luaL_checknumber(lua_state, 3);

    if (min_value > max_value) {
        const lua_Number swap = min_value;
        min_value = max_value;
        max_value = swap;
    }

    if (value < min_value)
        value = min_value;
    if (value > max_value)
        value = max_value;

    lua_pushnumber(lua_state, value);
    return 1;
}

static int lua_time_lerp(lua_State *lua_state) {
    const lua_Number start_value = luaL_checknumber(lua_state, 1);
    const lua_Number end_value = luaL_checknumber(lua_state, 2);
    lua_Number alpha = luaL_optnumber(lua_state, 3, 0.0);

    if (alpha < 0.0)
        alpha = 0.0;
    if (alpha > 1.0)
        alpha = 1.0;

    lua_pushnumber(lua_state, start_value + (end_value - start_value) * alpha);
    return 1;
}

static int lua_time_move_towards(lua_State *lua_state) {
    lua_Number current = luaL_checknumber(lua_state, 1);
    const lua_Number target = luaL_checknumber(lua_state, 2);
    lua_Number max_delta = luaL_checknumber(lua_state, 3);

    if (max_delta < 0.0)
        max_delta = -max_delta;

    const lua_Number distance = target - current;
    if (distance > max_delta)
        current += max_delta;
    else if (distance < -max_delta)
        current -= max_delta;
    else
        current = target;

    lua_pushnumber(lua_state, current);
    return 1;
}

static const luaL_Reg time_methods[] = {
    {"delta_time", lua_time_delta_time},
    {"unscaled_delta_time", lua_time_unscaled_delta_time},
    {"elapsed_time", lua_time_elapsed_time},
    {"unscaled_elapsed_time", lua_time_unscaled_elapsed_time},
    {"since_startup", lua_time_since_startup},
    {"fps", lua_time_fps},
    {"frame_count", lua_time_frame_count},
    {"time_scale", lua_time_time_scale},
    {"set_time_scale", lua_time_set_time_scale},
    {"max_delta_time", lua_time_max_delta_time},
    {"set_max_delta_time", lua_time_set_max_delta_time},
    {"fixed_delta_time", lua_time_fixed_delta_time},
    {"set_fixed_delta_time", lua_time_set_fixed_delta_time},
    {"is_paused", lua_time_is_paused},
    {"set_paused", lua_time_set_paused},
    {"pause", lua_time_pause},
    {"resume", lua_time_resume},
    {"seconds", lua_time_seconds},
    {"milliseconds", lua_time_milliseconds},
    {"minutes", lua_time_minutes},
    {"hours", lua_time_hours},
    {"clamp", lua_time_clamp},
    {"lerp", lua_time_lerp},
    {"move_towards", lua_time_move_towards},
    {NULL, NULL},
};

int luaopen_engine_time(lua_State *lua_state) {
    lua_newtable(lua_state);
    luaL_setfuncs(lua_state, time_methods, 0);
    return 1;
}
