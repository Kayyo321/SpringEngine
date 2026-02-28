#include "runtime_loader.h"

#include "project_config.h"
#include "windowman/windowman.h"

#include "tomlc17.h"

#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

static void frame_update(void) {}

static boolean has_conf_extension(const char *file_name) {
    if (!file_name)
        return False;

    const usize name_len = strlen(file_name);
    const char *suffix = ".conf";
    const usize suffix_len = strlen(suffix);

    if (name_len < suffix_len)
        return False;

    return strcmp(file_name + (name_len - suffix_len), suffix) == 0;
}

static boolean is_absolute_path(const char *path) {
    return path && path[0] == '/';
}

static result join_path(const char *base, const char *path, char *out_path, usize out_size) {
    if (!base || !path || !out_path || out_size == 0)
        return Err;

    if (is_absolute_path(path)) {
        if (snprintf(out_path, out_size, "%s", path) >= (int)out_size)
            return Err;
        return Ok;
    }

    if (snprintf(out_path, out_size, "%s/%s", base, path) >= (int)out_size)
        return Err;

    return Ok;
}

static result parent_directory(const char *path, char *out_dir, usize out_size) {
    if (!path || !out_dir || out_size == 0)
        return Err;

    const char *last_slash = strrchr(path, '/');
    if (!last_slash) {
        if (snprintf(out_dir, out_size, ".") >= (int)out_size)
            return Err;
        return Ok;
    }

    const usize length = (usize)(last_slash - path);
    if (length == 0) {
        if (snprintf(out_dir, out_size, "/") >= (int)out_size)
            return Err;
        return Ok;
    }

    if (length + 1 > out_size)
        return Err;

    memcpy(out_dir, path, length);
    out_dir[length] = '\0';

    return Ok;
}

static result parse_toml_file(const char *path, toml_result_t *out_parsed) {
    if (!path || !out_parsed)
        return Err;

    toml_result_t parsed = toml_parse_file_ex(path);
    if (!parsed.ok) {
        log_err("Failed to parse config '%s': %s", path, parsed.errmsg);
        toml_free(parsed);
        return Err;
    }

    *out_parsed = parsed;
    return Ok;
}

static result validate_conf_files(const char *directory_path, usize *out_conf_count) {
    if (!directory_path || !out_conf_count)
        return Err;

    DIR *directory = opendir(directory_path);
    if (!directory) {
        log_err("Failed to open directory '%s'", directory_path);
        return Err;
    }

    struct dirent *entry = Null;
    while ((entry = readdir(directory)) != Null) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;

        char child_path[PATH_MAX] = {0};
        if (snprintf(child_path, sizeof(child_path), "%s/%s", directory_path, entry->d_name) >= (int)sizeof(child_path)) {
            log_err("Path too long while scanning '%s/%s'", directory_path, entry->d_name);
            closedir(directory);
            return Err;
        }

        struct stat stat_info = {0};
        if (lstat(child_path, &stat_info) != 0) {
            log_err("Failed to stat '%s'", child_path);
            closedir(directory);
            return Err;
        }

        if (S_ISDIR(stat_info.st_mode)) {
            if (validate_conf_files(child_path, out_conf_count) != Ok) {
                closedir(directory);
                return Err;
            }
            continue;
        }

        if (!S_ISREG(stat_info.st_mode) || !has_conf_extension(entry->d_name))
            continue;

        toml_result_t parsed = {0};
        if (parse_toml_file(child_path, &parsed) != Ok) {
            closedir(directory);
            return Err;
        }

        toml_free(parsed);
        (*out_conf_count)++;
    }

    closedir(directory);
    return Ok;
}

