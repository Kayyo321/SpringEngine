#include "lighting_config.h"
#include "vfs.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

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

static result join_path(const char *base, const char *path, char *out_path, usize out_size) {
    return vfs_resolve_path(base, path, out_path, out_size);
}

static result copy_name(char *out_name, usize out_size, const char *name) {
    if (!out_name || out_size == 0 || !name || name[0] == '\0')
        return Err;

    if (snprintf(out_name, out_size, "%s", name) >= (int)out_size)
        return Err;

    return Ok;
}

static result parse_rgb_triplet(toml_datum_t value, unsigned char *out_r, unsigned char *out_g, unsigned char *out_b) {
    if (value.type != TOML_ARRAY || value.u.arr.size < 3 || !out_r || !out_g || !out_b)
        return Err;

    int channels[3] = {0};
    for (int index = 0; index < 3; ++index) {
        toml_datum_t channel = value.u.arr.elem[index];
        if (channel.type != TOML_INT64 || channel.u.int64 < 0 || channel.u.int64 > 255)
            return Err;

        channels[index] = (int)channel.u.int64;
    }

    *out_r = (unsigned char)channels[0];
    *out_g = (unsigned char)channels[1];
    *out_b = (unsigned char)channels[2];
    return Ok;
}

void lighting_global_config_reset(LightingGlobalConfig *config) {
    if (!config)
        return;

    memset(config, 0, sizeof(*config));
}

void lighting_scene_selection_reset(LightingSceneSelection *selection) {
    if (!selection)
        return;

    memset(selection, 0, sizeof(*selection));
    selection->global_light_multiplier = 1.0f;
}

static const LightingSceneMapEntry *find_scene_map_entry(const LightingGlobalConfig *config, const char *scene_id) {
    if (!config || !scene_id || scene_id[0] == '\0')
        return Null;

    for (usize index = 0; index < config->scene_map_count; ++index) {
        const LightingSceneMapEntry *entry = &config->scene_map[index];
        if (string_equals_ignore_case(entry->scene_id, scene_id))
            return entry;
    }

    return Null;
}

result lighting_load_global_config(const char *project_root, LightingGlobalConfig *out_config) {
    if (!project_root || !out_config)
        return Err;

    lighting_global_config_reset(out_config);

    char global_lighting_path[PATH_MAX] = {0};
    if (join_path(project_root, "global.lighting.conf", global_lighting_path, sizeof(global_lighting_path)) != Ok) {
        log_err("Lighting config path is too long for project '%s'", project_root);
        return Err;
    }

    toml_result_t parsed = {0};
    if (vfs_parse_toml_file(global_lighting_path, &parsed) != Ok) {
        log_err("Failed to parse global lighting config '%s'", global_lighting_path);
        return Err;
    }

    toml_datum_t global_table = toml_get(parsed.toptab, "LightingGlobal");
    if (global_table.type != TOML_TABLE) {
        log_err("Global lighting config '%s' is missing [LightingGlobal]", global_lighting_path);
        toml_free(parsed);
        return Err;
    }

    if (snprintf(out_config->lighting_root, sizeof(out_config->lighting_root), "%s", project_root) >= (int)sizeof(out_config->lighting_root)) {
        toml_free(parsed);
        return Err;
    }

    toml_datum_t allow_missing = toml_get(global_table, "allow_missing_scene_lighting");
    if (allow_missing.type == TOML_BOOLEAN)
        out_config->allow_missing_scene_lighting = allow_missing.u.boolean ? True : False;

    toml_datum_t paths_table = toml_get(global_table, "Paths");
    if (paths_table.type == TOML_TABLE) {
        toml_datum_t lighting_dir = toml_get(paths_table, "lighting_dir");
        if (lighting_dir.type == TOML_STRING && lighting_dir.u.s && lighting_dir.u.s[0] != '\0') {
            if (join_path(project_root, lighting_dir.u.s, out_config->lighting_root, sizeof(out_config->lighting_root)) != Ok) {
                log_err("LightingGlobal.Paths.lighting_dir is too long in '%s'", global_lighting_path);
                toml_free(parsed);
                return Err;
            }
        }
    }

    toml_datum_t defaults_table = toml_get(global_table, "Defaults");
    if (defaults_table.type == TOML_TABLE) {
        toml_datum_t default_file = toml_get(defaults_table, "file");
        if (default_file.type == TOML_STRING && default_file.u.s && default_file.u.s[0] != '\0') {
            if (snprintf(out_config->default_file, sizeof(out_config->default_file), "%s", default_file.u.s) >= (int)sizeof(out_config->default_file)) {
                log_err("LightingGlobal.Defaults.file is too long in '%s'", global_lighting_path);
                toml_free(parsed);
                return Err;
            }
        }

        toml_datum_t default_schema = toml_get(defaults_table, "schema");
        if (default_schema.type == TOML_STRING && default_schema.u.s && default_schema.u.s[0] != '\0') {
            if (copy_name(out_config->default_schema, sizeof(out_config->default_schema), default_schema.u.s) != Ok) {
                log_err("LightingGlobal.Defaults.schema is too long in '%s'", global_lighting_path);
                toml_free(parsed);
                return Err;
            }
        }
    }

    toml_datum_t scene_map = toml_get(global_table, "SceneMap");
    if (scene_map.type == TOML_ARRAY) {
        for (int index = 0; index < scene_map.u.arr.size; ++index) {
            if (out_config->scene_map_count >= LightingMaxSceneMapEntries) {
                log_warn("LightingGlobal.SceneMap exceeded max entries (%d); extras ignored", LightingMaxSceneMapEntries);
                break;
            }

            toml_datum_t entry_table = scene_map.u.arr.elem[index];
            if (entry_table.type != TOML_TABLE) {
                log_warn("LightingGlobal.SceneMap[%d] must be a table", index);
                continue;
            }

            toml_datum_t scene_id = toml_get(entry_table, "scene_id");
            toml_datum_t file = toml_get(entry_table, "file");
            if (scene_id.type != TOML_STRING || !scene_id.u.s || scene_id.u.s[0] == '\0' ||
                file.type != TOML_STRING || !file.u.s || file.u.s[0] == '\0') {
                log_warn("LightingGlobal.SceneMap[%d] requires scene_id and file", index);
                continue;
            }

            LightingSceneMapEntry *entry = &out_config->scene_map[out_config->scene_map_count];
            memset(entry, 0, sizeof(*entry));

            if (copy_name(entry->scene_id, sizeof(entry->scene_id), scene_id.u.s) != Ok) {
                log_warn("LightingGlobal.SceneMap[%d].scene_id is too long", index);
                continue;
            }

            if (snprintf(entry->file, sizeof(entry->file), "%s", file.u.s) >= (int)sizeof(entry->file)) {
                log_warn("LightingGlobal.SceneMap[%d].file is too long", index);
                continue;
            }

            toml_datum_t default_schema = toml_get(entry_table, "default_schema");
            if (default_schema.type == TOML_STRING && default_schema.u.s && default_schema.u.s[0] != '\0') {
                if (copy_name(entry->default_schema, sizeof(entry->default_schema), default_schema.u.s) != Ok) {
                    log_warn("LightingGlobal.SceneMap[%d].default_schema is too long", index);
                    continue;
                }
            }

            out_config->scene_map_count++;
        }
    }

    out_config->loaded = True;
    toml_free(parsed);
    return Ok;
}

