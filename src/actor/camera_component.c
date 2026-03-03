#include "camera_component.h"

CameraComponentData *actor_find_camera_component(Actor *actor) {
    return ActorFindBuiltinComponentDataAs(actor, "Camera", CameraComponentData);
}
