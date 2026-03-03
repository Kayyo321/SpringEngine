#ifndef TESTING_H
#define TESTING_H

extern int tests_ran, tests_failed, tests_passed;

void record_test_result(const char *test_name, unsigned long failure_count);

void run_allocator_tests(void);
void run_actor_tests(void);
void run_dj_tests(void);
void run_maker_tests(void);
void run_project_config_tests(void);
void run_ui_runtime_tests(void);
void run_runtime_loader_tests(void);
void run_script_runtime_tests(void);
void run_window_tests(void);
void run_vfs_and_packer_tests(void);

void run_all_tests(void);

#endif // TESTING_H
