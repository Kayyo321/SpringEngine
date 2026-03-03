#include "testing.h"

#define CommonAllowStdlibAllocators
#include "maker.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "common.h"

static result create_temp_test_dir(char *path_template, usize path_template_size, const char **out_temp_dir) {
    if (!path_template || !out_temp_dir || path_template_size == 0)
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

static boolean file_exists(const char *path) {
    if (!path)
        return False;

    struct stat file_stat = {0};
    return stat(path, &file_stat) == 0 && S_ISREG(file_stat.st_mode) ? True : False;
}

static boolean directory_exists(const char *path) {
    if (!path)
        return False;

    struct stat dir_stat = {0};
    return stat(path, &dir_stat) == 0 && S_ISDIR(dir_stat.st_mode) ? True : False;
}

static result read_file_contents(const char *path, char *output, usize output_size) {
    if (!path || !output || output_size == 0)
        return Err;

    FILE *file = fopen(path, "r");
    if (!file)
        return Err;

    const usize read_count = fread(output, sizeof(char), output_size - 1, file);
    output[read_count] = '\0';

    if (fclose(file) != 0)
        return Err;

    return Ok;
}

static void remove_tree(const char *path) {
    if (!path || path[0] == '\0')
        return;

    char command[1024] = {0};
    if (snprintf(command, sizeof(command), "rm -rf '%s'", path) >= (int)sizeof(command))
        return;

    (void)system(command);
}

void run_maker_tests(void) {
#ifdef TESTING
    usize failed = 0;

    log_msg("Running maker tests...");

    if (make_project(Null, "proj") != Err || make_project("", "proj") != Err || make_project("/tmp", Null) != Err || make_project("/tmp", "") != Err) {
        log_err("make_project should reject null/empty input arguments");
        ++failed;
    }

    if (make_script(Null, "player") != Err || make_script("", "player") != Err || make_script("/tmp", Null) != Err || make_script("/tmp", "") != Err) {
        log_err("make_script should reject null/empty input arguments");
        ++failed;
    }

    if (make_scene(Null, "level") != Err || make_scene("", "level") != Err || make_scene("/tmp", Null) != Err || make_scene("/tmp", "") != Err) {
        log_err("make_scene should reject null/empty input arguments");
        ++failed;
    }

    if (make_ui_document(Null, "hud") != Err || make_ui_document("", "hud") != Err || make_ui_document("/tmp", Null) != Err || make_ui_document("/tmp", "") != Err) {
        log_err("make_ui_document should reject null/empty input arguments");
        ++failed;
    }

    char temp_dir_template[] = "/tmp/springengine-maker-XXXXXX";
    const char *temp_dir = Null;
    if (create_temp_test_dir(temp_dir_template, sizeof(temp_dir_template), &temp_dir) != Ok || !temp_dir) {
        log_err("temp directory creation failed for maker tests");
        ++failed;
        record_test_result("Maker tests", failed);
        return;
    }

    char project_root[512] = {0};
    char scripts_dir[512] = {0};
    char prefabs_dir[512] = {0};
    char assets_dir[512] = {0};
    char sounds_dir[512] = {0};
    char sprites_dir[512] = {0};
    char ui_dir[512] = {0};
    char springengine_conf[512] = {0};
    char input_conf[512] = {0};
    char autoload_conf[512] = {0};
    char start_scene_conf[512] = {0};
    char start_scene_data[512] = {0};
    char script_file[512] = {0};
    char custom_script_file[512] = {0};
    char scene_manifest[512] = {0};
    char scene_data[512] = {0};
    char scene_manifest_with_suffix[512] = {0};
    char scene_data_with_suffix[512] = {0};
    char ui_doc[512] = {0};
    char ui_doc_with_suffix[512] = {0};

    if (snprintf(project_root, sizeof(project_root), "%s/MyNewGame", temp_dir) >= (int)sizeof(project_root)
        || snprintf(scripts_dir, sizeof(scripts_dir), "%s/scripts", project_root) >= (int)sizeof(scripts_dir)
        || snprintf(prefabs_dir, sizeof(prefabs_dir), "%s/prefabs", project_root) >= (int)sizeof(prefabs_dir)
        || snprintf(assets_dir, sizeof(assets_dir), "%s/assets", project_root) >= (int)sizeof(assets_dir)
        || snprintf(sounds_dir, sizeof(sounds_dir), "%s/assets/sounds", project_root) >= (int)sizeof(sounds_dir)
        || snprintf(sprites_dir, sizeof(sprites_dir), "%s/assets/sprites", project_root) >= (int)sizeof(sprites_dir)
        || snprintf(ui_dir, sizeof(ui_dir), "%s/ui", project_root) >= (int)sizeof(ui_dir)
        || snprintf(springengine_conf, sizeof(springengine_conf), "%s/springengine.conf", project_root) >= (int)sizeof(springengine_conf)
        || snprintf(input_conf, sizeof(input_conf), "%s/input.conf", project_root) >= (int)sizeof(input_conf)
        || snprintf(autoload_conf, sizeof(autoload_conf), "%s/autoload.dat.conf", project_root) >= (int)sizeof(autoload_conf)
        || snprintf(start_scene_conf, sizeof(start_scene_conf), "%s/starting_scene.scene.conf", project_root) >= (int)sizeof(start_scene_conf)
        || snprintf(start_scene_data, sizeof(start_scene_data), "%s/starting_scene.dat.conf", project_root) >= (int)sizeof(start_scene_data)
        || snprintf(script_file, sizeof(script_file), "%s/scripts/player.lua", project_root) >= (int)sizeof(script_file)
        || snprintf(custom_script_file, sizeof(custom_script_file), "%s/scripts/123-boss.ai.lua", project_root) >= (int)sizeof(custom_script_file)
        || snprintf(scene_manifest, sizeof(scene_manifest), "%s/arena.scene.conf", project_root) >= (int)sizeof(scene_manifest)
        || snprintf(scene_data, sizeof(scene_data), "%s/arena.dat.conf", project_root) >= (int)sizeof(scene_data)
        || snprintf(scene_manifest_with_suffix, sizeof(scene_manifest_with_suffix), "%s/boss.scene.conf", project_root) >= (int)sizeof(scene_manifest_with_suffix)
        || snprintf(scene_data_with_suffix, sizeof(scene_data_with_suffix), "%s/boss.dat.conf", project_root) >= (int)sizeof(scene_data_with_suffix)
        || snprintf(ui_doc, sizeof(ui_doc), "%s/ui/hud.ui.conf", project_root) >= (int)sizeof(ui_doc)
        || snprintf(ui_doc_with_suffix, sizeof(ui_doc_with_suffix), "%s/ui/pause.ui.conf", project_root) >= (int)sizeof(ui_doc_with_suffix)) {
        log_err("failed composing maker test paths");
        ++failed;
        goto cleanup;
    }

    if (make_project(temp_dir, "MyNewGame") != Ok) {
        log_err("make_project should create a valid project scaffold");
        ++failed;
        goto cleanup;
    }

    if (directory_exists(project_root) != True || directory_exists(scripts_dir) != True || directory_exists(prefabs_dir) != True
        || directory_exists(assets_dir) != True || directory_exists(sounds_dir) != True || directory_exists(sprites_dir) != True
        || directory_exists(ui_dir) != True) {
        log_err("make_project should create all expected directories");
        ++failed;
    }

    if (file_exists(springengine_conf) != True || file_exists(input_conf) != True || file_exists(autoload_conf) != True
        || file_exists(start_scene_conf) != True || file_exists(start_scene_data) != True) {
        log_err("make_project should create all expected starter files");
        ++failed;
    }

    const usize warn_before_duplicate_project = get_warn_count();
    const usize err_before_duplicate_project = get_error_count();
    if (make_project(temp_dir, "MyNewGame") != Err) {
        log_err("make_project should refuse overwriting existing default files");
        ++failed;
    }
    restore_diagnostic_counts(warn_before_duplicate_project, err_before_duplicate_project);

    if (make_script(project_root, "player") != Ok) {
        log_err("make_script should create script files with missing extension");
        ++failed;
    }

    if (make_script(project_root, "123-boss.ai") != Ok) {
        log_err("make_script should support names requiring Lua symbol normalization");
        ++failed;
    }

    const usize warn_before_duplicate_script = get_warn_count();
    const usize err_before_duplicate_script = get_error_count();
    if (make_script(project_root, "player") != Err) {
        log_err("make_script should refuse to overwrite existing scripts");
        ++failed;
    }
    restore_diagnostic_counts(warn_before_duplicate_script, err_before_duplicate_script);

    char script_contents[4096] = {0};
    if (read_file_contents(script_file, script_contents, sizeof(script_contents)) != Ok || strstr(script_contents, "local player = {}") == Null || strstr(script_contents, "function player:awake()") == Null) {
        log_err("make_script should create default lifecycle methods for simple script names");
        ++failed;
    }

    memset(script_contents, 0, sizeof(script_contents));
    if (read_file_contents(custom_script_file, script_contents, sizeof(script_contents)) != Ok || strstr(script_contents, "local _123_boss_ai = {}") == Null || strstr(script_contents, "return _123_boss_ai") == Null) {
        log_err("make_script should normalize non-identifier script names into valid Lua symbols");
        ++failed;
    }

    if (make_scene(project_root, "arena") != Ok) {
        log_err("make_scene should create scene/data files for base scene names");
        ++failed;
    }

    if (make_scene(project_root, "boss.scene.conf") != Ok) {
        log_err("make_scene should accept names that already include .scene.conf");
        ++failed;
    }

    const usize warn_before_duplicate_scene = get_warn_count();
    const usize err_before_duplicate_scene = get_error_count();
    if (make_scene(project_root, "arena") != Err) {
        log_err("make_scene should refuse to overwrite existing scene files");
        ++failed;
    }
    restore_diagnostic_counts(warn_before_duplicate_scene, err_before_duplicate_scene);

    char scene_contents[4096] = {0};
    if (read_file_contents(scene_manifest_with_suffix, scene_contents, sizeof(scene_contents)) != Ok
        || strstr(scene_contents, "id = \"boss\"") == Null
        || strstr(scene_contents, "data_file = \"boss.dat.conf\"") == Null) {
        log_err("make_scene should trim .scene.conf suffix when deriving ids and data names");
        ++failed;
    }

    if (file_exists(scene_manifest) != True || file_exists(scene_data) != True || file_exists(scene_manifest_with_suffix) != True || file_exists(scene_data_with_suffix) != True) {
        log_err("make_scene should produce both manifest and data files");
        ++failed;
    }

    if (make_ui_document(project_root, "hud") != Ok || make_ui_document(project_root, "pause.ui.conf") != Ok) {
        log_err("make_ui_document should create UI documents with and without extension");
        ++failed;
    }

    const usize warn_before_duplicate_ui = get_warn_count();
    const usize err_before_duplicate_ui = get_error_count();
    if (make_ui_document(project_root, "hud") != Err) {
        log_err("make_ui_document should refuse to overwrite existing UI documents");
        ++failed;
    }
    restore_diagnostic_counts(warn_before_duplicate_ui, err_before_duplicate_ui);

    char ui_contents[4096] = {0};
    if (read_file_contents(ui_doc_with_suffix, ui_contents, sizeof(ui_contents)) != Ok || strstr(ui_contents, "id = \"pause\"") == Null || strstr(ui_contents, "type = \"Canvas\"") == Null) {
        log_err("make_ui_document should trim .ui.conf suffix for document id and include a root canvas");
        ++failed;
    }

    if (file_exists(ui_doc) != True || file_exists(ui_doc_with_suffix) != True) {
        log_err("make_ui_document should create expected UI config files");
        ++failed;
    }

cleanup:
    remove_tree(project_root);
    (void)rmdir(temp_dir);

    record_test_result("Maker tests", failed);
#endif // TESTING
}
