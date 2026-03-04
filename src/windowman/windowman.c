#include "windowman.h"

#define DefaultWindowConfigLiteral { \
    .title = "SpringEngine-" Version " Window", \
    .width = 900, \
    .height = 600, \
    .target_fps = 60, \
    .resizable = True, \
    .clear_color = {80, 80, 80, 255}, \
} \

// Extern
const WindowConfig DefaultWindowConfig = DefaultWindowConfigLiteral;

static WindowConfig current_config = DefaultWindowConfigLiteral;

#ifdef Testing
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
#endif // Testing

result open_window(WindowConfig config) {
#ifdef Testing
    ensure_hooks();

    SetTraceLogLevel(LOG_NONE);

    hooks.init_window((int)config.width, (int)config.height, config.title);
    if (!hooks.is_window_ready()) {
#else
    SetTraceLogLevel(LOG_NONE);
    InitWindow(config.width, config.height, config.title);
    if (!IsWindowReady()) {
#endif // Testing
        log_err("Failed to initialize the window (title='%s', size=%lux%lu).", config.title ? config.title : "<null>", config.width, config.height);
        return Err;
    }

#ifdef Testing
    hooks.set_window_state(config.resizable ? FLAG_WINDOW_RESIZABLE : 0);
#else
    SetWindowState(config.resizable ? FLAG_WINDOW_RESIZABLE : 0);
#endif // Testing

    current_config = config;

    return Ok;
}

boolean update_window(UpdateCallback callback) {
#ifdef Testing
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
#endif // Testing

    return True;
}

void set_window_clear_color(Color color) {
    current_config.clear_color = color;
}

void close_window(void) {
#ifdef Testing
    ensure_hooks();
    hooks.close_window();
#else
    CloseWindow();
#endif // Testing
}
