#include "testing.h"

#include "common.h"

#include <string.h>

void run_all_tests(void) {
#ifdef TESTING
    run_allocator_tests();
    run_window_tests();

    log_msg("All tests completed.");
#endif // TESTING
}
