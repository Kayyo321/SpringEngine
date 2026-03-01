#include "testing.h"

#include "actor/actor.h"
#include "config/static_sprite_component.h"
#include "script/script_runtime_internal.h"

#include "common.h"

#include <math.h>
#include <string.h>

static boolean nearly_equal(float lhs, float rhs, float tolerance) {
    return fabsf(lhs - rhs) <= tolerance ? True : False;
}

void run_script_runtime_tests(void) {
#ifdef TESTING
    usize failed = 0;

    log_msg("Running script runtime tests...");

    {
        char joined[64] = {0};
        if (runtime_join_path("project", "scripts/player.lua", joined, sizeof(joined)) != Ok || strcmp(joined, "project/scripts/player.lua") != 0) {
            log_err("runtime_join_path should join relative paths");
            ++failed;
        }

        if (runtime_join_path("project", "/absolute/path.lua", joined, sizeof(joined)) != Ok || strcmp(joined, "/absolute/path.lua") != 0) {
            log_err("runtime_join_path should preserve absolute paths");
            ++failed;
        }

        if (runtime_join_path(Null, "scripts/player.lua", joined, sizeof(joined)) != Err) {
            log_err("runtime_join_path should reject null base paths");
            ++failed;
        }
    }

    {
        void *state = script_component_state_create("scripts/player.lua", Null);
        if (!state) {
            log_err("script_component_state_create should allocate state for valid module path");
            ++failed;
        }

        script_component_state_dispose(state);

        if (script_component_state_create(Null, Null) != Null || script_component_state_create("", Null) != Null) {
            log_err("script_component_state_create should reject null/empty module paths");
            ++failed;
        }
    }

    {
        ScriptRuntime runtime = {0};
        runtime.time_scale = 2.0f;
        runtime.time_max_delta_time = 0.25f;
        runtime.time_fixed_delta_time = 0.0f;
        runtime.time_paused = False;

        float scaled = runtime_time_apply_rules(&runtime, 0.10f);
        if (!nearly_equal(scaled, 0.20f, 0.0001f)) {
            log_err("runtime_time_apply_rules should apply time_scale");
            ++failed;
        }

        float clamped = runtime_time_apply_rules(&runtime, 1.0f);
        if (!nearly_equal(clamped, 0.25f, 0.0001f)) {
            log_err("runtime_time_apply_rules should clamp by time_max_delta_time");
            ++failed;
        }

        runtime.time_fixed_delta_time = 0.016f;
        runtime.time_scale = 10.0f;
        float fixed = runtime_time_apply_rules(&runtime, 0.25f);
        if (!nearly_equal(fixed, 0.16f, 0.0001f)) {
            log_err("runtime_time_apply_rules should honor fixed delta before scaling");
            ++failed;
        }

        runtime.time_paused = True;
        if (!nearly_equal(runtime_time_apply_rules(&runtime, 0.25f), 0.0f, 0.0001f)) {
            log_err("runtime_time_apply_rules should return zero while paused");
            ++failed;
        }
    }

    {
        Actor actor = {0};
        actor.transform.position = (ActorVector3){2.0f, -1.5f, 9.0f};

        Vector3 world_position = actor_world_position(&actor);
        if (!nearly_equal(world_position.x, 2.0f, 0.0001f) || !nearly_equal(world_position.y, -1.5f, 0.0001f) || !nearly_equal(world_position.z, 9.0f, 0.0001f)) {
            log_err("actor_world_position should reflect actor transform");
            ++failed;
        }

        StaticSpriteState sprite = {0};
        sprite.position = (Vector2){4.0f, 3.0f};

        ActorComponent component = {
            .descriptor = {
                .name = "StaticSprite",
                .kind = ComponentBuiltin,
                .initialize = (ComponentInitializeFn)0,
                .context = Null,
            },
            .data = &sprite,
        };

        actor.components = &component;
        actor.component_count = 1;

        Vector3 anchor_position = actor_visual_anchor_position(&actor);
        if (!nearly_equal(anchor_position.x, 6.0f, 0.0001f) || !nearly_equal(anchor_position.y, 1.5f, 0.0001f) || !nearly_equal(anchor_position.z, 9.0f, 0.0001f)) {
            log_err("actor_visual_anchor_position should include static sprite offset");
            ++failed;
        }
    }

    {
        lua_State *lua_state = luaL_newstate();
        if (!lua_state) {
            log_err("luaL_newstate should create a Lua state");
            ++failed;
        } else {
            luaL_openlibs(lua_state);

            if (luaopen_engine(lua_state) != 1 || !lua_istable(lua_state, -1)) {
                log_err("luaopen_engine should return the Engine module table");
                ++failed;
            } else {
                lua_getfield(lua_state, -1, "version");
                if (!lua_isfunction(lua_state, -1)) {
                    log_err("Engine.version should be exposed as a function");
                    ++failed;
                }
                lua_pop(lua_state, 1);

                lua_getfield(lua_state, -1, "Time");
                if (!lua_istable(lua_state, -1)) {
                    log_err("Engine.Time should be exposed as a table");
                    ++failed;
                }
                lua_pop(lua_state, 1);

                lua_getfield(lua_state, -1, "UI");
                if (!lua_istable(lua_state, -1)) {
                    log_err("Engine.UI should be exposed as a table");
                    ++failed;
                } else {
                    const char *expected_ui_methods[] = {
                        "find",
                        "set_visible",
                        "set_text",
                        "push_document",
                        "pop_document",
                        "set_document_layer",
                        "bring_to_front",
                        "send_to_back",
                        "current_documents",
                    };

                    for (usize method_index = 0; method_index < (sizeof(expected_ui_methods) / sizeof(expected_ui_methods[0])); ++method_index) {
                        lua_getfield(lua_state, -1, expected_ui_methods[method_index]);
                        if (!lua_isfunction(lua_state, -1)) {
                            log_err("Engine.UI.%s should be exposed as a function", expected_ui_methods[method_index]);
                            ++failed;
                        }
                        lua_pop(lua_state, 1);
                    }
                }
                lua_pop(lua_state, 1);
            }
            lua_pop(lua_state, 1);

            Actor actor = {0};
            actor.id = "hero";

            Actor actors[1] = {actor};
            ActorRegistry registry = {
                .actors = actors,
                .actor_count = 1,
            };

            ScriptRuntime runtime = {
                .actor_registry = &registry,
            };

            lua_pushlightuserdata(lua_state, &runtime);
            lua_setfield(lua_state, LUA_REGISTRYINDEX, "__springengine_runtime");

            lua_runtime_set_current_actor(lua_state, &actors[0]);
            if (lua_runtime_current_actor(lua_state) != &actors[0]) {
                log_err("lua_runtime_set_current_actor should store the active actor");
                ++failed;
            }

            if (lua_runtime_find_actor(lua_state, "hero") != &actors[0]) {
                log_err("lua_runtime_find_actor should resolve actor by id");
                ++failed;
            }

            if (lua_runtime_find_actor(lua_state, "missing") != Null) {
                log_err("lua_runtime_find_actor should return null for unknown actor ids");
                ++failed;
            }

            lua_runtime_set_current_actor(lua_state, Null);
            if (lua_runtime_current_actor(lua_state) != Null) {
                log_err("lua_runtime_set_current_actor should clear actor when null is provided");
                ++failed;
            }

            lua_close(lua_state);
        }
    }

    record_test_result("Script runtime tests", failed);
#endif // TESTING
}
