#include "script_runtime_internal.h"

#include <ctype.h>

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

static result lua_camera_get_current(lua_State *lua_state, Actor **out_actor, CameraComponentData **out_camera) {
    if (!out_actor || !out_camera)
        return Err;

    *out_actor = Null;
    *out_camera = Null;

    Actor *actor = lua_runtime_current_actor(lua_state);
    if (!actor)
        return Err;

    CameraComponentData *camera = actor_find_camera_component(actor);
    if (!camera)
        return Err;

    *out_actor = actor;
    *out_camera = camera;
    return Ok;
}

static int lua_camera_set_position(lua_State *lua_state) {
    Actor *actor = Null;
    CameraComponentData *camera = Null;
    if (lua_camera_get_current(lua_state, &actor, &camera) != Ok)
        return 0;

    actor->transform.position.x = (float)luaL_checknumber(lua_state, 1);
    actor->transform.position.y = (float)luaL_checknumber(lua_state, 2);
    actor->transform.position.z = (float)luaL_checknumber(lua_state, 3);

    camera->camera.position = (Vector3){
        actor->transform.position.x,
        actor->transform.position.y,
        actor->transform.position.z,
    };

    return 0;
}

static int lua_camera_get_position(lua_State *lua_state) {
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

static int lua_camera_translate(lua_State *lua_state) {
    Actor *actor = Null;
    CameraComponentData *camera = Null;
    if (lua_camera_get_current(lua_state, &actor, &camera) != Ok)
        return 0;

    const float dx = (float)luaL_optnumber(lua_state, 1, 0.0);
    const float dy = (float)luaL_optnumber(lua_state, 2, 0.0);
    const float dz = (float)luaL_optnumber(lua_state, 3, 0.0);

    actor->transform.position.x += dx;
    actor->transform.position.y += dy;
    actor->transform.position.z += dz;

    camera->camera.position = (Vector3){
        actor->transform.position.x,
        actor->transform.position.y,
        actor->transform.position.z,
    };

    return 0;
}

static int lua_camera_set_target(lua_State *lua_state) {
    Actor *actor = Null;
    CameraComponentData *camera = Null;
    if (lua_camera_get_current(lua_state, &actor, &camera) != Ok)
        return 0;

    (void)actor;

    camera->camera.target.x = (float)luaL_checknumber(lua_state, 1);
    camera->camera.target.y = (float)luaL_checknumber(lua_state, 2);
    camera->camera.target.z = (float)luaL_checknumber(lua_state, 3);
    return 0;
}

static int lua_camera_get_target(lua_State *lua_state) {
    Actor *actor = Null;
    CameraComponentData *camera = Null;
    if (lua_camera_get_current(lua_state, &actor, &camera) != Ok) {
        lua_pushnumber(lua_state, 0.0);
        lua_pushnumber(lua_state, 0.0);
        lua_pushnumber(lua_state, 0.0);
        return 3;
    }

    lua_pushnumber(lua_state, camera->camera.target.x);
    lua_pushnumber(lua_state, camera->camera.target.y);
    lua_pushnumber(lua_state, camera->camera.target.z);
    return 3;
}

static int lua_camera_set_up(lua_State *lua_state) {
    Actor *actor = Null;
    CameraComponentData *camera = Null;
    if (lua_camera_get_current(lua_state, &actor, &camera) != Ok)
        return 0;

    (void)actor;

    camera->camera.up.x = (float)luaL_checknumber(lua_state, 1);
    camera->camera.up.y = (float)luaL_checknumber(lua_state, 2);
    camera->camera.up.z = (float)luaL_checknumber(lua_state, 3);
    return 0;
}

static int lua_camera_get_up(lua_State *lua_state) {
    Actor *actor = Null;
    CameraComponentData *camera = Null;
    if (lua_camera_get_current(lua_state, &actor, &camera) != Ok) {
        lua_pushnumber(lua_state, 0.0);
        lua_pushnumber(lua_state, 1.0);
        lua_pushnumber(lua_state, 0.0);
        return 3;
    }

    lua_pushnumber(lua_state, camera->camera.up.x);
    lua_pushnumber(lua_state, camera->camera.up.y);
    lua_pushnumber(lua_state, camera->camera.up.z);
    return 3;
}

static int lua_camera_set_fov_y(lua_State *lua_state) {
    Actor *actor = Null;
    CameraComponentData *camera = Null;
    if (lua_camera_get_current(lua_state, &actor, &camera) != Ok)
        return 0;

    (void)actor;

    camera->camera.fovy = (float)luaL_checknumber(lua_state, 1);
    return 0;
}

static int lua_camera_get_fov_y(lua_State *lua_state) {
    Actor *actor = Null;
    CameraComponentData *camera = Null;
    if (lua_camera_get_current(lua_state, &actor, &camera) != Ok) {
        lua_pushnumber(lua_state, 60.0);
        return 1;
    }

    lua_pushnumber(lua_state, camera->camera.fovy);
    return 1;
}

static int lua_camera_set_projection(lua_State *lua_state) {
    Actor *actor = Null;
    CameraComponentData *camera = Null;
    if (lua_camera_get_current(lua_state, &actor, &camera) != Ok)
        return 0;

    (void)actor;

    const char *projection_name = luaL_checkstring(lua_state, 1);
    if (!projection_name)
        return 0;

    if (string_equals_ignore_case(projection_name, "orthographic"))
        camera->camera.projection = CAMERA_ORTHOGRAPHIC;
    else
        camera->camera.projection = CAMERA_PERSPECTIVE;

    return 0;
}

static int lua_camera_get_projection(lua_State *lua_state) {
    Actor *actor = Null;
    CameraComponentData *camera = Null;
    if (lua_camera_get_current(lua_state, &actor, &camera) != Ok) {
        lua_pushstring(lua_state, "perspective");
        return 1;
    }

    if (camera->camera.projection == CAMERA_ORTHOGRAPHIC)
        lua_pushstring(lua_state, "orthographic");
    else
        lua_pushstring(lua_state, "perspective");
    return 1;
}

static int lua_camera_set_active(lua_State *lua_state) {
    Actor *actor = Null;
    CameraComponentData *camera = Null;
    if (lua_camera_get_current(lua_state, &actor, &camera) != Ok)
        return 0;

    (void)actor;

    camera->active = lua_toboolean(lua_state, 1) ? True : False;
    return 0;
}

static int lua_camera_get_active(lua_State *lua_state) {
    Actor *actor = Null;
    CameraComponentData *camera = Null;
    if (lua_camera_get_current(lua_state, &actor, &camera) != Ok) {
        lua_pushboolean(lua_state, 0);
        return 1;
    }

    lua_pushboolean(lua_state, camera->active ? 1 : 0);
    return 1;
}

static int lua_camera_set_clipping(lua_State *lua_state) {
    Actor *actor = Null;
    CameraComponentData *camera = Null;
    if (lua_camera_get_current(lua_state, &actor, &camera) != Ok)
        return 0;

    (void)actor;

    camera->near_clip = (float)luaL_checknumber(lua_state, 1);
    camera->far_clip = (float)luaL_checknumber(lua_state, 2);
    return 0;
}

static int lua_camera_get_clipping(lua_State *lua_state) {
    Actor *actor = Null;
    CameraComponentData *camera = Null;
    if (lua_camera_get_current(lua_state, &actor, &camera) != Ok) {
        lua_pushnumber(lua_state, 0.1);
        lua_pushnumber(lua_state, 1000.0);
        return 2;
    }

    lua_pushnumber(lua_state, camera->near_clip);
    lua_pushnumber(lua_state, camera->far_clip);
    return 2;
}

static int lua_camera_look_at_actor(lua_State *lua_state) {
    Actor *actor = Null;
    CameraComponentData *camera = Null;
    if (lua_camera_get_current(lua_state, &actor, &camera) != Ok)
        return 0;

    (void)actor;

    const char *target_actor_id = luaL_checkstring(lua_state, 1);
    Actor *target_actor = lua_runtime_find_actor(lua_state, target_actor_id);
    if (!target_actor)
        return 0;

    const Vector3 target_position = actor_world_position(target_actor);

    camera->camera.target = (Vector3){
        target_position.x,
        target_position.y,
        target_position.z,
    };

    return 0;
}

static int lua_camera_lerp_towards_actor(lua_State *lua_state) {
    Actor *actor = Null;
    CameraComponentData *camera = Null;
    if (lua_camera_get_current(lua_state, &actor, &camera) != Ok)
        return 0;

    const char *target_actor_id = luaL_checkstring(lua_state, 1);
    float alpha = (float)luaL_optnumber(lua_state, 2, 0.1);
    const float offset_x = (float)luaL_optnumber(lua_state, 3, 0.0);
    const float offset_y = (float)luaL_optnumber(lua_state, 4, 0.0);
    const float offset_z = (float)luaL_optnumber(lua_state, 5, 0.0);

    if (alpha < 0.0f)
        alpha = 0.0f;
    if (alpha > 1.0f)
        alpha = 1.0f;

    Actor *target_actor = lua_runtime_find_actor(lua_state, target_actor_id);
    if (!target_actor)
        return 0;

    const Vector3 target_position = actor_world_position(target_actor);

    const float desired_x = target_position.x + offset_x;
    const float desired_y = target_position.y + offset_y;
    const float desired_z = target_position.z + offset_z;

    actor->transform.position.x += (desired_x - actor->transform.position.x) * alpha;
    actor->transform.position.y += (desired_y - actor->transform.position.y) * alpha;
    actor->transform.position.z += (desired_z - actor->transform.position.z) * alpha;

    camera->camera.position = (Vector3){
        actor->transform.position.x,
        actor->transform.position.y,
        actor->transform.position.z,
    };
    camera->camera.target = (Vector3){
        target_position.x,
        target_position.y,
        target_position.z,
    };

    return 0;
}

static const luaL_Reg camera_methods[] = {
    {"set_position", lua_camera_set_position},
    {"get_position", lua_camera_get_position},
    {"translate", lua_camera_translate},
    {"set_target", lua_camera_set_target},
    {"get_target", lua_camera_get_target},
    {"set_up", lua_camera_set_up},
    {"get_up", lua_camera_get_up},
    {"set_fov_y", lua_camera_set_fov_y},
    {"get_fov_y", lua_camera_get_fov_y},
    {"set_projection", lua_camera_set_projection},
    {"get_projection", lua_camera_get_projection},
    {"set_active", lua_camera_set_active},
    {"get_active", lua_camera_get_active},
    {"set_clipping", lua_camera_set_clipping},
    {"get_clipping", lua_camera_get_clipping},
    {"look_at_actor", lua_camera_look_at_actor},
    {"lerp_towards_actor", lua_camera_lerp_towards_actor},
    {NULL, NULL},
};

int luaopen_engine_camera(lua_State *lua_state) {
    lua_newtable(lua_state);
    luaL_setfuncs(lua_state, camera_methods, 0);
    return 1;
}