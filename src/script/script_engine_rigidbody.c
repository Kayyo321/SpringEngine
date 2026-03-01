#include "script_runtime_internal.h"

#include <math.h>

static result lua_rigidbody_get_current(lua_State *lua_state, Actor **out_actor, RigidbodyComponentData **out_rigidbody) {
    if (!out_actor || !out_rigidbody)
        return Err;

    *out_actor = Null;
    *out_rigidbody = Null;

    Actor *actor = lua_runtime_current_actor(lua_state);
    if (!actor)
        return Err;

    RigidbodyComponentData *rigidbody = actor_find_rigidbody_component(actor);
    if (!rigidbody)
        return Err;

    *out_actor = actor;
    *out_rigidbody = rigidbody;
    return Ok;
}

static void rigidbody_refresh_inverse_mass(RigidbodyComponentData *rigidbody) {
    if (!rigidbody)
        return;

    if (rigidbody->mass <= 0.0f)
        rigidbody->mass = 1.0f;

    rigidbody->inverse_mass = 1.0f / rigidbody->mass;
}

static int rigidbody_force_mode_from_lua(lua_State *lua_state, int index) {
    if (lua_gettop(lua_state) < index)
        return 0;

    if (!lua_isstring(lua_state, index))
        return 0;

    const char *mode = lua_tostring(lua_state, index);
    if (!mode)
        return 0;

    if (strcmp(mode, "impulse") == 0 || strcmp(mode, "Impulse") == 0)
        return 1;

    return 0;
}

static int lua_rigidbody_set_body_type(lua_State *lua_state) {
    Actor *actor = Null;
    RigidbodyComponentData *rigidbody = Null;
    if (lua_rigidbody_get_current(lua_state, &actor, &rigidbody) != Ok)
        return 0;

    (void)actor;

    const char *value = luaL_checkstring(lua_state, 1);
    RigidbodyBodyType body_type = RigidbodyBodyTypeDynamic;
    if (rigidbody_body_type_from_string(value, &body_type) != Ok) {
        lua_pushboolean(lua_state, 0);
        return 1;
    }

    rigidbody->body_type = body_type;
    lua_pushboolean(lua_state, 1);
    return 1;
}

static int lua_rigidbody_get_body_type(lua_State *lua_state) {
    Actor *actor = Null;
    RigidbodyComponentData *rigidbody = Null;
    if (lua_rigidbody_get_current(lua_state, &actor, &rigidbody) != Ok) {
        lua_pushstring(lua_state, "dynamic");
        return 1;
    }

    lua_pushstring(lua_state, rigidbody_body_type_to_string(rigidbody->body_type));
    return 1;
}

static int lua_rigidbody_set_simulated(lua_State *lua_state) {
    Actor *actor = Null;
    RigidbodyComponentData *rigidbody = Null;
    if (lua_rigidbody_get_current(lua_state, &actor, &rigidbody) != Ok)
        return 0;

    (void)actor;

    rigidbody->simulated = lua_toboolean(lua_state, 1) ? True : False;
    return 0;
}

static int lua_rigidbody_get_simulated(lua_State *lua_state) {
    Actor *actor = Null;
    RigidbodyComponentData *rigidbody = Null;
    if (lua_rigidbody_get_current(lua_state, &actor, &rigidbody) != Ok) {
        lua_pushboolean(lua_state, 0);
        return 1;
    }

    lua_pushboolean(lua_state, rigidbody->simulated ? 1 : 0);
    return 1;
}

static int lua_rigidbody_set_use_gravity(lua_State *lua_state) {
    Actor *actor = Null;
    RigidbodyComponentData *rigidbody = Null;
    if (lua_rigidbody_get_current(lua_state, &actor, &rigidbody) != Ok)
        return 0;

    (void)actor;

    rigidbody->use_gravity = lua_toboolean(lua_state, 1) ? True : False;
    return 0;
}

static int lua_rigidbody_get_use_gravity(lua_State *lua_state) {
    Actor *actor = Null;
    RigidbodyComponentData *rigidbody = Null;
    if (lua_rigidbody_get_current(lua_state, &actor, &rigidbody) != Ok) {
        lua_pushboolean(lua_state, 0);
        return 1;
    }

    lua_pushboolean(lua_state, rigidbody->use_gravity ? 1 : 0);
    return 1;
}

static int lua_rigidbody_set_mass(lua_State *lua_state) {
    Actor *actor = Null;
    RigidbodyComponentData *rigidbody = Null;
    if (lua_rigidbody_get_current(lua_state, &actor, &rigidbody) != Ok)
        return 0;

    (void)actor;

    const float mass = (float)luaL_checknumber(lua_state, 1);
    if (mass <= 0.0f) {
        lua_pushboolean(lua_state, 0);
        return 1;
    }

    rigidbody->mass = mass;
    rigidbody_refresh_inverse_mass(rigidbody);
    lua_pushboolean(lua_state, 1);
    return 1;
}