result run_project_runtime(const char *project_path) {
    struct stat project_stat = {0};
    if (!project_path || stat(project_path, &project_stat) != 0 || !S_ISDIR(project_stat.st_mode)) {
        log_err("Project path '%s' is not a valid directory", project_path ? project_path : "<null>");
        return Err;
    }

    usize conf_count = 0;
    if (validate_conf_files(project_path, &conf_count) != Ok)
        return Err;

    if (conf_count == 0) {
        log_err("No .conf files found under '%s'", project_path);
        return Err;
    }

    char springengine_config_path[PATH_MAX] = {0};
    if (join_path(project_path, "springengine.conf", springengine_config_path, sizeof(springengine_config_path)) != Ok) {
        log_err("Failed to resolve springengine.conf path for '%s'", project_path);
        return Err;
    }

    if (access(springengine_config_path, F_OK) != 0) {
        log_err("Required config missing: '%s'", springengine_config_path);
        return Err;
    }

    WindowConfig window_config = DefaultWindowConfig;
    if (load_project_window_config(springengine_config_path, &window_config) != Ok) {
        log_err("Failed to load required window settings from '%s'", springengine_config_path);
        return Err;
    }

    toml_result_t project_toml = {0};
    if (parse_toml_file(springengine_config_path, &project_toml) != Ok)
        return Err;

    toml_datum_t boot_table = toml_get(project_toml.toptab, "Boot");
    if (boot_table.type != TOML_TABLE) {
        log_err("Config '%s' is missing [Boot] table", springengine_config_path);
        toml_free(project_toml);
        return Err;
    }

    toml_datum_t first_scene = toml_get(boot_table, "first_scene");
    if (first_scene.type != TOML_STRING || !first_scene.u.s || first_scene.u.s[0] == '\0') {
        log_err("Config '%s' is missing Boot.first_scene", springengine_config_path);
        toml_free(project_toml);
        return Err;
    }

    toml_datum_t autoload_data = toml_get(boot_table, "autoload_data");
    if (autoload_data.type != TOML_STRING || !autoload_data.u.s || autoload_data.u.s[0] == '\0') {
        log_err("Config '%s' is missing Boot.autoload_data", springengine_config_path);
        toml_free(project_toml);
        return Err;
    }

    char scenes_root[PATH_MAX] = {0};
    if (snprintf(scenes_root, sizeof(scenes_root), "%s", project_path) >= (int)sizeof(scenes_root)) {
        log_err("Project path is too long: '%s'", project_path);
        toml_free(project_toml);
        return Err;
    }

    toml_datum_t paths_table = toml_get(project_toml.toptab, "Paths");
    if (paths_table.type == TOML_TABLE) {
        toml_datum_t scenes_dir = toml_get(paths_table, "scenes_dir");
        if (scenes_dir.type == TOML_STRING && scenes_dir.u.s && scenes_dir.u.s[0] != '\0') {
            if (join_path(project_path, scenes_dir.u.s, scenes_root, sizeof(scenes_root)) != Ok) {
                log_err("Failed to resolve Paths.scenes_dir from '%s'", springengine_config_path);
                toml_free(project_toml);
                return Err;
            }
        }
    }

    char autoload_path[PATH_MAX] = {0};
    if (join_path(project_path, autoload_data.u.s, autoload_path, sizeof(autoload_path)) != Ok) {
        log_err("Failed to resolve Boot.autoload_data path");
        toml_free(project_toml);
        return Err;
    }

    toml_result_t autoload_toml = {0};
    if (parse_toml_file(autoload_path, &autoload_toml) != Ok) {
        toml_free(project_toml);
        return Err;
    }
    toml_free(autoload_toml);

    char first_scene_path[PATH_MAX] = {0};
    if (join_path(scenes_root, first_scene.u.s, first_scene_path, sizeof(first_scene_path)) != Ok) {
        log_err("Failed to resolve Boot.first_scene path");
        toml_free(project_toml);
        return Err;
    }

    toml_result_t scene_toml = {0};
    if (parse_toml_file(first_scene_path, &scene_toml) != Ok) {
        toml_free(project_toml);
        return Err;
    }

    toml_datum_t scene_table = toml_get(scene_toml.toptab, "Scene");
    if (scene_table.type != TOML_TABLE) {
        log_err("Scene config '%s' is missing [Scene] table", first_scene_path);
        toml_free(scene_toml);
        toml_free(project_toml);
        return Err;
    }

    toml_datum_t scene_data_file = toml_get(scene_table, "data_file");
    if (scene_data_file.type != TOML_STRING || !scene_data_file.u.s || scene_data_file.u.s[0] == '\0') {
        log_err("Scene config '%s' is missing Scene.data_file", first_scene_path);
        toml_free(scene_toml);
        toml_free(project_toml);
        return Err;
    }

    char first_scene_directory[PATH_MAX] = {0};
    if (parent_directory(first_scene_path, first_scene_directory, sizeof(first_scene_directory)) != Ok) {
        log_err("Failed to resolve first scene directory for '%s'", first_scene_path);
        toml_free(scene_toml);
        toml_free(project_toml);
        return Err;
    }

    char scene_data_path[PATH_MAX] = {0};
    if (join_path(first_scene_directory, scene_data_file.u.s, scene_data_path, sizeof(scene_data_path)) != Ok) {
        log_err("Failed to resolve Scene.data_file path");
        toml_free(scene_toml);
        toml_free(project_toml);
        return Err;
    }

    toml_result_t scene_data_toml = {0};
    if (parse_toml_file(scene_data_path, &scene_data_toml) != Ok) {
        toml_free(scene_toml);
        toml_free(project_toml);
        return Err;
    }

    toml_free(scene_data_toml);
    toml_free(scene_toml);
    toml_free(project_toml);

    log_msg("Loaded project config '%s'", springengine_config_path);
    log_msg("Loaded autoload data '%s'", autoload_path);
    log_msg("Loaded first scene '%s'", first_scene_path);
    log_msg("Loaded first scene data '%s'", scene_data_path);

    if (open_window(window_config) != Ok)
        return Err;

    while (!update_window(frame_update)) {}

    close_window();
    return Ok;
}
