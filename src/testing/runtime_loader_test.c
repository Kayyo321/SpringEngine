#include "testing.h"

#include "config/runtime_loader.h"

#include "common.h"

void run_runtime_loader_tests(void) {
#ifdef TESTING
    usize failed = 0;

    log_msg("Running runtime loader tests...");

    if (runtime_current_scene_path() != Null) {
        log_err("runtime_current_scene_path should return null when runtime is not active");
        ++failed;
    }

    if (runtime_request_scene_load("starting_scene.scene.conf") != Err) {
        log_err("runtime_request_scene_load should reject requests while runtime is inactive");
        ++failed;
    }

    if (runtime_request_scene_load(Null) != Err) {
        log_err("runtime_request_scene_load should reject null scene paths");
        ++failed;
    }

    if (runtime_request_scene_load("") != Err) {
        log_err("runtime_request_scene_load should reject empty scene paths");
        ++failed;
    }

    record_test_result("Runtime loader tests", failed);
#endif // TESTING
}
