#include "runtime_loader_internal.h"

#include <stdio.h>

result runtime_request_scene_load(const char *scene_path) {
    RuntimeGuardActiveErr();
    if (!scene_path || scene_path[0] == '\0')
        return Err;

    if (snprintf(runtime_state.pending_scene_path, sizeof(runtime_state.pending_scene_path), "%s", scene_path) >= (int)sizeof(runtime_state.pending_scene_path)) {
        log_err("Requested scene path is too long: '%s'", scene_path);
        return Err;
    }

    runtime_state.has_pending_scene_load = True;
    return Ok;
}

const char *runtime_current_scene_path(void) {
    RuntimeGuardActiveNull();

    if (!runtime_state.current_scene_path[0])
        return Null;

    return runtime_state.current_scene_path;
}