static int lua_rigidbody_get_mass(lua_State *lua_state) {
    Actor *actor = Null;
    RigidbodyComponentData *rigidbody = Null;
    if (lua_rigidbody_get_current(lua_state, &actor, &rigidbody) != Ok) {
        lua_pushnumber(lua_state, 0.0);
        return 1;
    }

    lua_pushnumber(lua_state, rigidbody->mass);
    return 1;
}

static int lua_rigidbody_set_gravity_scale(lua_State *lua_state) {
    Actor *actor = Null;
    RigidbodyComponentData *rigidbody = Null;
    if (lua_rigidbody_get_current(lua_state, &actor, &rigidbody) != Ok)
        return 0;

    (void)actor;

    rigidbody->gravity_scale = (float)luaL_checknumber(lua_state, 1);
    return 0;
}

static int lua_rigidbody_get_gravity_scale(lua_State *lua_state) {
    Actor *actor = Null;
    RigidbodyComponentData *rigidbody = Null;
    if (lua_rigidbody_get_current(lua_state, &actor, &rigidbody) != Ok) {
        lua_pushnumber(lua_state, 0.0);
        return 1;
    }

    lua_pushnumber(lua_state, rigidbody->gravity_scale);
    return 1;
}

static int lua_rigidbody_set_linear_drag(lua_State *lua_state) {
    Actor *actor = Null;
    RigidbodyComponentData *rigidbody = Null;
    if (lua_rigidbody_get_current(lua_state, &actor, &rigidbody) != Ok)
        return 0;

    (void)actor;

    float drag = (float)luaL_checknumber(lua_state, 1);
    if (drag < 0.0f)
        drag = 0.0f;

    rigidbody->linear_drag = drag;
    return 0;
}

static int lua_rigidbody_get_linear_drag(lua_State *lua_state) {
    Actor *actor = Null;
    RigidbodyComponentData *rigidbody = Null;
    if (lua_rigidbody_get_current(lua_state, &actor, &rigidbody) != Ok) {
        lua_pushnumber(lua_state, 0.0);
        return 1;
    }

    lua_pushnumber(lua_state, rigidbody->linear_drag);
    return 1;
}

static int lua_rigidbody_set_angular_drag(lua_State *lua_state) {
    Actor *actor = Null;
    RigidbodyComponentData *rigidbody = Null;
    if (lua_rigidbody_get_current(lua_state, &actor, &rigidbody) != Ok)
        return 0;

    (void)actor;

    float drag = (float)luaL_checknumber(lua_state, 1);
    if (drag < 0.0f)
        drag = 0.0f;

    rigidbody->angular_drag = drag;
    return 0;
}

static int lua_rigidbody_get_angular_drag(lua_State *lua_state) {
    Actor *actor = Null;
    RigidbodyComponentData *rigidbody = Null;
    if (lua_rigidbody_get_current(lua_state, &actor, &rigidbody) != Ok) {
        lua_pushnumber(lua_state, 0.0);
        return 1;
    }

    lua_pushnumber(lua_state, rigidbody->angular_drag);
    return 1;
}

static int lua_rigidbody_set_velocity(lua_State *lua_state) {
    Actor *actor = Null;
    RigidbodyComponentData *rigidbody = Null;
    if (lua_rigidbody_get_current(lua_state, &actor, &rigidbody) != Ok)
        return 0;

    (void)actor;

    rigidbody->velocity_x = (float)luaL_checknumber(lua_state, 1);
    rigidbody->velocity_y = (float)luaL_checknumber(lua_state, 2);
    return 0;
}

static int lua_rigidbody_get_velocity(lua_State *lua_state) {
    Actor *actor = Null;
    RigidbodyComponentData *rigidbody = Null;
    if (lua_rigidbody_get_current(lua_state, &actor, &rigidbody) != Ok) {
        lua_pushnumber(lua_state, 0.0);
        lua_pushnumber(lua_state, 0.0);
        return 2;
    }

    lua_pushnumber(lua_state, rigidbody->velocity_x);
    lua_pushnumber(lua_state, rigidbody->velocity_y);
    return 2;
}

static int lua_rigidbody_set_angular_velocity(lua_State *lua_state) {
    Actor *actor = Null;
    RigidbodyComponentData *rigidbody = Null;
    if (lua_rigidbody_get_current(lua_state, &actor, &rigidbody) != Ok)
        return 0;

    (void)actor;

    rigidbody->angular_velocity = (float)luaL_checknumber(lua_state, 1);
    return 0;
}

