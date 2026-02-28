#include <stdlib.h>
#include <stdio.h>
#include <time.h>

#include "common.h"
#include "program.h"

static void spring_engine(void) {
    if (open_logger() != Ok)
        quit(Err);

    log_msg("%s is starting", program.title);

    {
        const time_t start_time = time(Null);

#ifdef TESTING
        run_all_tests();
#endif // TESTING

        run_program();

        const time_t end_time = time(Null);
        const double elapsed = difftime(end_time, start_time);
        log_msg("Program executed in %.2f seconds.", elapsed);
    }

    log_msg("Finished with %lu warnings and %lu errors.", get_warn_count(), get_error_count());
    
    quit(get_error_count() > 0);
}

int main(int argc, char **argv) {
    program = (Program) {
        .title = "SpringEngine-" Version,
        .argc = argc,
        .argv = argv,
    };
    
    spring_engine();
}
