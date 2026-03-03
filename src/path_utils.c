#include "path_utils.h"

#include <sys/stat.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

boolean is_absolute_path(const char *path) {
    return (path && path[0] == '/') ? True : False;
}

result join_path(const char *base, const char *path, char *out_path, usize out_size) {
    if (!base || !path || !out_path || out_size == 0)
        return Err;

    if (is_absolute_path(path)) {
        if (snprintf(out_path, out_size, "%s", path) >= (int)out_size)
            return Err;
        return Ok;
    }

    if (base[0] == '\0') {
        if (snprintf(out_path, out_size, "%s", path) >= (int)out_size)
            return Err;
        return Ok;
    }

    if (snprintf(out_path, out_size, "%s/%s", base, path) >= (int)out_size)
        return Err;

    return Ok;
}

result ensure_directory(const char *path) {
    if (!path || path[0] == '\0')
        return Err;

    struct stat path_stat = {0};
    if (stat(path, &path_stat) == 0) {
        if (S_ISDIR(path_stat.st_mode))
            return Ok;

        log_err("Path exists but is not a directory: '%s'", path);
        return Err;
    }

    if (mkdir(path, 0755) != 0) {
        log_err("Failed to create directory '%s': %s", path, strerror(errno));
        return Err;
    }

    return Ok;
}

result ensure_directory_recursive(const char *path) {
    if (!path || path[0] == '\0')
        return Err;

    char work[PATH_MAX] = {0};
    if (snprintf(work, sizeof(work), "%s", path) >= (int)sizeof(work)) {
        log_err("Directory path too long: '%s'", path);
        return Err;
    }

    const usize length = strlen(work);
    if (length == 0)
        return Err;

    if (length > 1 && work[length - 1] == '/')
        work[length - 1] = '\0';

    for (char *cursor = work + 1; *cursor != '\0'; ++cursor) {
        if (*cursor != '/')
            continue;

        *cursor = '\0';
        if (ensure_directory(work) != Ok)
            return Err;
        *cursor = '/';
    }

    return ensure_directory(work);
}

result parent_directory(const char *path, char *out_parent, usize out_size) {
    if (!path || !out_parent || out_size == 0)
        return Err;

    const char *slash = strrchr(path, '/');
    if (!slash)
        return Err;

    const usize parent_length = (usize)(slash - path);
    if (parent_length == 0) {
        if (snprintf(out_parent, out_size, "/") >= (int)out_size)
            return Err;
        return Ok;
    }

    if (parent_length + 1 > out_size)
        return Err;

    memcpy(out_parent, path, parent_length);
    out_parent[parent_length] = '\0';
    return Ok;
}

result normalize_path(const char *path, char *out_path, usize out_size) {
    if (!path || !out_path || out_size == 0)
        return Err;

    usize out_index = 0;
    for (usize index = 0; path[index] != '\0'; ++index) {
        char ch = path[index];
        if (ch == '\\')
            ch = '/';

        if (ch == '/' && out_index > 0 && out_path[out_index - 1] == '/')
            continue;

        if (out_index + 1 >= out_size)
            return Err;

        out_path[out_index++] = ch;
    }

    while (out_index > 1 && out_path[out_index - 1] == '/')
        --out_index;

    if (out_index == 0) {
        out_path[0] = '\0';
        return Ok;
    }

    out_path[out_index] = '\0';
    return Ok;
}

result normalize_relative_path(const char *path, char *out_path, usize out_size) {
    if (normalize_path(path, out_path, out_size) != Ok)
        return Err;

    if (out_path[0] == '/')
        return Err;

    return Ok;
}

result tar_split_path(const char *relative_path, char *out_name, usize out_name_size, char *out_prefix, usize out_prefix_size) {
    if (!relative_path || !out_name || !out_prefix)
        return Err;

    const usize path_length = strlen(relative_path);
    if (path_length == 0)
        return Err;

    if (path_length <= out_name_size - 1) {
        memset(out_prefix, 0, out_prefix_size);
        if (snprintf(out_name, out_name_size, "%s", relative_path) >= (int)out_name_size)
            return Err;
        return Ok;
    }

    const char *split = Null;
    for (const char *cursor = relative_path; *cursor != '\0'; ++cursor) {
        if (*cursor == '/') {
            const usize prefix_length = (usize)(cursor - relative_path);
            const usize name_length = strlen(cursor + 1);
            if (prefix_length <= out_prefix_size - 1 && name_length <= out_name_size - 1)
                split = cursor;
        }
    }

    if (!split)
        return Err;

    const usize prefix_length = (usize)(split - relative_path);
    const usize name_length = strlen(split + 1);

    memset(out_prefix, 0, out_prefix_size);
    memcpy(out_prefix, relative_path, prefix_length);
    out_prefix[prefix_length] = '\0';

    memset(out_name, 0, out_name_size);
    memcpy(out_name, split + 1, name_length);
    out_name[name_length] = '\0';
    return Ok;
}

const char *path_extension(const char *path) {
    const char *dot = path ? strrchr(path, '.') : Null;
    return dot ? dot : "";
}

boolean path_has_extension(const char *path, const char *extension) {
    if (!path || !extension)
        return False;

    const char *dot = strrchr(path, '.');
    if (!dot)
        return False;

    return strcmp(dot, extension) == 0 ? True : False;
}

boolean path_is_targame_archive(const char *path) {
    return path_has_extension(path, ".targame");
}

boolean path_has_unsafe_components(const char *path) {
    if (!path || path[0] == '\0')
        return True;

    if (path[0] == '/')
        return True;

    if (strcmp(path, "..") == 0)
        return True;

    if (strstr(path, "../") != Null)
        return True;

    if (strstr(path, "/..") != Null)
        return True;

    return False;
}
