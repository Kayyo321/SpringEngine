#include "version_config.h"

#include "tomlc17.h"
#include "vfs.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

static result join_path(const char *base, const char *path, char *out_path, usize out_size) {
    return vfs_resolve_path(base, path, out_path, out_size);
}

static const char *trim_left(const char *text) {
    if (!text)
        return Null;

    while (*text != '\0' && isspace((unsigned char)*text))
        text++;

    return text;
}

static void trim_right_inplace(char *text) {
    if (!text)
        return;

usize length = strlen(text);
while (length > 0 && isspace((unsigned char)text[length - 1])) {
    text[length - 1] = '\0';
    length--;
}
}

static result parse_next_version_part(const char **cursor, long *out_value, boolean *out_done) {
    if (!cursor || !out_value || !out_done)
        return Err;

    if (*cursor == Null || **cursor == '\0') {
        *out_value = 0;
        *out_done = True;
        *cursor = Null;
        return Ok;
    }

    const char *scan = *cursor;
    long value = 0;
    int digit_count = 0;

    while (*scan != '\0' && isdigit((unsigned char)*scan)) {
        value = (value * 10) + (*scan - '0');
        scan++;
        digit_count++;
    }

    if (digit_count == 0)
        return Err;

    if (*scan == '.') {
        scan++;
        if (*scan == '\0')
            return Err;
        *cursor = scan;
        *out_done = False;
    } else if (*scan == '\0') {
        *cursor = Null;
        *out_done = True;
    } else {
        return Err;
    }

    *out_value = value;
    return Ok;
}

static result compare_versions(const char *left, const char *right, int *out_cmp) {
    if (!left || !right || !out_cmp)
        return Err;

    const char *left_cursor = left;
    const char *right_cursor = right;
    boolean left_done = False;
    boolean right_done = False;

    // Segment cap prevents malformed inputs from looping indefinitely.
    for (int index = 0; index < 64; ++index) {
        long left_part = 0;
        long right_part = 0;

        if (parse_next_version_part(&left_cursor, &left_part, &left_done) != Ok)
            return Err;

        if (parse_next_version_part(&right_cursor, &right_part, &right_done) != Ok)
            return Err;

        if (left_part < right_part) {
            *out_cmp = -1;
            return Ok;
        }

        if (left_part > right_part) {
            *out_cmp = 1;
            return Ok;
        }

        if (left_done && right_done) {
            *out_cmp = 0;
            return Ok;
        }
    }

    return Err;
}

static result parse_requirement(const char *requirement_text, char *out_operator, char *out_version, usize out_version_size) {
    if (!requirement_text || !out_operator || !out_version || out_version_size == 0)
        return Err;

    const char *cursor = trim_left(requirement_text);
    if (!cursor || (*cursor != '>' && *cursor != '='))
        return Err;

    *out_operator = *cursor;
    cursor = trim_left(cursor + 1);

    if (!cursor || *cursor == '\0')
        return Err;

    if (snprintf(out_version, out_version_size, "%s", cursor) >= (int)out_version_size)
        return Err;

    trim_right_inplace(out_version);
    if (out_version[0] == '\0')
        return Err;

    return Ok;
}

static const char *find_requirement_text(toml_result_t parsed) {
    toml_datum_t version_table = toml_get(parsed.toptab, "Version");
    if (version_table.type == TOML_TABLE) {
        toml_datum_t springengine = toml_get(version_table, "springengine");
        if (springengine.type == TOML_STRING && springengine.u.s && springengine.u.s[0] != '\0')
            return springengine.u.s;

        toml_datum_t required = toml_get(version_table, "required");
        if (required.type == TOML_STRING && required.u.s && required.u.s[0] != '\0')
            return required.u.s;
    }

    toml_datum_t root_springengine = toml_get(parsed.toptab, "springengine");
    if (root_springengine.type == TOML_STRING && root_springengine.u.s && root_springengine.u.s[0] != '\0')
        return root_springengine.u.s;

    toml_datum_t root_required = toml_get(parsed.toptab, "required");
    if (root_required.type == TOML_STRING && root_required.u.s && root_required.u.s[0] != '\0')
        return root_required.u.s;

    return Null;
}

result version_check_project_requirement(const char *project_root, const char *engine_version) {
    if (!project_root || !engine_version || engine_version[0] == '\0')
        return Err;

    char version_config_path[PATH_MAX] = {0};
    if (join_path(project_root, "version.conf", version_config_path, sizeof(version_config_path)) != Ok) {
        log_err("Version config path is too long for project '%s'", project_root);
        return Err;
    }

    if (vfs_file_exists(version_config_path) != True) {
        log_warn("Optional version config not found: '%s'", version_config_path);
        return Ok;
    }

    toml_result_t parsed = {0};
    if (vfs_parse_toml_file(version_config_path, &parsed) != Ok) {
        log_err("Failed to parse version requirements from '%s'", version_config_path);
        return Err;
    }

    const char *requirement_text = find_requirement_text(parsed);
    if (!requirement_text) {
        log_err("Version config '%s' must define Version.springengine or Version.required", version_config_path);
        toml_free(parsed);
        return Err;
    }

    char requirement_operator = '\0';
    char required_version[64] = {0};
    if (parse_requirement(requirement_text, &requirement_operator, required_version, sizeof(required_version)) != Ok) {
        log_err("Version config '%s' contains invalid requirement '%s'; expected format '> 0.1.0' or '= 0.1.0'", version_config_path, requirement_text);
        toml_free(parsed);
        return Err;
    }

    int comparison = 0;
    if (compare_versions(engine_version, required_version, &comparison) != Ok) {
        log_err("Version config '%s' contains invalid semantic version '%s'", version_config_path, required_version);
        toml_free(parsed);
        return Err;
    }

    boolean passes = False;
    if (requirement_operator == '>')
        passes = comparison > 0 ? True : False;
    else if (requirement_operator == '=')
        passes = comparison == 0 ? True : False;

    if (!passes) {
        log_err(
            "Project requires SpringEngine %c %s but current version is %s",
            requirement_operator,
            required_version,
            engine_version);
        toml_free(parsed);
        return Err;
    }

    log_msg("Version requirement satisfied: SpringEngine %c %s", requirement_operator, required_version);
    toml_free(parsed);
    return Ok;
}
