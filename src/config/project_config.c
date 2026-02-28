#include "project_config.h"

#include "tomlc17.h"

static result read_window_title(toml_datum_t window_table, WindowConfig *config) {
    toml_datum_t value = toml_get(window_table, "title");
    if (value.type == TOML_STRING && value.u.s)
        config->title = (char *)value.u.s;

    return Ok;
}

static result read_window_dimensions(toml_datum_t window_table, WindowConfig *config) {
    toml_datum_t width = toml_get(window_table, "width");
    toml_datum_t height = toml_get(window_table, "height");

    if (width.type == TOML_INT64 && width.u.int64 > 0)
        config->width = (usize)width.u.int64;

    if (height.type == TOML_INT64 && height.u.int64 > 0)
        config->height = (usize)height.u.int64;

    return Ok;
}

static result read_window_flags(toml_datum_t window_table, WindowConfig *config) {
    toml_datum_t fps = toml_get(window_table, "target_fps");
    toml_datum_t resizable = toml_get(window_table, "resizable");

    if (fps.type == TOML_INT64 && fps.u.int64 > 0)
        config->target_fps = (usize)fps.u.int64;

    if (resizable.type == TOML_BOOLEAN)
        config->resizable = resizable.u.boolean ? True : False;

    return Ok;
}

result load_project_window_config(const char *config_path, WindowConfig *out_config) {
    if (!config_path || !out_config)
        return Err;

    *out_config = DefaultWindowConfig;

    toml_result_t parsed = toml_parse_file_ex(config_path);
    if (!parsed.ok) {
        log_warn("Config parse failed for '%s': %s", config_path, parsed.errmsg);
        toml_free(parsed);
        return Err;
    }

    toml_datum_t window_table = toml_get(parsed.toptab, "Window");
    if (window_table.type != TOML_TABLE) {
        log_warn("Config '%s' is missing [Window] table; using defaults", config_path);
        toml_free(parsed);
        return Err;
    }

    read_window_title(window_table, out_config);
    read_window_dimensions(window_table, out_config);
    read_window_flags(window_table, out_config);

    toml_free(parsed);
    return Ok;
}
