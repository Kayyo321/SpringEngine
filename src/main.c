#include <stdlib.h>
#include <stdio.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#endif

#include "common.h"
#include "program.h"
#ifdef Testing
#include "testing/testing.h"
#endif // Testing

static double get_timestamp_seconds(void) {
#ifdef _WIN32
    static LARGE_INTEGER frequency = {0};
    LARGE_INTEGER counter;

    if (frequency.QuadPart == 0)
        QueryPerformanceFrequency(&frequency);

    QueryPerformanceCounter(&counter);
    return (double)counter.QuadPart / (double)frequency.QuadPart;
#else
    struct timespec timestamp = {0};
    clock_gettime(CLOCK_MONOTONIC, &timestamp);
    return (double)timestamp.tv_sec + ((double)timestamp.tv_nsec / 1000000000.0);
#endif
}

static void spring_engine(void) {
    if (open_logger() != Ok)
        quit(Err);

    log_msg("%s is starting", program.title);

    {
        const double start_time = get_timestamp_seconds();

#ifdef Testing
        run_all_tests();
#else
        run_program();
#endif // Testing

        const double end_time = get_timestamp_seconds();
        const double elapsed = end_time - start_time;
        log_msg("Program executed in %.3f seconds.", elapsed);
    }

    log_msg("Finished with %lu warnings and %lu errors.", get_warn_count(), get_error_count());
    
    quit(get_error_count() > 0);
}

int main(int argc, char **argv) {
    program = (Program) {
        .title = "SpringEngine-" Version
#ifdef Debug
         " (Debug-Non-Release!!!)"
#elif Testing
         " (Testing-Non-Release!!!)"
#endif
    ,
        .argc = argc,
        .argv = argv,
    };
    
    spring_engine();
}