static int lua_rigidbody_get_angular_velocity(lua_State *lua_state) {
    Actor *actor = Null;
    RigidbodyComponentData *rigidbody = Null;
    if (lua_rigidbody_get_current(lua_state, &actor, &rigidbody) != Ok) {
        lua_pushnumber(lua_state, 0.0);
        return 1;
    }

    lua_pushnumber(lua_state, rigidbody->angular_velocity);
    return 1;
}

static int lua_rigidbody_set_freeze_position(lua_State *lua_state) {
    Actor *actor = Null;
    RigidbodyComponentData *rigidbody = Null;
    if (lua_rigidbody_get_current(lua_state, &actor, &rigidbody) != Ok)
        return 0;

    (void)actor;

    rigidbody->freeze_position_x = lua_toboolean(lua_state, 1) ? True : False;
    rigidbody->freeze_position_y = lua_toboolean(lua_state, 2) ? True : False;
    return 0;
}

static int lua_rigidbody_get_freeze_position(lua_State *lua_state) {
    Actor *actor = Null;
    RigidbodyComponentData *rigidbody = Null;
    if (lua_rigidbody_get_current(lua_state, &actor, &rigidbody) != Ok) {
        lua_pushboolean(lua_state, 0);
        lua_pushboolean(lua_state, 0);
        return 2;
    }

    lua_pushboolean(lua_state, rigidbody->freeze_position_x ? 1 : 0);
    lua_pushboolean(lua_state, rigidbody->freeze_position_y ? 1 : 0);
    return 2;
}

static int lua_rigidbody_set_freeze_rotation(lua_State *lua_state) {
    Actor *actor = Null;
    RigidbodyComponentData *rigidbody = Null;
    if (lua_rigidbody_get_current(lua_state, &actor, &rigidbody) != Ok)
        return 0;

    (void)actor;

    rigidbody->freeze_rotation = lua_toboolean(lua_state, 1) ? True : False;
    return 0;
}

static int lua_rigidbody_get_freeze_rotation(lua_State *lua_state) {
    Actor *actor = Null;
    RigidbodyComponentData *rigidbody = Null;
    if (lua_rigidbody_get_current(lua_state, &actor, &rigidbody) != Ok) {
        lua_pushboolean(lua_state, 0);
        return 1;
    }

    lua_pushboolean(lua_state, rigidbody->freeze_rotation ? 1 : 0);
    return 1;
}

static int lua_rigidbody_add_force(lua_State *lua_state) {
    Actor *actor = Null;
    RigidbodyComponentData *rigidbody = Null;
    if (lua_rigidbody_get_current(lua_state, &actor, &rigidbody) != Ok)
        return 0;

    (void)actor;

    const float force_x = (float)luaL_checknumber(lua_state, 1);
    const float force_y = (float)luaL_checknumber(lua_state, 2);
    const int impulse_mode = rigidbody_force_mode_from_lua(lua_state, 3);

    if (impulse_mode) {
        rigidbody_refresh_inverse_mass(rigidbody);
        rigidbody->velocity_x += force_x * rigidbody->inverse_mass;
        rigidbody->velocity_y += force_y * rigidbody->inverse_mass;
    } else {
        rigidbody->force_x += force_x;
        rigidbody->force_y += force_y;
    }

    return 0;
}

static int lua_rigidbody_apply_force(lua_State *lua_state) {
    Actor *actor = Null;
    RigidbodyComponentData *rigidbody = Null;
    if (lua_rigidbody_get_current(lua_state, &actor, &rigidbody) != Ok)
        return 0;

    (void)actor;

    float dir_x = (float)luaL_checknumber(lua_state, 1);
    float dir_y = (float)luaL_checknumber(lua_state, 2);
    float magnitude = (float)luaL_checknumber(lua_state, 3);

    const float length = sqrtf((dir_x * dir_x) + (dir_y * dir_y));
    if (length <= 0.0001f || magnitude == 0.0f)
        return 0;

    dir_x /= length;
    dir_y /= length;

    const float force_x = dir_x * magnitude;
    const float force_y = dir_y * magnitude;

    rigidbody_refresh_inverse_mass(rigidbody);
    rigidbody->velocity_x += force_x * rigidbody->inverse_mass;
    rigidbody->velocity_y += force_y * rigidbody->inverse_mass;
    return 0;
}

