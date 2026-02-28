#include "program.h"
#include "testing/testing.h"

#include "actor/actor.h"
#include "config/project_config.h"
#include "windowman/windowman.h"

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
    log_err("Running project '%s' is not implemented yet", project_path);
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

    log_err("Unknown command '%s'", program.argv[1]);

#undef arg_equ
}
