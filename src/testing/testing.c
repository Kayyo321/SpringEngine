#include "testing.h"

#include "common.h"

#include <string.h>

int tests_ran=0, tests_failed=0, tests_passed=0;

void record_test_result(const char *test_name, unsigned long failure_count) {
#ifdef Testing
    if (failure_count == 0) {
        ++tests_passed;
        log_msg("%s passed.", test_name ? test_name : "Test");
    } else {
        ++tests_failed;
        log_err("%s failed: %lu failure(s).", test_name ? test_name : "Test", failure_count);
    }

    ++tests_ran;
#else
    (void)test_name;
    (void)failure_count;
#endif // Testing
}

void run_all_tests(void) {
#ifdef Testing
    tests_ran = 0;
    tests_failed = 0;
    tests_passed = 0;

    run_allocator_tests();
    run_actor_tests();
    run_dj_tests();
    run_maker_tests();
    run_project_config_tests();
    run_version_config_tests();
    run_shader_config_tests();
    run_shader_registry_tests();
    run_material_registry_tests();
    run_ui_runtime_tests();
    run_runtime_loader_tests();
    run_script_runtime_tests();
    run_window_tests();
    run_vfs_and_packer_tests();

    log_msg("All tests completed. Summary: ran=%d passed=%d failed=%d", tests_ran, tests_passed, tests_failed);
#endif // Testing
}
