#include "camera_component.h"

CameraComponentData *actor_find_camera_component(Actor *actor) {
    return actor_find_builtin_component_data_as(actor, "Camera", CameraComponentData);
}
