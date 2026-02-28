#include "camera_component.h"

#include <string.h>

CameraComponentData *actor_find_camera_component(Actor *actor) {
    if (!actor)
        return Null;

    for (usize component_index = 0; component_index < actor->component_count; ++component_index) {
        ActorComponent *component = &actor->components[component_index];
        if (component->descriptor.kind == ComponentBuiltin && component->descriptor.name && strcmp(component->descriptor.name, "Camera") == 0)
            return (CameraComponentData *)component->data;
    }

    return Null;
}
