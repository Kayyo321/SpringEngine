#include "shader_config.h"

#include "tomlc17.h"
#include "vfs.h"

#include <stdio.h>
#include <string.h>

static result join_path(const char *base, const char *path, char *out_path, usize out_size) {
    return vfs_resolve_path(base, path, out_path, out_size);
}

static result copy_text(char *out_text, usize out_size, const char *text) {
    if (!out_text || out_size == 0 || !text || text[0] == '\0')
        return Err;

    if (snprintf(out_text, out_size, "%s", text) >= (int)out_size)
        return Err;

    return Ok;
}

void shader_global_config_reset(ShaderGlobalConfig *config) {
    if (!config)
        return;

    memset(config, 0, sizeof(*config));
}

static result shader_apply_defaults(const char *project_root, ShaderGlobalConfig *config) {
    if (!project_root || !config)
        return Err;

    shader_global_config_reset(config);

    config->loaded = True;
    config->allow_compile_fallback = True;
    config->allow_expensive_post = True;
    config->max_variants_per_shader = 64;

    if (copy_text(config->active_profile, sizeof(config->active_profile), "default") != Ok)
        return Err;

    if (copy_text(config->default_sprite_material, sizeof(config->default_sprite_material), "default_sprite.mat.conf") != Ok)
        return Err;

    if (copy_text(config->default_ui_material, sizeof(config->default_ui_material), "default_ui.mat.conf") != Ok)
        return Err;

    if (copy_text(config->default_post_stack, sizeof(config->default_post_stack), "default.postfx.conf") != Ok)
        return Err;

    if (join_path(project_root, "./shaders", config->shader_root, sizeof(config->shader_root)) != Ok)
        return Err;

    if (join_path(project_root, "./materials", config->material_root, sizeof(config->material_root)) != Ok)
        return Err;

    return Ok;
}

static result resolve_global_shader_config_path(const char *project_root, char *out_path, usize out_size, boolean *out_exists) {
    if (!project_root || !out_path || out_size == 0 || !out_exists)
        return Err;

    *out_exists = False;

    char dashed_path[PATH_MAX] = {0};
    if (join_path(project_root, "global-shaders.conf", dashed_path, sizeof(dashed_path)) != Ok)
        return Err;

    if (vfs_file_exists(dashed_path) == True) {
        if (snprintf(out_path, out_size, "%s", dashed_path) >= (int)out_size)
            return Err;

        *out_exists = True;
        return Ok;
    }

    char dotted_path[PATH_MAX] = {0};
    if (join_path(project_root, "global.shaders.conf", dotted_path, sizeof(dotted_path)) != Ok)
        return Err;

    if (vfs_file_exists(dotted_path) == True) {
        if (snprintf(out_path, out_size, "%s", dotted_path) >= (int)out_size)
            return Err;

        *out_exists = True;
        return Ok;
    }

    out_path[0] = '\0';
    return Ok;
}

