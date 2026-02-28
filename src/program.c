#include "program.h"

#include "windowman/windowman.h"

void test_update(void) {
    DrawText("Running tests...", 10, 10, 20, BLACK);
}

void run_program(void) {
	open_window(DefaultWindowConfig);

    do {
        //
    } while (!update_window(test_update));

    close_window();
}
