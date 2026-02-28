#ifndef WINDOWMAN_H
#define WINDOWMAN_H

#include "common.h"

#include "raylib.h"

typedef void (*UpdateCallback)(void);

typedef struct {
    char *title;
    usize width;
    usize height;

    usize target_fps;

    boolean resizable;

    Color clear_color;
} WindowConfig;

extern const WindowConfig DefaultWindowConfig;

result open_window(WindowConfig config);
boolean update_window(UpdateCallback callback);
void close_window(void);

#endif // WINDOWMAN_H
