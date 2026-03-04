#define CommonAllowStdlibAllocators
#include "testing.h"

#include "actor/actor.h"
#include "config/static_sprite_component.h"
#include "script/script_runtime_internal.h"

// CommonAllowStdlibAllocators needed to unmask definition of allocators in this library
#include <stdlib.h>

#include "common.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static result create_temp_test_dir(char *path_template, usize path_template_size, const char **out_temp_dir) {
    if (!path_template || !out_temp_dir || path_template_size == 0)
        return Err;

#if defined(__APPLE__)
    int temp_fd = mkstemp(path_template);
    if (temp_fd < 0)
        return Err;

    if (close(temp_fd) != 0)
        return Err;

    if (unlink(path_template) != 0)
        return Err;

    if (mkdir(path_template, 0700) != 0)
        return Err;

    *out_temp_dir = path_template;
    return Ok;
#else
    char *temp_dir = mkdtemp(path_template);
    if (!temp_dir)
        return Err;

    *out_temp_dir = temp_dir;
    return Ok;
#endif
}

static boolean nearly_equal(float lhs, float rhs, float tolerance) {
    return fabsf(lhs - rhs) <= tolerance ? True : False;
}

void run_script_runtime_tests(void) {
#ifdef Testing
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
        char temp_dir_template[] = "/tmp/springengine-script-runtime-XXXXXX";
        const char *temp_dir = Null;
        lua_State *lua_state = luaL_newstate();
        if (!lua_state) {
            log_err("luaL_newstate should create a Lua state");
            ++failed;
        } else if (create_temp_test_dir(temp_dir_template, sizeof(temp_dir_template), &temp_dir) != Ok || !temp_dir) {
            log_err("temp directory creation failed for script runtime tests");
            ++failed;
            lua_close(lua_state);
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

                lua_getfield(lua_state, -1, "Disk");
                if (!lua_istable(lua_state, -1)) {
                    log_err("Engine.Disk should be exposed as a table");
                    ++failed;
                } else {
                    const char *expected_disk_methods[] = {
                        "resolve",
                        "exists",
                        "read_text",
                        "write_text",
                        "append_text",
                        "save",
                        "ensure_directory",
                    };

                    for (usize method_index = 0; method_index < (sizeof(expected_disk_methods) / sizeof(expected_disk_methods[0])); ++method_index) {
                        lua_getfield(lua_state, -1, expected_disk_methods[method_index]);
                        if (!lua_isfunction(lua_state, -1)) {
                            log_err("Engine.Disk.%s should be exposed as a function", expected_disk_methods[method_index]);
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

            if (snprintf(runtime.project_root, sizeof(runtime.project_root), "%s", temp_dir) >= (int)sizeof(runtime.project_root)) {
                log_err("failed to compose runtime project root for Engine.Disk tests");
                ++failed;
            }

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

            {
                char expected_disk_path[512] = {0};
                char file_path[512] = {0};
                char unit_dir[512] = {0};
                char saves_dir[512] = {0};

                if (snprintf(expected_disk_path, sizeof(expected_disk_path), "%s/saves/unit/state.txt", temp_dir) >= (int)sizeof(expected_disk_path)
                    || snprintf(file_path, sizeof(file_path), "%s/saves/unit/state.txt", temp_dir) >= (int)sizeof(file_path)
                    || snprintf(unit_dir, sizeof(unit_dir), "%s/saves/unit", temp_dir) >= (int)sizeof(unit_dir)
                    || snprintf(saves_dir, sizeof(saves_dir), "%s/saves", temp_dir) >= (int)sizeof(saves_dir)) {
                    log_err("failed to compose Engine.Disk test paths");
                    ++failed;
                } else {
                    lua_pushstring(lua_state, expected_disk_path);
                    lua_setglobal(lua_state, "__expected_disk_path");

                    const char *disk_test_script =
                        "local Disk = require('Engine.Disk')\n"
                        "if Disk.exists('saves/unit/state.txt') then return false end\n"
                        "if not Disk.ensure_directory('saves/unit') then return false end\n"
                        "if not Disk.write_text('saves/unit/state.txt', 'hello') then return false end\n"
                        "if not Disk.exists('saves/unit/state.txt') then return false end\n"
                        "if Disk.read_text('saves/unit/state.txt') ~= 'hello' then return false end\n"
                        "if not Disk.append_text('saves/unit/state.txt', ' world') then return false end\n"
                        "if Disk.read_text('saves/unit/state.txt') ~= 'hello world' then return false end\n"
                        "if not Disk.save('saves/unit/state.txt', 'reset') then return false end\n"
                        "if Disk.read_text('saves/unit/state.txt') ~= 'reset' then return false end\n"
                        "if Disk.resolve('saves/unit/state.txt') ~= __expected_disk_path then return false end\n"
                        "if Disk.write_text('bad/../blocked.txt', 'nope') then return false end\n"
                        "return true\n";

                    if (luaL_dostring(lua_state, disk_test_script) != LUA_OK) {
                        const char *error_message = lua_tostring(lua_state, -1);
                        log_err("Engine.Disk Lua test script failed: %s", error_message ? error_message : "<unknown error>");
                        ++failed;
                        lua_pop(lua_state, 1);
                    } else {
                        const boolean disk_ok = lua_toboolean(lua_state, -1) ? True : False;
                        lua_pop(lua_state, 1);
                        if (!disk_ok) {
                            log_err("Engine.Disk integration behavior should pass write/read/append/save/resolve checks");
                            ++failed;
                        }
                    }
                }

                (void)unlink(file_path);
                (void)rmdir(unit_dir);
                (void)rmdir(saves_dir);
                (void)rmdir(temp_dir);
            }

            lua_close(lua_state);
        }
    }

    record_test_result("Script runtime tests", failed);
#endif // Testing
}
