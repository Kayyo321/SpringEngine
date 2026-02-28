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

result open_window(WindowConfig config) {
    InitWindow(config.width, config.height, config.title);
    SetWindowState(config.resizable ? FLAG_WINDOW_RESIZABLE : 0);

    if (!IsWindowReady()) {
        log_err("Failed to initialize the window.");
        return Err;
    }

    current_config = config;

    return Ok;
}

boolean update_window(UpdateCallback callback) {
    if (!WindowShouldClose()) {
        BeginDrawing();

        ClearBackground(current_config.clear_color);

        callback();

        EndDrawing();

        return False;
    } 

    return True;
}

void close_window(void) {
    CloseWindow();
}
