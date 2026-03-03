#include "testing.h"

#define COMMON_ALLOW_STDLIB_ALLOCATORS
#include "packer.h"

#include "vfs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

static result create_temp_test_dir(char *path_template, const char **out_temp_dir) {
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

static void remove_tree(const char *path) {
    if (!path || path[0] == '\0')
        return;

    char command[1024] = {0};
    if (snprintf(command, sizeof(command), "rm -rf '%s'", path) >= (int)sizeof(command))
        return;

    (void)system(command);
}

static result ensure_directory(const char *path) {
    if (!path || path[0] == '\0')
        return Err;

    struct stat st = {0};
    if (stat(path, &st) == 0)
        return S_ISDIR(st.st_mode) ? Ok : Err;

    return mkdir(path, 0755) == 0 ? Ok : Err;
}

static result write_file_text(const char *path, const char *content) {
    if (!path || !content)
        return Err;

    FILE *file = fopen(path, "w");
    if (!file)
        return Err;

    if (fputs(content, file) == EOF) {
        fclose(file);
        return Err;
    }

    return fclose(file) == 0 ? Ok : Err;
}

static result read_file_text(const char *path, char *out_text, usize out_size) {
    if (!path || !out_text || out_size == 0)
        return Err;

    FILE *file = fopen(path, "r");
    if (!file)
        return Err;

    usize read_count = fread(out_text, 1, out_size - 1, file);
    out_text[read_count] = '\0';
    fclose(file);
    return Ok;
}

void run_vfs_and_packer_tests(void) {
#ifdef TESTING
    usize failed = 0;
    log_msg("Running VFS/Packer tests...");

    if (pack_directory_to_targame(Null, "x.targame") != Err || pack_directory_to_targame("", "x.targame") != Err
        || pack_directory_to_targame(".", Null) != Err || unpack_targame_to_directory(Null, "x") != Err
        || unpack_targame_to_directory("", "x") != Err || unpack_targame_to_directory("x.targame", Null) != Err) {
        log_err("Packer should reject null/empty input arguments");
        ++failed;
    }

    char temp_dir_template[] = "/tmp/springengine-vfs-packer-XXXXXX";
    const char *temp_dir = Null;
    if (create_temp_test_dir(temp_dir_template, &temp_dir) != Ok || !temp_dir) {
        log_err("Temp directory creation failed for VFS/Packer tests");
        ++failed;
        record_test_result("VFS/Packer tests", failed);
        return;
    }

    char project_dir[PATH_MAX] = {0};
    char scripts_dir[PATH_MAX] = {0};
    char assets_dir[PATH_MAX] = {0};
    char sprites_dir[PATH_MAX] = {0};
    char project_conf[PATH_MAX] = {0};
    char script_file[PATH_MAX] = {0};
    char sprite_file[PATH_MAX] = {0};
    char archive_path[PATH_MAX] = {0};
    char nested_archive_path[PATH_MAX] = {0};
    char unpacked_dir[PATH_MAX] = {0};
    char unpacked_script[PATH_MAX] = {0};

    if (snprintf(project_dir, sizeof(project_dir), "%s/project", temp_dir) >= (int)sizeof(project_dir)
        || snprintf(scripts_dir, sizeof(scripts_dir), "%s/scripts", project_dir) >= (int)sizeof(scripts_dir)
        || snprintf(assets_dir, sizeof(assets_dir), "%s/assets", project_dir) >= (int)sizeof(assets_dir)
        || snprintf(sprites_dir, sizeof(sprites_dir), "%s/Sprites", assets_dir) >= (int)sizeof(sprites_dir)
        || snprintf(project_conf, sizeof(project_conf), "%s/springengine.conf", project_dir) >= (int)sizeof(project_conf)
        || snprintf(script_file, sizeof(script_file), "%s/main.lua", scripts_dir) >= (int)sizeof(script_file)
        || snprintf(sprite_file, sizeof(sprite_file), "%s/player.txt", sprites_dir) >= (int)sizeof(sprite_file)
        || snprintf(archive_path, sizeof(archive_path), "%s/game.targame", temp_dir) >= (int)sizeof(archive_path)
        || snprintf(nested_archive_path, sizeof(nested_archive_path), "%s/new/archive/path/game.targame", temp_dir) >= (int)sizeof(nested_archive_path)
        || snprintf(unpacked_dir, sizeof(unpacked_dir), "%s/unpacked", temp_dir) >= (int)sizeof(unpacked_dir)
        || snprintf(unpacked_script, sizeof(unpacked_script), "%s/scripts/main.lua", unpacked_dir) >= (int)sizeof(unpacked_script)) {
        log_err("Path assembly failed in VFS/Packer tests");
        ++failed;
        remove_tree(temp_dir);
        record_test_result("VFS/Packer tests", failed);
        return;
    }

    if (ensure_directory(project_dir) != Ok || ensure_directory(scripts_dir) != Ok || ensure_directory(assets_dir) != Ok || ensure_directory(sprites_dir) != Ok) {
        log_err("Failed to create test project directory tree");
        ++failed;
    }

    const char *springengine_conf =
        "[Engine]\n"
        "project_id = \"archive-test\"\n"
        "\n"
        "[Boot]\n"
        "first_scene = \"starting_scene.scene.conf\"\n"
        "autoload_data = \"autoload.dat.conf\"\n"
        "\n"
        "[Window]\n"
        "title = \"Archive Test\"\n"
        "width = 320\n"
        "height = 180\n";

    if (write_file_text(project_conf, springengine_conf) != Ok
        || write_file_text(script_file, "return { update = function(self) return 1 end }\n") != Ok
        || write_file_text(sprite_file, "sprite-data\n") != Ok) {
        log_err("Failed writing test files for VFS/Packer tests");
        ++failed;
    }

    if (pack_directory_to_targame(project_dir, archive_path) != Ok) {
        log_err("Packing test project into archive should succeed");
        ++failed;
    }

    if (pack_directory_to_targame(project_dir, nested_archive_path) != Ok) {
        log_err("Packing should create missing archive destination directories");
        ++failed;
    }

    if (unpack_targame_to_directory(archive_path, unpacked_dir) != Ok) {
        log_err("Unpacking archive should succeed");
        ++failed;
    }

    char unpacked_script_contents[256] = {0};
    if (read_file_text(unpacked_script, unpacked_script_contents, sizeof(unpacked_script_contents)) != Ok
        || strstr(unpacked_script_contents, "update") == Null) {
        log_err("Unpacked script should match packed content");
        ++failed;
    }

    if (vfs_mount_project(project_dir) != Ok) {
        log_err("VFS should mount disk projects");
        ++failed;
    } else {
        if (vfs_is_archive_mode() != False) {
            log_err("Disk mount should not report archive mode");
            ++failed;
        }

        char resolved_path[PATH_MAX] = {0};
        if (vfs_resolve_path(project_dir, "scripts/main.lua", resolved_path, sizeof(resolved_path)) != Ok
            || vfs_file_exists(resolved_path) != True) {
            log_err("Disk VFS should resolve and find script path");
            ++failed;
        }

        Heap script_text = NullHeap;
        if (vfs_read_file_text(resolved_path, &script_text) != Ok || !script_text.pointer || strstr((const char *)script_text.pointer, "return") == Null) {
            log_err("Disk VFS should read script text");
            ++failed;
        }
        if (script_text.pointer)
            deallocate(script_text);

        vfs_unmount();
    }

    if (vfs_mount_project(archive_path) != Ok) {
        log_err("VFS should mount .targame archive projects");
        ++failed;
    } else {
        if (vfs_is_archive_mode() != True) {
            log_err("Archive mount should report archive mode");
            ++failed;
        }

        char resolved_script_path[PATH_MAX] = {0};
        if (vfs_resolve_path(archive_path, "scripts/main.lua", resolved_script_path, sizeof(resolved_script_path)) != Ok
            || vfs_file_exists(resolved_script_path) != True) {
            log_err("Archive VFS should resolve and find script path");
            ++failed;
        }

        Heap archive_script_text = NullHeap;
        if (vfs_read_file_text(resolved_script_path, &archive_script_text) != Ok || !archive_script_text.pointer
            || strstr((const char *)archive_script_text.pointer, "update") == Null) {
            log_err("Archive VFS should read script text from memory");
            ++failed;
        }
        if (archive_script_text.pointer)
            deallocate(archive_script_text);

        toml_result_t parsed = {0};
        if (vfs_parse_toml_file("springengine.conf", &parsed) != Ok) {
            log_err("Archive VFS should parse TOML text from memory");
            ++failed;
        } else {
            toml_free(parsed);
        }

        usize conf_count = 0;
        if (vfs_count_conf_files(&conf_count) != Ok || conf_count == 0) {
            log_err("Archive VFS should report .conf files");
            ++failed;
        }

        vfs_unmount();
    }

    remove_tree(temp_dir);
    record_test_result("VFS/Packer tests", failed);
#endif
}