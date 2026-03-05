#include "program.h"
#include "testing/testing.h"

#include "config/runtime_loader.h"
#include "maker.h"
#include "packer.h"

#include <string.h>

static void log_usage(void) {
    log_msg("SpringEngine Usage:");
    log_msg("  --version | -v");
    log_msg("  --validate-config <project_path_or_archive.targame> | -vc <project_path_or_archive.targame>");
    log_msg("  --run <project_path_or_archive.targame> | -r <project_path_or_archive.targame>");
    log_msg("  --make-proj <path_to_put_it> <project_name> | -mp <path_to_put_it> <project_name>");
    log_msg("  --make-script <project_root> <script_name> | -ms <project_root> <script_name>");
    log_msg("  --make-scene <project_root> <scene_name> | -msc <project_root> <scene_name>");
    log_msg("  --make-ui-doc <project_root> <doc_name> | -mud <project_root> <doc_name>");
    log_msg("  --pack <source_directory> <archive.targame> | -p <source_directory> <archive.targame>");
    log_msg("  --unpack <archive.targame> <destination_directory> | -u <archive.targame> <destination_directory>");
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

#define ArgEqu(arg_index, arg_str) (program.argv[arg_index] && strcmp(program.argv[arg_index], arg_str) == 0)

    if (ArgEqu(1, "--version") || ArgEqu(1, "-v")) {
        version();
        return;
    }

    if (ArgEqu(1, "--run") || ArgEqu(1, "-r")) {
        if (program.argc < 3) {
            log_err("Missing project path for --run command");
            quit(Err);
        }

        const char *project_path = program.argv[2];
        run(project_path);

        return;
    }

    if (ArgEqu(1, "--validate-config") || ArgEqu(1, "-vc")) {
        if (program.argc < 3) {
            log_err("Missing project path for --validate-config command");
            quit(Err);
        }

        const char *project_path = program.argv[2];
        if (validate_project_configs(project_path) != Ok)
            quit(Err);

        return;
    }

    if (ArgEqu(1, "--make-proj") || ArgEqu(1, "-mp")) {
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

    if (ArgEqu(1, "--make-script") || ArgEqu(1, "-ms")) {
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

    if (ArgEqu(1, "--make-scene") || ArgEqu(1, "-msc")) {
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

    if (ArgEqu(1, "--make-ui-doc") || ArgEqu(1, "-mud")) {
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

    if (ArgEqu(1, "--pack") || ArgEqu(1, "-p")) {
        if (program.argc < 4) {
            log_err("Missing source directory and archive path for --pack command");
            quit(Err);
        }

        const char *source_directory = program.argv[2];
        const char *archive_path = program.argv[3];

        if (pack_directory_to_targame(source_directory, archive_path) != Ok) {
            log_err("Failed to pack directory '%s' into archive '%s'", source_directory, archive_path);
            quit(Err);
        }

        return;
    }

    if (ArgEqu(1, "--unpack") || ArgEqu(1, "-u")) {
        if (program.argc < 4) {
            log_err("Missing archive path and destination directory for --unpack command");
            quit(Err);
        }

        const char *archive_path = program.argv[2];
        const char *destination_directory = program.argv[3];

        if (unpack_targame_to_directory(archive_path, destination_directory) != Ok) {
            log_err("Failed to unpack archive '%s' into directory '%s'", archive_path, destination_directory);
            quit(Err);
        }

        return;
    }

    log_err("Unknown command '%s'", program.argv[1]);

#undef ArgEqu
}
