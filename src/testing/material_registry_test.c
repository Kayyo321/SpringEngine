#include "testing.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "config/shader_config.h"
#include "config/shader_registry.h"
#include "config/material_registry.h"

#include "common.h"

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

static result mr_create_temp_dir(char *path_template, const char **out_temp_dir) {
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

static result mr_ensure_dir(const char *path) {
    if (!path)
        return Err;

    struct stat st = {0};
    if (stat(path, &st) == 0)
        return S_ISDIR(st.st_mode) ? Ok : Err;

    return mkdir(path, 0755) == 0 ? Ok : Err;
}

static result mr_write_text_file(const char *path, const char *content) {
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

static void mr_remove_tree(const char *path) {
    if (!path || path[0] == '\0')
        return;

    char command[1024] = {0};
    if (snprintf(command, sizeof(command), "rm -rf '%s'", path) >= (int)sizeof(command))
        return;

    (void)system(command);
}

// ShaderLibrary (~35 MB) and MaterialLibrary (~18 MB) must live in static
// storage rather than on the stack to avoid stack overflow.
#ifdef Testing
static ShaderLibrary s_mr_shader_lib;
static MaterialLibrary s_mat_lib;
#endif

void run_material_registry_tests(void) {
#ifdef Testing
    usize failed = 0;

    log_msg("Running material registry tests...");

    // Null guards for material_library_load
    ShaderGlobalConfig cfg = {0};
    memset(&s_mat_lib, 0, sizeof(s_mat_lib));
    memset(&s_mr_shader_lib, 0, sizeof(s_mr_shader_lib));

    if (material_library_load(Null, &s_mr_shader_lib, &s_mat_lib) != Err) {
        log_err("material_library_load should reject null global_config");
        ++failed;
    }
    if (material_library_load(&cfg, Null, &s_mat_lib) != Err) {
        log_err("material_library_load should reject null shader_library");
        ++failed;
    }
    if (material_library_load(&cfg, &s_mr_shader_lib, Null) != Err) {
        log_err("material_library_load should reject null out_library");
        ++failed;
    }

    // material_library_reset: null guard should not crash
    material_library_reset(Null);

    // material_library_reset: should zero all fields
    s_mat_lib.loaded = True;
    s_mat_lib.material_count = 3;
    material_library_reset(&s_mat_lib);
    if (s_mat_lib.loaded != False || s_mat_lib.material_count != 0) {
        log_err("material_library_reset should zero all fields");
        ++failed;
    }

    // material_library_find_by_id: null guards
    if (material_library_find_by_id(Null, "some_mat") != Null) {
        log_err("material_library_find_by_id should return null for null library");
        ++failed;
    }
    if (material_library_find_by_id(&s_mat_lib, Null) != Null) {
        log_err("material_library_find_by_id should return null for null material_id");
        ++failed;
    }
    if (material_library_find_by_id(&s_mat_lib, "") != Null) {
        log_err("material_library_find_by_id should return null for empty material_id");
        ++failed;
    }

    // material_descriptor_find_texture_slot: null descriptor guard
    if (material_descriptor_find_texture_slot(Null, "main") != Null) {
        log_err("material_descriptor_find_texture_slot should return null for null descriptor");
        ++failed;
    }

    char temp_dir_template[] = "/tmp/springengine-material-XXXXXX";
    const char *temp_dir = Null;
    if (mr_create_temp_dir(temp_dir_template, &temp_dir) != Ok || !temp_dir) {
        log_err("temp directory creation failed for material registry tests");
        ++failed;
    } else {
        char materials_dir[PATH_MAX] = {0};
        if (snprintf(materials_dir, sizeof(materials_dir), "%s/materials", temp_dir) >= (int)sizeof(materials_dir)) {
            log_err("materials dir path too long in material registry tests");
            ++failed;
        } else {
            ShaderGlobalConfig global_cfg = {0};
            if (snprintf(global_cfg.material_root, sizeof(global_cfg.material_root), "%s", materials_dir) >= (int)sizeof(global_cfg.material_root)) {
                log_err("material_root path too long in material registry tests");
                ++failed;
            } else {
                // Populate stub shader library with one known shader id
                memset(&s_mr_shader_lib, 0, sizeof(s_mr_shader_lib));
                s_mr_shader_lib.loaded = True;
                s_mr_shader_lib.shader_count = 1;
                (void)snprintf(s_mr_shader_lib.shaders[0].id, sizeof(s_mr_shader_lib.shaders[0].id), "test_shader");

                // Missing materials dir -> loaded=True, material_count=0
                {
                    const usize w = get_warn_count();
                    const usize e = get_error_count();
                    memset(&s_mat_lib, 0, sizeof(s_mat_lib));
                    if (material_library_load(&global_cfg, &s_mr_shader_lib, &s_mat_lib) != Ok) {
                        log_err("material_library_load should succeed with empty library when materials dir is missing");
                        ++failed;
                    }
                    restore_diagnostic_counts(w, e);
                    if (s_mat_lib.loaded != True) {
                        log_err("material library with missing dir should have loaded = True");
                        ++failed;
                    }
                    if (s_mat_lib.material_count != 0) {
                        log_err("material library with missing dir should have material_count = 0");
                        ++failed;
                    }
                }

                if (mr_ensure_dir(materials_dir) != Ok) {
                    log_err("failed to create materials subdirectory for material registry tests");
                    ++failed;
                } else {
                    char mat_path[PATH_MAX] = {0};
                    char mat_path2[PATH_MAX] = {0};
                    if (snprintf(mat_path, sizeof(mat_path), "%s/test.mat.conf", materials_dir) >= (int)sizeof(mat_path) ||
                        snprintf(mat_path2, sizeof(mat_path2), "%s/test2.mat.conf", materials_dir) >= (int)sizeof(mat_path2)) {
                        log_err("material file paths too long in material registry tests");
                        ++failed;
                    } else {
                        // Valid material with texture slots -> loaded=True, count=1
                        mr_write_text_file(mat_path,
                            "[Material]\n"
                            "schema = 1\n"
                            "id = \"test_material\"\n"
                            "shader = \"test_shader\"\n"
                            "\n"
                            "[Material.Textures]\n"
                            "main = \"sprites/hero.png\"\n"
                            "normal = \"sprites/hero_normal.png\"\n");

                        memset(&s_mat_lib, 0, sizeof(s_mat_lib));
                        if (material_library_load(&global_cfg, &s_mr_shader_lib, &s_mat_lib) != Ok) {
                            log_err("material_library_load should succeed with a valid material file");
                            ++failed;
                        } else {
                            if (s_mat_lib.loaded != True) {
                                log_err("loaded material library should have loaded = True");
                                ++failed;
                            }
                            if (s_mat_lib.material_count != 1) {
                                log_err("material library should have exactly 1 material after loading one valid file");
                                ++failed;
                            }
                            if (strcmp(s_mat_lib.materials[0].id, "test_material") != 0) {
                                log_err("loaded material id should match 'test_material'");
                                ++failed;
                            }
                            if (strcmp(s_mat_lib.materials[0].shader_id, "test_shader") != 0) {
                                log_err("loaded material shader_id should match 'test_shader'");
                                ++failed;
                            }
                            if (s_mat_lib.materials[0].texture_slot_count != 2) {
                                log_err("loaded material should have 2 texture slots");
                                ++failed;
                            }

                            // material_library_find_by_id: found
                            const MaterialDescriptor *found = material_library_find_by_id(&s_mat_lib, "test_material");
                            if (!found) {
                                log_err("material_library_find_by_id should find 'test_material'");
                                ++failed;
                            }

                            // material_library_find_by_id: not found
                            if (material_library_find_by_id(&s_mat_lib, "nonexistent") != Null) {
                                log_err("material_library_find_by_id should return null for unknown id");
                                ++failed;
                            }

                            // material_descriptor_find_texture_slot
                            if (found) {
                                const char *main_tex = material_descriptor_find_texture_slot(found, "main");
                                if (!main_tex || strcmp(main_tex, "sprites/hero.png") != 0) {
                                    log_err("material_descriptor_find_texture_slot should return correct path for 'main' slot");
                                    ++failed;
                                }
                                if (material_descriptor_find_texture_slot(found, "diffuse") != Null) {
                                    log_err("material_descriptor_find_texture_slot should return null for unknown slot");
                                    ++failed;
                                }
                                if (material_descriptor_find_texture_slot(found, Null) != Null) {
                                    log_err("material_descriptor_find_texture_slot should return null for null slot name");
                                    ++failed;
                                }
                            }
                        }

                        // Missing [Material] table -> Err
                        mr_write_text_file(mat_path,
                            "[Boot]\n"
                            "first_scene = \"starting_scene.scene.conf\"\n");
                        {
                            const usize w = get_warn_count();
                            const usize e = get_error_count();
                            memset(&s_mat_lib, 0, sizeof(s_mat_lib));
                            if (material_library_load(&global_cfg, &s_mr_shader_lib, &s_mat_lib) != Err) {
                                log_err("material_library_load should fail when [Material] table is missing");
                                ++failed;
                            }
                            restore_diagnostic_counts(w, e);
                        }

                        // Missing Material.id -> Err
                        mr_write_text_file(mat_path,
                            "[Material]\n"
                            "shader = \"test_shader\"\n");
                        {
                            const usize w = get_warn_count();
                            const usize e = get_error_count();
                            memset(&s_mat_lib, 0, sizeof(s_mat_lib));
                            if (material_library_load(&global_cfg, &s_mr_shader_lib, &s_mat_lib) != Err) {
                                log_err("material_library_load should fail when Material.id is missing");
                                ++failed;
                            }
                            restore_diagnostic_counts(w, e);
                        }

                        // Missing Material.shader -> Err
                        mr_write_text_file(mat_path,
                            "[Material]\n"
                            "id = \"no_shader_material\"\n");
                        {
                            const usize w = get_warn_count();
                            const usize e = get_error_count();
                            memset(&s_mat_lib, 0, sizeof(s_mat_lib));
                            if (material_library_load(&global_cfg, &s_mr_shader_lib, &s_mat_lib) != Err) {
                                log_err("material_library_load should fail when Material.shader is missing");
                                ++failed;
                            }
                            restore_diagnostic_counts(w, e);
                        }

                        // Unknown shader reference -> Err
                        mr_write_text_file(mat_path,
                            "[Material]\n"
                            "id = \"orphan_material\"\n"
                            "shader = \"nonexistent_shader\"\n");
                        {
                            const usize w = get_warn_count();
                            const usize e = get_error_count();
                            memset(&s_mat_lib, 0, sizeof(s_mat_lib));
                            if (material_library_load(&global_cfg, &s_mr_shader_lib, &s_mat_lib) != Err) {
                                log_err("material_library_load should fail when shader id is not in the shader library");
                                ++failed;
                            }
                            restore_diagnostic_counts(w, e);
                        }

                        // Duplicate material id across two files -> Err
                        mr_write_text_file(mat_path,
                            "[Material]\n"
                            "id = \"dup_mat\"\n"
                            "shader = \"test_shader\"\n");
                        mr_write_text_file(mat_path2,
                            "[Material]\n"
                            "id = \"dup_mat\"\n"
                            "shader = \"test_shader\"\n");
                        {
                            const usize w = get_warn_count();
                            const usize e = get_error_count();
                            memset(&s_mat_lib, 0, sizeof(s_mat_lib));
                            if (material_library_load(&global_cfg, &s_mr_shader_lib, &s_mat_lib) != Err) {
                                log_err("material_library_load should fail for duplicate material id across files");
                                ++failed;
                            }
                            restore_diagnostic_counts(w, e);
                        }
                        (void)unlink(mat_path2);
                    }
                }
            }
        }

        mr_remove_tree(temp_dir);
    }

    record_test_result("Material registry tests", failed);
#endif // Testing
}
