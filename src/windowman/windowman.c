#include "windowman.h"

const WindowConfig DefaultWindowConfig = (WindowConfig) {
    .title = "SpringEngine-" Version " Window",
    .width = 900,
    .height = 600,
    .target_fps = 60,
    .resizable = True,
    .clear_color = DARKGRAY,
};

static WindowConfig current_config = DefaultWindowConfig;

#ifdef TESTING
static void default_init_window(int width, int height, const char *title) {
    InitWindow(width, height, title);
}

static void default_set_window_state(unsigned int flags) {
    SetWindowState(flags);
}

static int default_is_window_ready(void) {
    return IsWindowReady();
}

static int default_window_should_close(void) {
    return WindowShouldClose();
}

static void default_begin_drawing(void) {
    BeginDrawing();
}

static void default_clear_background(Color color) {
    ClearBackground(color);
}

static void default_end_drawing(void) {
    EndDrawing();
}

static void default_close_window(void) {
    CloseWindow();
}

static const WindowmanTestHooks default_hooks = {
    .init_window = default_init_window,
    .set_window_state = default_set_window_state,
    .is_window_ready = default_is_window_ready,
    .window_should_close = default_window_should_close,
    .begin_drawing = default_begin_drawing,
    .clear_background = default_clear_background,
    .end_drawing = default_end_drawing,
    .close_window = default_close_window,
};

static WindowmanTestHooks hooks;
static boolean hooks_initialized = False;

static void ensure_hooks(void) {
    if (!hooks_initialized) {
        hooks = default_hooks;
        hooks_initialized = True;
    }
}

void windowman_set_test_hooks(const WindowmanTestHooks *test_hooks) {
    ensure_hooks();

    if (test_hooks != Null) {
        hooks = *test_hooks;
    }
}

void windowman_reset_test_hooks(void) {
    hooks = default_hooks;
    hooks_initialized = True;
}
#endif // TESTING

result open_window(WindowConfig config) {
#ifdef TESTING
    ensure_hooks();

    hooks.init_window((int)config.width, (int)config.height, config.title);
    hooks.set_window_state(config.resizable ? FLAG_WINDOW_RESIZABLE : 0);

    if (!hooks.is_window_ready()) {
#else
    InitWindow(config.width, config.height, config.title);
    SetWindowState(config.resizable ? FLAG_WINDOW_RESIZABLE : 0);

    if (!IsWindowReady()) {
#endif // TESTING
        log_err("Failed to initialize the window.");
        return Err;
    }

    current_config = config;

    return Ok;
}

boolean update_window(UpdateCallback callback) {
#ifdef TESTING
    ensure_hooks();

    if (!hooks.window_should_close()) {
        hooks.begin_drawing();

        hooks.clear_background(current_config.clear_color);

        callback();

        hooks.end_drawing();

        return False;
    }
#else
    if (!WindowShouldClose()) {
        BeginDrawing();

        ClearBackground(current_config.clear_color);

        callback();

        EndDrawing();

        return False;
    } 
#endif // TESTING

    return True;
}

void close_window(void) {
#ifdef TESTING
    ensure_hooks();
    hooks.close_window();
#else
    CloseWindow();
#endif // TESTING
}
