#include "program.h"
#include "testing/testing.h"

#include "config/runtime_loader.h"
#include "maker.h"

#include <string.h>

static void log_usage(void) {
    log_msg("SpringEngine Usage:");
    log_msg("  --version | -v");
    log_msg("  --run <project_path> | -r <project_path>");
}

static void version(void) {
    log_msg("SpringEngine version is %s", program.title);
}

static void run(const char *project_path) {
    if (run_project_runtime(project_path) != Ok)
        quit(Err);
}

void run_program(void) {
    if (program.argc < 2) {
        log_usage();
        return;
    }

#define arg_equ(arg_index, arg_str) (program.argv[arg_index] && strcmp(program.argv[arg_index], arg_str) == 0)

    if (arg_equ(1, "--version") || arg_equ(1, "-v")) {
        version();
        return;
    }

    if (arg_equ(1, "--run") || arg_equ(1, "-r")) {
        if (program.argc < 3) {
            log_err("Missing project path for --run command");
            quit(Err);
        }

        const char *project_path = program.argv[2];
        run(project_path);

        return;
    }

    if (arg_equ(1, "--make-proj") || arg_equ(1, "-mp")) {
        if (program.argc < 4) {
            log_err("Missing path to put the project for --make-proj command && project name");
            quit(Err);
        }

        const char *path_to_put_it = program.argv[2];
        const char *project_name = program.argv[3];

        if (make_project(path_to_put_it, project_name) != Ok) {
            log_err("Failed to create project '%s' at path '%s'", project_name, path_to_put_it);
            quit(Err);
        }

        return;
    }

    if (arg_equ(1, "--make-script") || arg_equ(1, "-ms")) {
        if (program.argc < 4) {
            log_err("Missing path to project root to put the script for --make-script command && script name");
            quit(Err);
        }

        const char *project_root = program.argv[2];
        const char *script_name = program.argv[3];

        if (make_script(project_root, script_name) != Ok) {
            log_err("Failed to create script '%s' at path '%s'", script_name, project_root);
            quit(Err);
        }

        return;
    }

    if (arg_equ(1, "--make-scene") || arg_equ(1, "-msc")) {
        if (program.argc < 4) {
            log_err("Missing path to project root to put the scene for --make-scene command && scene name");
            quit(Err);
        }

        const char *project_root = program.argv[2];
        const char *scene_name = program.argv[3];

        if (make_scene(project_root, scene_name) != Ok) {
            log_err("Failed to create scene '%s' at path '%s'", scene_name, project_root);
            quit(Err);
        }

        return;
    }

    if (arg_equ(1, "--make-ui-doc") || arg_equ(1, "-mud")) {
        if (program.argc < 4) {
            log_err("Missing path to project root to put the ui doc for --make-ui-doc command && doc name");
            quit(Err);
        }

        const char *project_root = program.argv[2];
        const char *doc_name = program.argv[3];

        if (make_ui_document(project_root, doc_name) != Ok) {
            log_err("Failed to create UI doc '%s' at path '%s'", doc_name, project_root);
            quit(Err);
        }

        return;
    }

    log_err("Unknown command '%s'", program.argv[1]);

#undef arg_equ
}
