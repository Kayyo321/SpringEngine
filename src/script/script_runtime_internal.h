#ifndef ScriptRuntimeInternalH
#define ScriptRuntimeInternalH

#include "script_runtime.h"

#include "actor/camera_component.h"
#include "actor/collider_component.h"
#include "actor/light_component.h"
#include "actor/rigidbody_component.h"
#include "config/runtime_loader.h"
#include "config/static_sprite_component.h"
#include "common.h"

#include "lauxlib.h"
#include "lualib.h"
#include "raylib.h"
#include "tomlc17.h"

#include <stdio.h>
#include <string.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

result runtime_join_path(const char *base, const char *path, char *out_path, usize out_size);

Actor *lua_runtime_current_actor(lua_State *lua_state);
void lua_runtime_set_current_actor(lua_State *lua_state, Actor *actor);
ScriptRuntime *lua_runtime_instance(lua_State *lua_state);
Actor *lua_runtime_find_actor(lua_State *lua_state, const char *actor_id);
Vector3 actor_visual_anchor_position(Actor *actor);
Vector3 actor_world_position(Actor *actor);

float runtime_time_apply_rules(ScriptRuntime *runtime, float raw_delta_time);

result load_input_config(const char *project_root);
void reset_input_config(void);

int luaopen_engine_input(lua_State *lua_state);
int luaopen_engine_time(lua_State *lua_state);
int luaopen_engine_transform(lua_State *lua_state);
int luaopen_engine_actor(lua_State *lua_state);
int luaopen_engine_camera(lua_State *lua_state);
int luaopen_engine_collider(lua_State *lua_state);
int luaopen_engine_rigidbody(lua_State *lua_state);
int luaopen_engine_scene(lua_State *lua_state);
int luaopen_engine_dj(lua_State *lua_state);
int luaopen_engine_ui(lua_State *lua_state);
int luaopen_engine_disk(lua_State *lua_state);
int luaopen_engine_async(lua_State *lua_state);
int luaopen_engine(lua_State *lua_state);

#endif
