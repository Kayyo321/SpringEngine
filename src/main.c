#include <stdlib.h>
#include <stdio.h>
#include <time.h>

#include "common.h"
#include "program.h"
#ifdef TESTING
#include "testing/testing.h"
#endif // TESTING

static double elapsed_seconds(const struct timespec start_time, const struct timespec end_time) {
    time_t seconds = end_time.tv_sec - start_time.tv_sec;
    long nanoseconds = end_time.tv_nsec - start_time.tv_nsec;

    if (nanoseconds < 0) {
        --seconds;
        nanoseconds += 1000000000L;
    }

    return (double)seconds + ((double)nanoseconds / 1000000000.0);
}

static void spring_engine(void) {
    if (open_logger() != Ok)
        quit(Err);

    log_msg("%s is starting", program.title);

    {
        struct timespec start_time;
        struct timespec end_time;
        clock_gettime(CLOCK_MONOTONIC, &start_time);

#ifdef TESTING
        run_all_tests();
#else
        run_program();
#endif // TESTING

        clock_gettime(CLOCK_MONOTONIC, &end_time);
        const double elapsed = elapsed_seconds(start_time, end_time);
        log_msg("Program executed in %.3f seconds.", elapsed);
    }

    log_msg("Finished with %lu warnings and %lu errors.", get_warn_count(), get_error_count());
    
    quit(get_error_count() > 0);
}

int main(int argc, char **argv) {
    program = (Program) {
#ifdef SPRINGENGINE_DEBUG
        .title = "SpringEngine-" Version " (Debug-Non-Release!!!)",
#else
        .title = "SpringEngine-" Version,
#endif // SPRINGENGINE_DEBUG
        .argc = argc,
        .argv = argv,
    };
    
    spring_engine();
}
