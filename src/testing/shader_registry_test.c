#include "testing.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "config/shader_config.h"
#include "config/shader_registry.h"

#include "common.h"

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

static result sr_create_temp_dir(char *path_template, const char **out_temp_dir) {
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

static result sr_ensure_dir(const char *path) {
    if (!path)
        return Err;

    struct stat st = {0};
    if (stat(path, &st) == 0)
        return S_ISDIR(st.st_mode) ? Ok : Err;

    return mkdir(path, 0755) == 0 ? Ok : Err;
}

static result sr_write_text_file(const char *path, const char *content) {
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

static void sr_remove_tree(const char *path) {
    if (!path || path[0] == '\0')
        return;

    char command[1024] = {0};
    if (snprintf(command, sizeof(command), "rm -rf '%s'", path) >= (int)sizeof(command))
        return;

    (void)system(command);
}

// ShaderLibrary is ~35 MB; must live in static storage, not on the stack.
#ifdef Testing
static ShaderLibrary s_shader_lib;
#endif

void run_shader_registry_tests(void) {
#ifdef Testing
    usize failed = 0;

    log_msg("Running shader registry tests...");

    // Null guards
    ShaderGlobalConfig cfg = {0};
    memset(&s_shader_lib, 0, sizeof(s_shader_lib));
    if (shader_library_load(Null, &s_shader_lib) != Err) {
        log_err("shader_library_load should reject null global_config");
        ++failed;
    }
    if (shader_library_load(&cfg, Null) != Err) {
        log_err("shader_library_load should reject null out_library");
        ++failed;
    }

    // shader_library_reset: null guard should not crash
    shader_library_reset(Null);

    // shader_library_reset: should zero all fields
    s_shader_lib.loaded = True;
    s_shader_lib.shader_count = 7;
    shader_library_reset(&s_shader_lib);
    if (s_shader_lib.loaded != False || s_shader_lib.shader_count != 0) {
        log_err("shader_library_reset should zero all fields");
        ++failed;
    }

    char temp_dir_template[] = "/tmp/springengine-shader-reg-XXXXXX";
    const char *temp_dir = Null;
    if (sr_create_temp_dir(temp_dir_template, &temp_dir) != Ok || !temp_dir) {
        log_err("temp directory creation failed for shader registry tests");
        ++failed;
    } else {
        char shaders_dir[PATH_MAX] = {0};
        if (snprintf(shaders_dir, sizeof(shaders_dir), "%s/shaders", temp_dir) >= (int)sizeof(shaders_dir)) {
            log_err("shaders dir path too long in shader registry tests");
            ++failed;
        } else {
            ShaderGlobalConfig global_cfg = {0};
            if (snprintf(global_cfg.shader_root, sizeof(global_cfg.shader_root), "%s", shaders_dir) >= (int)sizeof(global_cfg.shader_root)) {
                log_err("shader_root path too long in shader registry tests");
                ++failed;
            } else {
                // Missing shader dir -> loaded=True, shader_count=0
                {
                    const usize w = get_warn_count();
                    const usize e = get_error_count();
                    memset(&s_shader_lib, 0, sizeof(s_shader_lib));
                    if (shader_library_load(&global_cfg, &s_shader_lib) != Ok) {
                        log_err("shader_library_load should succeed with empty library when shader dir is missing");
                        ++failed;
                    }
                    restore_diagnostic_counts(w, e);
                    if (s_shader_lib.loaded != True) {
                        log_err("shader library with missing dir should have loaded = True");
                        ++failed;
                    }
                    if (s_shader_lib.shader_count != 0) {
                        log_err("shader library with missing dir should have shader_count = 0");
                        ++failed;
                    }
                }

                if (sr_ensure_dir(shaders_dir) != Ok) {
                    log_err("failed to create shaders subdirectory for shader registry tests");
                    ++failed;
                } else {
                    char shader_path[PATH_MAX] = {0};
                    char shader_path2[PATH_MAX] = {0};
                    if (snprintf(shader_path, sizeof(shader_path), "%s/test.shader.conf", shaders_dir) >= (int)sizeof(shader_path) ||
                        snprintf(shader_path2, sizeof(shader_path2), "%s/test2.shader.conf", shaders_dir) >= (int)sizeof(shader_path2)) {
                        log_err("shader file paths too long in shader registry tests");
                        ++failed;
                    } else {
                        // Valid shader file -> loaded=True, count=1, fields verified
                        sr_write_text_file(shader_path,
                            "[Shader]\n"
                            "schema = 1\n"
                            "id = \"test_shader\"\n"
                            "vertex = \"test.vert.glsl\"\n"
                            "fragment = \"test.frag.glsl\"\n"
                            "\n"
                            "[Shader.States]\n"
                            "blend = \"alpha\"\n"
                            "cull = \"none\"\n"
                            "depth_test = false\n"
                            "depth_write = false\n"
                            "\n"
                            "[[Shader.Uniforms]]\n"
                            "name = \"u_time\"\n"
                            "type = \"float\"\n"
                            "\n"
                            "[[Shader.Keywords]]\n"
                            "name = \"USE_ALPHA\"\n");

                        memset(&s_shader_lib, 0, sizeof(s_shader_lib));
                        if (shader_library_load(&global_cfg, &s_shader_lib) != Ok) {
                            log_err("shader_library_load should succeed with a valid shader file");
                            ++failed;
                        } else {
                            if (s_shader_lib.loaded != True) {
                                log_err("loaded shader library should have loaded = True");
                                ++failed;
                            }
                            if (s_shader_lib.shader_count != 1) {
                                log_err("shader library should have exactly 1 shader after loading one valid file");
                                ++failed;
                            }
                            if (strcmp(s_shader_lib.shaders[0].id, "test_shader") != 0) {
                                log_err("loaded shader id should match 'test_shader'");
                                ++failed;
                            }
                            if (strcmp(s_shader_lib.shaders[0].vertex, "test.vert.glsl") != 0) {
                                log_err("loaded shader vertex should match 'test.vert.glsl'");
                                ++failed;
                            }
                            if (strcmp(s_shader_lib.shaders[0].fragment, "test.frag.glsl") != 0) {
                                log_err("loaded shader fragment should match 'test.frag.glsl'");
                                ++failed;
                            }
                            if (s_shader_lib.shaders[0].uniform_count != 1 ||
                                strcmp(s_shader_lib.shaders[0].uniforms[0].name, "u_time") != 0 ||
                                strcmp(s_shader_lib.shaders[0].uniforms[0].type, "float") != 0) {
                                log_err("loaded shader uniform fields should match the config");
                                ++failed;
                            }
                            if (s_shader_lib.shaders[0].keyword_count != 1 ||
                                strcmp(s_shader_lib.shaders[0].keywords[0].name, "USE_ALPHA") != 0) {
                                log_err("loaded shader keyword name should match 'USE_ALPHA'");
                                ++failed;
                            }
                        }

                        // Missing [Shader] table -> Err
                        sr_write_text_file(shader_path,
                            "[Boot]\n"
                            "first_scene = \"starting_scene.scene.conf\"\n");
                        {
                            const usize w = get_warn_count();
                            const usize e = get_error_count();
                            memset(&s_shader_lib, 0, sizeof(s_shader_lib));
                            if (shader_library_load(&global_cfg, &s_shader_lib) != Err) {
                                log_err("shader_library_load should fail when [Shader] table is missing");
                                ++failed;
                            }
                            restore_diagnostic_counts(w, e);
                        }

                        // Missing required id -> Err
                        sr_write_text_file(shader_path,
                            "[Shader]\n"
                            "vertex = \"test.vert.glsl\"\n"
                            "fragment = \"test.frag.glsl\"\n");
                        {
                            const usize w = get_warn_count();
                            const usize e = get_error_count();
                            memset(&s_shader_lib, 0, sizeof(s_shader_lib));
                            if (shader_library_load(&global_cfg, &s_shader_lib) != Err) {
                                log_err("shader_library_load should fail when Shader.id is missing");
                                ++failed;
                            }
                            restore_diagnostic_counts(w, e);
                        }

                        // Unsupported blend mode -> Err
                        sr_write_text_file(shader_path,
                            "[Shader]\n"
                            "id = \"bad_blend\"\n"
                            "vertex = \"test.vert.glsl\"\n"
                            "fragment = \"test.frag.glsl\"\n"
                            "\n"
                            "[Shader.States]\n"
                            "blend = \"multiply\"\n");
                        {
                            const usize w = get_warn_count();
                            const usize e = get_error_count();
                            memset(&s_shader_lib, 0, sizeof(s_shader_lib));
                            if (shader_library_load(&global_cfg, &s_shader_lib) != Err) {
                                log_err("shader_library_load should fail for unsupported blend mode");
                                ++failed;
                            }
                            restore_diagnostic_counts(w, e);
                        }

                        // Unsupported cull mode -> Err
                        sr_write_text_file(shader_path,
                            "[Shader]\n"
                            "id = \"bad_cull\"\n"
                            "vertex = \"test.vert.glsl\"\n"
                            "fragment = \"test.frag.glsl\"\n"
                            "\n"
                            "[Shader.States]\n"
                            "cull = \"left\"\n");
                        {
                            const usize w = get_warn_count();
                            const usize e = get_error_count();
                            memset(&s_shader_lib, 0, sizeof(s_shader_lib));
                            if (shader_library_load(&global_cfg, &s_shader_lib) != Err) {
                                log_err("shader_library_load should fail for unsupported cull mode");
                                ++failed;
                            }
                            restore_diagnostic_counts(w, e);
                        }

                        // Unsupported uniform type -> Err
                        sr_write_text_file(shader_path,
                            "[Shader]\n"
                            "id = \"bad_uniform\"\n"
                            "vertex = \"test.vert.glsl\"\n"
                            "fragment = \"test.frag.glsl\"\n"
                            "\n"
                            "[[Shader.Uniforms]]\n"
                            "name = \"u_val\"\n"
                            "type = \"matrix4x4\"\n");
                        {
                            const usize w = get_warn_count();
                            const usize e = get_error_count();
                            memset(&s_shader_lib, 0, sizeof(s_shader_lib));
                            if (shader_library_load(&global_cfg, &s_shader_lib) != Err) {
                                log_err("shader_library_load should fail for unsupported uniform type");
                                ++failed;
                            }
                            restore_diagnostic_counts(w, e);
                        }

                        // Duplicate uniform name -> Err
                        sr_write_text_file(shader_path,
                            "[Shader]\n"
                            "id = \"dup_uniform\"\n"
                            "vertex = \"test.vert.glsl\"\n"
                            "fragment = \"test.frag.glsl\"\n"
                            "\n"
                            "[[Shader.Uniforms]]\n"
                            "name = \"u_time\"\n"
                            "type = \"float\"\n"
                            "\n"
                            "[[Shader.Uniforms]]\n"
                            "name = \"u_time\"\n"
                            "type = \"float\"\n");
                        {
                            const usize w = get_warn_count();
                            const usize e = get_error_count();
                            memset(&s_shader_lib, 0, sizeof(s_shader_lib));
                            if (shader_library_load(&global_cfg, &s_shader_lib) != Err) {
                                log_err("shader_library_load should fail for duplicate uniform name");
                                ++failed;
                            }
                            restore_diagnostic_counts(w, e);
                        }

                        // Duplicate keyword name -> Err
                        sr_write_text_file(shader_path,
                            "[Shader]\n"
                            "id = \"dup_keyword\"\n"
                            "vertex = \"test.vert.glsl\"\n"
                            "fragment = \"test.frag.glsl\"\n"
                            "\n"
                            "[[Shader.Keywords]]\n"
                            "name = \"FOO\"\n"
                            "\n"
                            "[[Shader.Keywords]]\n"
                            "name = \"FOO\"\n");
                        {
                            const usize w = get_warn_count();
                            const usize e = get_error_count();
                            memset(&s_shader_lib, 0, sizeof(s_shader_lib));
                            if (shader_library_load(&global_cfg, &s_shader_lib) != Err) {
                                log_err("shader_library_load should fail for duplicate keyword name");
                                ++failed;
                            }
                            restore_diagnostic_counts(w, e);
                        }

                        // Duplicate shader id across two files -> Err
                        sr_write_text_file(shader_path,
                            "[Shader]\n"
                            "id = \"dup_id\"\n"
                            "vertex = \"a.vert.glsl\"\n"
                            "fragment = \"a.frag.glsl\"\n");
                        sr_write_text_file(shader_path2,
                            "[Shader]\n"
                            "id = \"dup_id\"\n"
                            "vertex = \"b.vert.glsl\"\n"
                            "fragment = \"b.frag.glsl\"\n");
                        {
                            const usize w = get_warn_count();
                            const usize e = get_error_count();
                            memset(&s_shader_lib, 0, sizeof(s_shader_lib));
                            if (shader_library_load(&global_cfg, &s_shader_lib) != Err) {
                                log_err("shader_library_load should fail for duplicate shader id across files");
                                ++failed;
                            }
                            restore_diagnostic_counts(w, e);
                        }
                        (void)unlink(shader_path2);
                    }
                }
            }
        }

        sr_remove_tree(temp_dir);
    }

    record_test_result("Shader registry tests", failed);
#endif // Testing
}