static int lua_rigidbody_add_torque(lua_State *lua_state) {
    Actor *actor = Null;
    RigidbodyComponentData *rigidbody = Null;
    if (lua_rigidbody_get_current(lua_state, &actor, &rigidbody) != Ok)
        return 0;

    (void)actor;

    const float torque = (float)luaL_checknumber(lua_state, 1);
    const int impulse_mode = rigidbody_force_mode_from_lua(lua_state, 2);

    if (impulse_mode) {
        rigidbody_refresh_inverse_mass(rigidbody);
        rigidbody->angular_velocity += torque * rigidbody->inverse_mass;
    } else {
        rigidbody->torque += torque;
    }

    return 0;
}

static int lua_rigidbody_clear_forces(lua_State *lua_state) {
    Actor *actor = Null;
    RigidbodyComponentData *rigidbody = Null;
    if (lua_rigidbody_get_current(lua_state, &actor, &rigidbody) != Ok)
        return 0;

    (void)actor;

    rigidbody->force_x = 0.0f;
    rigidbody->force_y = 0.0f;
    rigidbody->torque = 0.0f;
    return 0;
}

static int lua_rigidbody_move_position(lua_State *lua_state) {
    Actor *actor = Null;
    RigidbodyComponentData *rigidbody = Null;
    if (lua_rigidbody_get_current(lua_state, &actor, &rigidbody) != Ok)
        return 0;

    actor->transform.position.x = (float)luaL_checknumber(lua_state, 1);
    actor->transform.position.y = (float)luaL_checknumber(lua_state, 2);

    if (lua_gettop(lua_state) >= 3)
        actor->transform.position.z = (float)luaL_checknumber(lua_state, 3);

    rigidbody->velocity_x = 0.0f;
    rigidbody->velocity_y = 0.0f;
    return 0;
}

static int lua_rigidbody_move_rotation(lua_State *lua_state) {
    Actor *actor = Null;
    RigidbodyComponentData *rigidbody = Null;
    if (lua_rigidbody_get_current(lua_state, &actor, &rigidbody) != Ok)
        return 0;

    actor->transform.rotation_euler.z = (float)luaL_checknumber(lua_state, 1);
    rigidbody->angular_velocity = 0.0f;
    return 0;
}

static int lua_rigidbody_is_grounded(lua_State *lua_state) {
    Actor *actor = Null;
    RigidbodyComponentData *rigidbody = Null;
    if (lua_rigidbody_get_current(lua_state, &actor, &rigidbody) != Ok) {
        lua_pushboolean(lua_state, 0);
        return 1;
    }

    lua_pushboolean(lua_state, rigidbody->is_grounded ? 1 : 0);
    return 1;
}

static const luaL_Reg rigidbody_methods[] = {
    {"set_body_type", lua_rigidbody_set_body_type},
    {"get_body_type", lua_rigidbody_get_body_type},
    {"set_simulated", lua_rigidbody_set_simulated},
    {"get_simulated", lua_rigidbody_get_simulated},
    {"set_use_gravity", lua_rigidbody_set_use_gravity},
    {"get_use_gravity", lua_rigidbody_get_use_gravity},
    {"set_mass", lua_rigidbody_set_mass},
    {"get_mass", lua_rigidbody_get_mass},
    {"set_gravity_scale", lua_rigidbody_set_gravity_scale},
    {"get_gravity_scale", lua_rigidbody_get_gravity_scale},
    {"set_linear_drag", lua_rigidbody_set_linear_drag},
    {"get_linear_drag", lua_rigidbody_get_linear_drag},
    {"set_angular_drag", lua_rigidbody_set_angular_drag},
    {"get_angular_drag", lua_rigidbody_get_angular_drag},
    {"set_velocity", lua_rigidbody_set_velocity},
    {"get_velocity", lua_rigidbody_get_velocity},
    {"set_angular_velocity", lua_rigidbody_set_angular_velocity},
    {"get_angular_velocity", lua_rigidbody_get_angular_velocity},
    {"set_freeze_position", lua_rigidbody_set_freeze_position},
    {"get_freeze_position", lua_rigidbody_get_freeze_position},
    {"set_freeze_rotation", lua_rigidbody_set_freeze_rotation},
    {"get_freeze_rotation", lua_rigidbody_get_freeze_rotation},
    {"add_force", lua_rigidbody_add_force},
    {"apply_force", lua_rigidbody_apply_force},
    {"add_torque", lua_rigidbody_add_torque},
    {"clear_forces", lua_rigidbody_clear_forces},
    {"move_position", lua_rigidbody_move_position},
    {"move_rotation", lua_rigidbody_move_rotation},
    {"is_grounded", lua_rigidbody_is_grounded},
    {NULL, NULL},
};

int luaopen_engine_rigidbody(lua_State *lua_state) {
    lua_newtable(lua_state);
    luaL_setfuncs(lua_state, rigidbody_methods, 0);
    return 1;
}
