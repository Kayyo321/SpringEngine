#include "testing.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include "config/version_config.h"

#include "common.h"

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

static result vc_create_temp_dir(char *path_template, const char **out_temp_dir) {
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

static result vc_write_text_file(const char *path, const char *content) {
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

void run_version_config_tests(void) {
#ifdef Testing
    usize failed = 0;

    log_msg("Running version config tests...");

    if (version_check_project_requirement(Null, "0.1.0") != Err) {
        log_err("version_check_project_requirement should reject null project_root");
        ++failed;
    }

    if (version_check_project_requirement("/tmp", Null) != Err) {
        log_err("version_check_project_requirement should reject null engine_version");
        ++failed;
    }

    if (version_check_project_requirement("/tmp", "") != Err) {
        log_err("version_check_project_requirement should reject empty engine_version");
        ++failed;
    }

    char temp_dir_template[] = "/tmp/springengine-version-XXXXXX";
    const char *temp_dir = Null;
    if (vc_create_temp_dir(temp_dir_template, &temp_dir) != Ok || !temp_dir) {
        log_err("temp directory creation failed for version config tests");
        ++failed;
    } else {
        char version_conf_path[PATH_MAX] = {0};
        if (snprintf(version_conf_path, sizeof(version_conf_path), "%s/version.conf", temp_dir) >= (int)sizeof(version_conf_path)) {
            log_err("version config path too long in version config tests");
            ++failed;
        } else {
            // No version.conf -> Ok (optional file)
            {
                const usize w = get_warn_count();
                const usize e = get_error_count();
                if (version_check_project_requirement(temp_dir, "0.1.0") != Ok) {
                    log_err("version_check_project_requirement should pass when version.conf is absent");
                    ++failed;
                }
                restore_diagnostic_counts(w, e);
            }

            // '>' operator: engine newer than requirement -> Ok
            vc_write_text_file(version_conf_path,
                "[Version]\n"
                "springengine = \"> 0.0.9\"\n");
            if (version_check_project_requirement(temp_dir, "0.1.0") != Ok) {
                log_err("version_check_project_requirement should pass when engine > required");
                ++failed;
            }

            // '>' operator: engine equal to requirement -> Err
            vc_write_text_file(version_conf_path,
                "[Version]\n"
                "springengine = \"> 0.1.0\"\n");
            {
                const usize w = get_warn_count();
                const usize e = get_error_count();
                if (version_check_project_requirement(temp_dir, "0.1.0") != Err) {
                    log_err("version_check_project_requirement should fail when engine == required with '>' operator");
                    ++failed;
                }
                restore_diagnostic_counts(w, e);
            }

            // '>' operator: engine older than requirement -> Err
            vc_write_text_file(version_conf_path,
                "[Version]\n"
                "springengine = \"> 0.2.0\"\n");
            {
                const usize w = get_warn_count();
                const usize e = get_error_count();
                if (version_check_project_requirement(temp_dir, "0.1.0") != Err) {
                    log_err("version_check_project_requirement should fail when engine < required");
                    ++failed;
                }
                restore_diagnostic_counts(w, e);
            }

            // '=' operator: exact match -> Ok
            vc_write_text_file(version_conf_path,
                "[Version]\n"
                "springengine = \"= 0.1.1\"\n");
            if (version_check_project_requirement(temp_dir, "0.1.1") != Ok) {
                log_err("version_check_project_requirement should pass when engine == required with '=' operator");
                ++failed;
            }

            // '=' operator: mismatch -> Err
            vc_write_text_file(version_conf_path,
                "[Version]\n"
                "springengine = \"= 0.1.1\"\n");
            {
                const usize w = get_warn_count();
                const usize e = get_error_count();
                if (version_check_project_requirement(temp_dir, "0.1.0") != Err) {
                    log_err("version_check_project_requirement should fail when engine != required with '=' operator");
                    ++failed;
                }
                restore_diagnostic_counts(w, e);
            }

            // Multi-digit segment comparison: 0.1.10 > 0.1.9
            vc_write_text_file(version_conf_path,
                "[Version]\n"
                "springengine = \"> 0.1.9\"\n");
            if (version_check_project_requirement(temp_dir, "0.1.10") != Ok) {
                log_err("version_check_project_requirement should handle multi-digit version segments correctly");
                ++failed;
            }

            // Version.required key alias
            vc_write_text_file(version_conf_path,
                "[Version]\n"
                "required = \"= 0.1.0\"\n");
            if (version_check_project_requirement(temp_dir, "0.1.0") != Ok) {
                log_err("version_check_project_requirement should accept Version.required key alias");
                ++failed;
            }

            // Root-level springengine key (no [Version] table)
            vc_write_text_file(version_conf_path,
                "springengine = \"> 0.0.1\"\n");
            if (version_check_project_requirement(temp_dir, "0.1.0") != Ok) {
                log_err("version_check_project_requirement should accept root-level springengine key");
                ++failed;
            }

            // Missing requirement key -> Err
            vc_write_text_file(version_conf_path,
                "[Version]\n"
                "some_other_key = \"= 0.1.0\"\n");
            {
                const usize w = get_warn_count();
                const usize e = get_error_count();
                if (version_check_project_requirement(temp_dir, "0.1.0") != Err) {
                    log_err("version_check_project_requirement should fail when no recognized requirement key is present");
                    ++failed;
                }
                restore_diagnostic_counts(w, e);
            }

            // Invalid requirement format -> Err
            vc_write_text_file(version_conf_path,
                "[Version]\n"
                "springengine = \">> 0.1.0\"\n");
            {
                const usize w = get_warn_count();
                const usize e = get_error_count();
                if (version_check_project_requirement(temp_dir, "0.1.0") != Err) {
                    log_err("version_check_project_requirement should fail for malformed requirement format");
                    ++failed;
                }
                restore_diagnostic_counts(w, e);
            }

            (void)unlink(version_conf_path);
        }

        (void)rmdir(temp_dir);
    }

    record_test_result("Version config tests", failed);
#endif // Testing
}
