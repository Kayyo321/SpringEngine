#ifndef WindowmanH
#define WindowmanH

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

#ifdef TESTING
typedef struct {
    void (*init_window)(int width, int height, const char *title);
    void (*set_window_state)(unsigned int flags);
    int (*is_window_ready)(void);
    int (*window_should_close)(void);
    void (*begin_drawing)(void);
    void (*clear_background)(Color color);
    void (*end_drawing)(void);
    void (*close_window)(void);
} WindowmanTestHooks;
#endif // TESTING

extern const WindowConfig DefaultWindowConfig;

result open_window(WindowConfig config);
boolean update_window(UpdateCallback callback);
void set_window_clear_color(Color color);
void close_window(void);

#ifdef TESTING
void windowman_set_test_hooks(const WindowmanTestHooks *hooks);
void windowman_reset_test_hooks(void);
#endif // TESTING

#endif // WINDOWMAN_H