result shader_load_global_config(const char *project_root, ShaderGlobalConfig *out_config) {
    if (!project_root || !out_config)
        return Err;

    if (shader_apply_defaults(project_root, out_config) != Ok) {
        log_err("Failed to initialize default shader config for project '%s'", project_root);
        return Err;
    }

    boolean config_exists = False;
    if (resolve_global_shader_config_path(project_root, out_config->config_file_path, sizeof(out_config->config_file_path), &config_exists) != Ok) {
        log_err("Shader config path is too long for project '%s'", project_root);
        return Err;
    }

    out_config->config_file_present = config_exists;

    if (!config_exists) {
        log_msg("Optional shader config not found; using built-in defaults");
        return Ok;
    }

    toml_result_t parsed = {0};
    if (vfs_parse_toml_file(out_config->config_file_path, &parsed) != Ok) {
        log_err("Failed to parse shader config '%s'", out_config->config_file_path);
        return Err;
    }

    toml_datum_t global_table = toml_get(parsed.toptab, "ShaderGlobal");
    if (global_table.type != TOML_TABLE) {
        log_err("Shader config '%s' is missing [ShaderGlobal]", out_config->config_file_path);
        toml_free(parsed);
        return Err;
    }

    toml_datum_t allow_compile_fallback = toml_get(global_table, "allow_compile_fallback");
    if (allow_compile_fallback.type == TOML_BOOLEAN)
        out_config->allow_compile_fallback = allow_compile_fallback.u.boolean ? True : False;

    toml_datum_t active_profile = toml_get(global_table, "active_profile");
    if (active_profile.type == TOML_STRING && active_profile.u.s && active_profile.u.s[0] != '\0') {
        if (copy_text(out_config->active_profile, sizeof(out_config->active_profile), active_profile.u.s) != Ok) {
            log_err("ShaderGlobal.active_profile is too long in '%s'", out_config->config_file_path);
            toml_free(parsed);
            return Err;
        }
    }

    toml_datum_t paths_table = toml_get(global_table, "Paths");
    if (paths_table.type == TOML_TABLE) {
        toml_datum_t shader_dir = toml_get(paths_table, "shader_dir");
        if (shader_dir.type == TOML_STRING && shader_dir.u.s && shader_dir.u.s[0] != '\0') {
            if (join_path(project_root, shader_dir.u.s, out_config->shader_root, sizeof(out_config->shader_root)) != Ok) {
                log_err("ShaderGlobal.Paths.shader_dir is too long in '%s'", out_config->config_file_path);
                toml_free(parsed);
                return Err;
            }
        }

        toml_datum_t material_dir = toml_get(paths_table, "material_dir");
        if (material_dir.type == TOML_STRING && material_dir.u.s && material_dir.u.s[0] != '\0') {
            if (join_path(project_root, material_dir.u.s, out_config->material_root, sizeof(out_config->material_root)) != Ok) {
                log_err("ShaderGlobal.Paths.material_dir is too long in '%s'", out_config->config_file_path);
                toml_free(parsed);
                return Err;
            }
        }
    }

    toml_datum_t defaults_table = toml_get(global_table, "Defaults");
    if (defaults_table.type == TOML_TABLE) {
        toml_datum_t sprite_material = toml_get(defaults_table, "sprite_material");
        if (sprite_material.type == TOML_STRING && sprite_material.u.s && sprite_material.u.s[0] != '\0') {
            if (copy_text(out_config->default_sprite_material, sizeof(out_config->default_sprite_material), sprite_material.u.s) != Ok) {
                log_err("ShaderGlobal.Defaults.sprite_material is too long in '%s'", out_config->config_file_path);
                toml_free(parsed);
                return Err;
            }
        }

        toml_datum_t ui_material = toml_get(defaults_table, "ui_material");
        if (ui_material.type == TOML_STRING && ui_material.u.s && ui_material.u.s[0] != '\0') {
            if (copy_text(out_config->default_ui_material, sizeof(out_config->default_ui_material), ui_material.u.s) != Ok) {
                log_err("ShaderGlobal.Defaults.ui_material is too long in '%s'", out_config->config_file_path);
                toml_free(parsed);
                return Err;
            }
        }

        toml_datum_t post_stack = toml_get(defaults_table, "post_stack");
        if (post_stack.type == TOML_STRING && post_stack.u.s && post_stack.u.s[0] != '\0') {
            if (copy_text(out_config->default_post_stack, sizeof(out_config->default_post_stack), post_stack.u.s) != Ok) {
                log_err("ShaderGlobal.Defaults.post_stack is too long in '%s'", out_config->config_file_path);
                toml_free(parsed);
                return Err;
            }
        }
    }

    toml_datum_t profiles = toml_get(global_table, "Profiles");
    if (profiles.type == TOML_ARRAY) {
        for (int index = 0; index < profiles.u.arr.size; ++index) {
            toml_datum_t profile = profiles.u.arr.elem[index];
            if (profile.type != TOML_TABLE)
                continue;

            toml_datum_t id = toml_get(profile, "id");
            if (id.type != TOML_STRING || !id.u.s || id.u.s[0] == '\0')
                continue;

            if (strcmp(id.u.s, out_config->active_profile) != 0)
                continue;

            toml_datum_t max_variants = toml_get(profile, "max_variants_per_shader");
            if (max_variants.type == TOML_INT64 && max_variants.u.int64 > 0)
                out_config->max_variants_per_shader = (int)max_variants.u.int64;

            toml_datum_t allow_expensive_post = toml_get(profile, "allow_expensive_post");
            if (allow_expensive_post.type == TOML_BOOLEAN)
                out_config->allow_expensive_post = allow_expensive_post.u.boolean ? True : False;

            break;
        }
    }

    toml_free(parsed);
    return Ok;
}
