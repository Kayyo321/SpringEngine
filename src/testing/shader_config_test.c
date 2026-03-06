#include "testing.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "config/shader_config.h"

#include "common.h"

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

static result sc_create_temp_dir(char *path_template, const char **out_temp_dir) {
    if (!path_template || !out_temp_dir)
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

static result sc_write_text_file(const char *path, const char *content) {
    FILE *file = fopen(path, "w");
    if (!file)
        return Err;

    if (fputs(content, file) == EOF) {
        fclose(file);
        return Err;
    }

    fclose(file);
    return Ok;
}

void run_shader_config_tests(void) {
#ifdef Testing
    usize failed = 0;

    log_msg("Running shader config tests...");

    // Null guards
    ShaderGlobalConfig config = {0};
    if (shader_load_global_config(Null, &config) != Err) {
        log_err("shader_load_global_config should reject null project_root");
        ++failed;
    }

    if (shader_load_global_config("/tmp", Null) != Err) {
        log_err("shader_load_global_config should reject null out_config");
        ++failed;
    }

    // shader_global_config_reset: null guard should not crash
    shader_global_config_reset(Null);

    // shader_global_config_reset: should zero all fields
    ShaderGlobalConfig to_reset = {0};
    to_reset.loaded = True;
    to_reset.max_variants_per_shader = 99;
    to_reset.allow_expensive_post = True;
    shader_global_config_reset(&to_reset);
    if (to_reset.loaded != False || to_reset.max_variants_per_shader != 0 || to_reset.allow_expensive_post != False) {
        log_err("shader_global_config_reset should zero all fields");
        ++failed;
    }

    char temp_dir_template[] = "/tmp/springengine-shader-cfg-XXXXXX";
    const char *temp_dir = Null;
    if (sc_create_temp_dir(temp_dir_template, &temp_dir) != Ok || !temp_dir) {
        log_err("temp directory creation failed for shader config tests");
        ++failed;
    } else {
        // No config file -> built-in defaults
        {
            ShaderGlobalConfig defaults = {0};
            if (shader_load_global_config(temp_dir, &defaults) != Ok) {
                log_err("shader_load_global_config should succeed with built-in defaults when no config file exists");
                ++failed;
            } else {
                if (defaults.loaded != True) {
                    log_err("defaults config should have loaded = True");
                    ++failed;
                }
                if (defaults.config_file_present != False) {
                    log_err("defaults config should have config_file_present = False when no file exists");
                    ++failed;
                }
                if (defaults.allow_compile_fallback != True) {
                    log_err("defaults config should set allow_compile_fallback = True");
                    ++failed;
                }
                if (defaults.allow_expensive_post != True) {
                    log_err("defaults config should set allow_expensive_post = True");
                    ++failed;
                }
                if (defaults.max_variants_per_shader != 64) {
                    log_err("defaults config should set max_variants_per_shader = 64");
                    ++failed;
                }
                if (strcmp(defaults.active_profile, "default") != 0) {
                    log_err("defaults config should set active_profile = \"default\"");
                    ++failed;
                }
                if (strcmp(defaults.default_sprite_material, "default_sprite.mat.conf") != 0) {
                    log_err("defaults config should set default_sprite_material = \"default_sprite.mat.conf\"");
                    ++failed;
                }
                if (strcmp(defaults.default_ui_material, "default_ui.mat.conf") != 0) {
                    log_err("defaults config should set default_ui_material = \"default_ui.mat.conf\"");
                    ++failed;
                }
                if (strcmp(defaults.default_post_stack, "default.postfx.conf") != 0) {
                    log_err("defaults config should set default_post_stack = \"default.postfx.conf\"");
                    ++failed;
                }
            }
        }

        char dashed_conf_path[PATH_MAX] = {0};
        char dotted_conf_path[PATH_MAX] = {0};
        if (snprintf(dashed_conf_path, sizeof(dashed_conf_path), "%s/global-shaders.conf", temp_dir) >= (int)sizeof(dashed_conf_path) ||
            snprintf(dotted_conf_path, sizeof(dotted_conf_path), "%s/global.shaders.conf", temp_dir) >= (int)sizeof(dotted_conf_path)) {
            log_err("config file paths too long in shader config tests");
            ++failed;
        } else {
            // Valid global-shaders.conf (dashed filename) with overrides and active profile
            sc_write_text_file(dashed_conf_path,
                "[ShaderGlobal]\n"
                "schema = 1\n"
                "active_profile = \"low\"\n"
                "allow_compile_fallback = false\n"
                "\n"
                "[ShaderGlobal.Paths]\n"
                "shader_dir = \"./shaders\"\n"
                "material_dir = \"./materials\"\n"
                "\n"
                "[ShaderGlobal.Defaults]\n"
                "sprite_material = \"custom_sprite.mat.conf\"\n"
                "ui_material = \"custom_ui.mat.conf\"\n"
                "post_stack = \"custom.postfx.conf\"\n"
                "\n"
                "[[ShaderGlobal.Profiles]]\n"
                "id = \"low\"\n"
                "max_variants_per_shader = 16\n"
                "allow_expensive_post = false\n");

            ShaderGlobalConfig loaded = {0};
            if (shader_load_global_config(temp_dir, &loaded) != Ok) {
                log_err("shader_load_global_config should parse a valid global-shaders.conf");
                ++failed;
            } else {
                if (loaded.loaded != True) {
                    log_err("loaded config should have loaded = True after successful parse");
                    ++failed;
                }
                if (loaded.config_file_present != True) {
                    log_err("loaded config should have config_file_present = True when file exists");
                    ++failed;
                }
                if (loaded.allow_compile_fallback != False) {
                    log_err("loaded config should respect allow_compile_fallback = false override");
                    ++failed;
                }
                if (strcmp(loaded.active_profile, "low") != 0) {
                    log_err("loaded config should respect active_profile = \"low\" override");
                    ++failed;
                }
                if (strcmp(loaded.default_sprite_material, "custom_sprite.mat.conf") != 0) {
                    log_err("loaded config should respect Defaults.sprite_material override");
                    ++failed;
                }
                if (strcmp(loaded.default_ui_material, "custom_ui.mat.conf") != 0) {
                    log_err("loaded config should respect Defaults.ui_material override");
                    ++failed;
                }
                if (strcmp(loaded.default_post_stack, "custom.postfx.conf") != 0) {
                    log_err("loaded config should respect Defaults.post_stack override");
                    ++failed;
                }
                // Active profile 'low' should apply max_variants_per_shader = 16, allow_expensive_post = false
                if (loaded.max_variants_per_shader != 16) {
                    log_err("loaded config should apply active profile max_variants_per_shader = 16");
                    ++failed;
                }
                if (loaded.allow_expensive_post != False) {
                    log_err("loaded config should apply active profile allow_expensive_post = false");
                    ++failed;
                }
            }

            // Dotted fallback: global.shaders.conf should be used when dashed file is absent
            (void)unlink(dashed_conf_path);
            sc_write_text_file(dotted_conf_path,
                "[ShaderGlobal]\n"
                "active_profile = \"default\"\n");

            ShaderGlobalConfig dotted_loaded = {0};
            if (shader_load_global_config(temp_dir, &dotted_loaded) != Ok) {
                log_err("shader_load_global_config should find global.shaders.conf when dashed file is absent");
                ++failed;
            } else {
                if (dotted_loaded.config_file_present != True) {
                    log_err("dotted fallback config should have config_file_present = True");
                    ++failed;
                }
            }
            (void)unlink(dotted_conf_path);

            // Invalid config: missing [ShaderGlobal] table -> Err
            sc_write_text_file(dashed_conf_path,
                "[Boot]\n"
                "first_scene = \"starting_scene.scene.conf\"\n");
            {
                ShaderGlobalConfig bad_config = {0};
                const usize w = get_warn_count();
                const usize e = get_error_count();
                if (shader_load_global_config(temp_dir, &bad_config) != Err) {
                    log_err("shader_load_global_config should fail when [ShaderGlobal] table is missing");
                    ++failed;
                }
                restore_diagnostic_counts(w, e);
            }

            (void)unlink(dashed_conf_path);
        }

        (void)rmdir(temp_dir);
    }

    record_test_result("Shader config tests", failed);
#endif // Testing
}
