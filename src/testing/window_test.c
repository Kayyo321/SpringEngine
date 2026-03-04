#include "testing.h"

#include "../windowman/windowman.h"

#include "common.h"

#include <string.h>

typedef struct {
    int init_called;
    int set_state_called;
    unsigned int last_window_state;
    int ready;

    int should_close;
    int begin_called;
    int clear_called;
    Color last_clear_color;
    int end_called;

    int callback_called;
    int close_called;

    int last_width;
    int last_height;
    const char *last_title;
} WindowTestState;

static WindowTestState state;

static void reset_state(void) {
    memset(&state, 0, sizeof(state));
    state.ready = 1;
}

static void mock_init_window(int width, int height, const char *title) {
    ++state.init_called;
    state.last_width = width;
    state.last_height = height;
    state.last_title = title;
}

static void mock_set_window_state(unsigned int flags) {
    ++state.set_state_called;
    state.last_window_state = flags;
}

static int mock_is_window_ready(void) {
    return state.ready;
}

static int mock_window_should_close(void) {
    return state.should_close;
}

static void mock_begin_drawing(void) {
    ++state.begin_called;
}

static void mock_clear_background(Color color) {
    ++state.clear_called;
    state.last_clear_color = color;
}

static void mock_end_drawing(void) {
    ++state.end_called;
}

static void mock_close_window(void) {
    ++state.close_called;
}

static WindowmanTestHooks mock_hooks(void) {
    return (WindowmanTestHooks) {
        .init_window = mock_init_window,
        .set_window_state = mock_set_window_state,
        .is_window_ready = mock_is_window_ready,
        .window_should_close = mock_window_should_close,
        .begin_drawing = mock_begin_drawing,
        .clear_background = mock_clear_background,
        .end_drawing = mock_end_drawing,
        .close_window = mock_close_window,
    };
}

static void test_callback(void) {
    ++state.callback_called;
}

void run_window_tests(void) {
#ifdef Testing
    usize failed = 0;

    log_msg("Running windowman tests...");

    reset_state();
    WindowmanTestHooks hooks = mock_hooks();
    windowman_set_test_hooks(&hooks);

    WindowConfig config = DefaultWindowConfig;
    config.title = "Windowman Test";
    config.width = 1280;
    config.height = 720;
    config.resizable = True;
    config.clear_color = SKYBLUE;

    if (open_window(config) != Ok) {
        log_err("open_window should return Ok when window is ready");
        ++failed;
    }

    if (state.init_called != 1 || state.last_width != 1280 || state.last_height != 720 || strcmp(state.last_title, "Windowman Test") != 0) {
        log_err("open_window should pass correct config to InitWindow");
        ++failed;
    }

    if (state.set_state_called != 1 || state.last_window_state != FLAG_WINDOW_RESIZABLE) {
        log_err("open_window should set resizable window flag");
        ++failed;
    }

    state.should_close = 0;
    if (update_window(test_callback) != False) {
        log_err("update_window should return False while window should stay open");
        ++failed;
    }

    if (state.begin_called != 1 || state.clear_called != 1 || state.end_called != 1 || state.callback_called != 1) {
        log_err("update_window should draw exactly once and invoke callback");
        ++failed;
    }

    if (state.last_clear_color.r != SKYBLUE.r || state.last_clear_color.g != SKYBLUE.g || state.last_clear_color.b != SKYBLUE.b || state.last_clear_color.a != SKYBLUE.a) {
        log_err("update_window should clear using current config color");
        ++failed;
    }

    state.should_close = 1;
    if (update_window(test_callback) != True) {
        log_err("update_window should return True when window should close");
        ++failed;
    }

    if (state.begin_called != 1 || state.clear_called != 1 || state.end_called != 1 || state.callback_called != 1) {
        log_err("update_window should not draw or call callback when closing");
        ++failed;
    }

    close_window();
    if (state.close_called != 1) {
        log_err("close_window should call CloseWindow once");
        ++failed;
    }

    reset_state();
    state.ready = 0;
    hooks = mock_hooks();
    windowman_set_test_hooks(&hooks);

    config.resizable = False;

    const usize warn_before_expected_window_err = get_warn_count();
    const usize err_before_expected_window_err = get_error_count();

    if (open_window(config) != Err) {
        log_err("open_window should return Err when IsWindowReady is false");
        ++failed;
    }

    restore_diagnostic_counts(warn_before_expected_window_err, err_before_expected_window_err);

    if (state.set_state_called != 0) {
        log_err("open_window should not set window state when initialization fails");
        ++failed;
    }

    windowman_reset_test_hooks();

    record_test_result("Windowman tests", failed);
#endif // Testing
}