result lighting_resolve_scene_selection(
    const LightingGlobalConfig *global_config,
    toml_datum_t scene_toptab,
    LightingSceneSelection *out_selection) {
    if (!global_config || !global_config->loaded || scene_toptab.type != TOML_TABLE || !out_selection)
        return Err;

    lighting_scene_selection_reset(out_selection);

    toml_datum_t scene_table = toml_get(scene_toptab, "Scene");
    if (scene_table.type != TOML_TABLE)
        return Err;

    toml_datum_t scene_id_value = toml_get(scene_table, "id");
    const char *scene_id = (scene_id_value.type == TOML_STRING && scene_id_value.u.s && scene_id_value.u.s[0] != '\0')
        ? scene_id_value.u.s
        : Null;

    const LightingSceneMapEntry *mapped_entry = find_scene_map_entry(global_config, scene_id);

    const char *scene_file = Null;
    const char *scene_schema = Null;

    toml_datum_t scene_lighting = toml_get(scene_table, "Lighting");
    if (scene_lighting.type == TOML_TABLE) {
        toml_datum_t file = toml_get(scene_lighting, "file");
        if (file.type == TOML_STRING && file.u.s && file.u.s[0] != '\0')
            scene_file = file.u.s;

        toml_datum_t schema = toml_get(scene_lighting, "schema");
        if (schema.type == TOML_STRING && schema.u.s && schema.u.s[0] != '\0')
            scene_schema = schema.u.s;

        toml_datum_t blend = toml_get(scene_lighting, "blend_in_seconds");
        if (blend.type == TOML_FP64 && blend.u.fp64 >= 0.0) {
            out_selection->has_blend = True;
            out_selection->blend_in_seconds = (float)blend.u.fp64;
        } else if (blend.type == TOML_INT64 && blend.u.int64 >= 0) {
            out_selection->has_blend = True;
            out_selection->blend_in_seconds = (float)blend.u.int64;
        }
    }

    const char *resolved_file = scene_file;
    if (!resolved_file || resolved_file[0] == '\0') {
        if (mapped_entry && mapped_entry->file[0] != '\0')
            resolved_file = mapped_entry->file;
        else if (global_config->default_file[0] != '\0')
            resolved_file = global_config->default_file;
    }

    if (!resolved_file || resolved_file[0] == '\0') {
        if (global_config->allow_missing_scene_lighting)
            return Ok;

        log_err("No lighting file resolved for scene '%s'", scene_id ? scene_id : "<unknown>");
        return Err;
    }

    if (snprintf(out_selection->file_ref, sizeof(out_selection->file_ref), "%s", resolved_file) >= (int)sizeof(out_selection->file_ref))
        return Err;

    if (resolved_file[0] == '/') {
        if (snprintf(out_selection->file_path, sizeof(out_selection->file_path), "%s", resolved_file) >= (int)sizeof(out_selection->file_path))
            return Err;
    } else {
        if (join_path(global_config->lighting_root, resolved_file, out_selection->file_path, sizeof(out_selection->file_path)) != Ok)
            return Err;
    }

    toml_result_t lighting_parsed = {0};
    if (vfs_parse_toml_file(out_selection->file_path, &lighting_parsed) != Ok) {
        if (global_config->allow_missing_scene_lighting)
            return Ok;

        log_err("Failed to parse lighting file '%s'", out_selection->file_path);
        return Err;
    }

    toml_datum_t lighting_table = toml_get(lighting_parsed.toptab, "Lighting");
    toml_datum_t schema_table = toml_get(lighting_parsed.toptab, "Schema");
    if (lighting_table.type != TOML_TABLE || schema_table.type != TOML_TABLE) {
        log_err("Lighting file '%s' must include [Lighting] and [Schema]", out_selection->file_path);
        toml_free(lighting_parsed);
        return Err;
    }

    const char *file_default_schema = Null;
    toml_datum_t file_default_schema_value = toml_get(lighting_table, "default_schema");
    if (file_default_schema_value.type == TOML_STRING && file_default_schema_value.u.s && file_default_schema_value.u.s[0] != '\0')
        file_default_schema = file_default_schema_value.u.s;

    const char *resolved_schema = scene_schema;
    if (!resolved_schema || resolved_schema[0] == '\0') {
        if (mapped_entry && mapped_entry->default_schema[0] != '\0')
            resolved_schema = mapped_entry->default_schema;
        else if (file_default_schema)
            resolved_schema = file_default_schema;
        else if (global_config->default_schema[0] != '\0')
            resolved_schema = global_config->default_schema;
    }

    if (!resolved_schema || resolved_schema[0] == '\0') {
        log_err("No lighting schema resolved for '%s'", out_selection->file_path);
        toml_free(lighting_parsed);
        return Err;
    }

    toml_datum_t active_schema = toml_get(schema_table, resolved_schema);
    if (active_schema.type != TOML_TABLE) {
        log_err("Lighting schema '%s' was not found in '%s'", resolved_schema, out_selection->file_path);
        toml_free(lighting_parsed);
        return Err;
    }

    if (copy_name(out_selection->schema_name, sizeof(out_selection->schema_name), resolved_schema) != Ok) {
        toml_free(lighting_parsed);
        return Err;
    }

    toml_datum_t ambient = toml_get(active_schema, "Ambient");
    if (ambient.type == TOML_TABLE) {
        toml_datum_t global_multiplier = toml_get(ambient, "global_multiplier");
        if (global_multiplier.type == TOML_FP64 && global_multiplier.u.fp64 >= 0.0)
            out_selection->global_light_multiplier = (float)global_multiplier.u.fp64;
        else if (global_multiplier.type == TOML_INT64 && global_multiplier.u.int64 >= 0)
            out_selection->global_light_multiplier = (float)global_multiplier.u.int64;
        else {
            toml_datum_t intensity = toml_get(ambient, "intensity");
            if (intensity.type == TOML_FP64 && intensity.u.fp64 >= 0.0)
                out_selection->global_light_multiplier = (float)intensity.u.fp64;
            else if (intensity.type == TOML_INT64 && intensity.u.int64 >= 0)
                out_selection->global_light_multiplier = (float)intensity.u.int64;
        }

        toml_datum_t sky_color = toml_get(ambient, "sky_color");
        toml_datum_t color = toml_get(ambient, "color");

        unsigned char r = 0;
        unsigned char g = 0;
        unsigned char b = 0;
        if (parse_rgb_triplet(color, &r, &g, &b) == Ok || parse_rgb_triplet(sky_color, &r, &g, &b) == Ok) {
            out_selection->has_clear_color = True;
            out_selection->clear_r = r;
            out_selection->clear_g = g;
            out_selection->clear_b = b;
            out_selection->clear_a = 255;
        }
    }

    out_selection->has_lighting = True;

    toml_free(lighting_parsed);
    return Ok;
}
