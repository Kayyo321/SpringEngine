#include "testing.h"

#include "common.h"

#include <string.h>

void run_allocator_tests(void) {
#ifdef Testing
    usize failed = 0;

    log_msg("Running allocator tests...");

    Heap a = allocate(16, sizeof(unsigned char));
    if (!a.pointer || a.size != 16) {
        log_err("allocate failed basic contract (pointer=%p, size=%lu)", a.pointer, a.size);
        ++failed;
    }

    unsigned char expected[64];
    memset(expected, 0, sizeof(expected));

    if (a.pointer) {
        memset(a.pointer, 0xAB, 16);
        memset(expected, 0xAB, 16);
    }

    a = reallocate(a, 64);
    if (!a.pointer || a.size != 64) {
        log_err("reallocate grow failed contract (pointer=%p, size=%lu)", a.pointer, a.size);
        ++failed;
    } else if (memcmp(a.pointer, expected, 16) != 0) {
        log_err("reallocate grow did not preserve initial bytes");
        ++failed;
    }

    if (a.pointer) {
        memset((unsigned char *)a.pointer + 16, 0xCD, 48);
        memset(expected + 16, 0xCD, 48);
    }

    a = reallocate(a, 8);
    if (!a.pointer || a.size != 8) {
        log_err("reallocate shrink failed contract (pointer=%p, size=%lu)", a.pointer, a.size);
        ++failed;
    } else if (memcmp(a.pointer, expected, 8) != 0) {
        log_err("reallocate shrink did not preserve prefix bytes");
        ++failed;
    }

    deallocate(a);

    Heap first = allocate(4, sizeof(unsigned int));
    Heap middle = allocate(32, 1);
    Heap last = allocate(64, 1);

    deallocate(middle);
    deallocate(first);
    deallocate(last);

    if (scan_and_deallocate() != Ok) {
        log_err("scan_and_deallocate should return Ok when no heaps are leaked");
        ++failed;
    }

    Heap leak = allocate(5, 5);
    if (!leak.pointer) {
        log_err("failed to create intentional leak test heap");
        ++failed;
    }

    const usize warn_before_expected_leak = get_warn_count();
    const usize err_before_expected_leak = get_error_count();

    if (scan_and_deallocate() != Err) {
        log_err("scan_and_deallocate should return Err when leaks are present");
        ++failed;
    }

    restore_diagnostic_counts(warn_before_expected_leak, err_before_expected_leak);

    if (scan_and_deallocate() != Ok) {
        log_err("scan_and_deallocate should return Ok after leaked heaps are reclaimed");
        ++failed;
    }

    record_test_result("Allocator tests", failed);
#endif // Testing
}
