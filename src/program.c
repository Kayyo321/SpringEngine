#include "program.h"
#include "testing/testing.h"

#include "windowman/windowman.h"

void test_update(void) {
    DrawText("Running tests...", 10, 10, 20, BLACK);
}

void run_program(void) {
#ifndef TESTING
	open_window(DefaultWindowConfig);

    do {
        //
    } while (!update_window(test_update));

    close_window();
#else 
    run_all_tests();
#endif // TESTING
}
