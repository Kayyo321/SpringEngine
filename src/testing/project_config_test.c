#include "testing.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "config/project_config.h"

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

void run_project_config_tests(void) {
#ifdef TESTING
    usize failed = 0;

    log_msg("Running project config tests...");

    char temp_dir_template[] = "/tmp/springengine-project-config-XXXXXX";
    const char *temp_dir = Null;
    if (create_temp_test_dir(temp_dir_template, sizeof(temp_dir_template), &temp_dir) != Ok || !temp_dir) {
        log_err("temp directory creation failed for project config tests");
        ++failed;
    } else {
        char valid_conf_path[512] = {0};
        char invalid_conf_path[512] = {0};
        if (snprintf(valid_conf_path, sizeof(valid_conf_path), "%s/springengine-valid.conf", temp_dir) >= (int)sizeof(valid_conf_path)) {
            log_err("failed to compose valid config path");
            ++failed;
        } else {
            FILE *valid_conf = fopen(valid_conf_path, "w");
            if (!valid_conf) {
                log_err("failed to create valid config file");
                ++failed;
            } else {
                fputs(
                    "[Window]\n"
                    "title = \"Unit Test Window\"\n"
                    "width = 1024\n"
                    "height = 576\n"
                    "target_fps = 144\n"
                    "resizable = true\n",
                    valid_conf);
                fclose(valid_conf);

                WindowConfig loaded = {0};
                if (load_project_window_config(valid_conf_path, &loaded) != Ok) {
                    log_err("load_project_window_config should parse a valid [Window] table");
                    ++failed;
                } else {
                    if (!loaded.title || strcmp(loaded.title, "Unit Test Window") != 0) {
                        log_err("window title should match parsed config value");
                        ++failed;
                    }

                    if (loaded.width != 1024 || loaded.height != 576) {
                        log_err("window dimensions should match parsed config values");
                        ++failed;
                    }

                    if (loaded.target_fps != 144 || loaded.resizable != True) {
                        log_err("window flags should match parsed config values");
                        ++failed;
                    }
                }
            }
        }

        if (snprintf(invalid_conf_path, sizeof(invalid_conf_path), "%s/springengine-missing-window.conf", temp_dir) >= (int)sizeof(invalid_conf_path)) {
            log_err("failed to compose invalid config path");
            ++failed;
        } else {
            FILE *invalid_conf = fopen(invalid_conf_path, "w");
            if (!invalid_conf) {
                log_err("failed to create invalid config file");
                ++failed;
            } else {
                fputs(
                    "[Boot]\n"
                    "first_scene = \"starting_scene.scene.conf\"\n",
                    invalid_conf);
                fclose(invalid_conf);

                WindowConfig loaded = {
                    .title = "placeholder",
                    .width = 1,
                    .height = 1,
                    .target_fps = 1,
                    .resizable = True,
                    .clear_color = RED,
                };

                const usize warn_before_expected_missing_window = get_warn_count();
                const usize err_before_expected_missing_window = get_error_count();

                if (load_project_window_config(invalid_conf_path, &loaded) != Err) {
                    log_err("load_project_window_config should return Err when [Window] is missing");
                    ++failed;
                }

                restore_diagnostic_counts(warn_before_expected_missing_window, err_before_expected_missing_window);

                if (loaded.width != DefaultWindowConfig.width || loaded.height != DefaultWindowConfig.height ||
                    loaded.target_fps != DefaultWindowConfig.target_fps || loaded.resizable != DefaultWindowConfig.resizable) {
                    log_err("output config should reset to defaults when parsing fails");
                    ++failed;
                }
            }
        }

        (void)unlink(valid_conf_path);
        (void)unlink(invalid_conf_path);
        (void)rmdir(temp_dir);
    }

    record_test_result("Project config tests", failed);
#endif // TESTING
}
