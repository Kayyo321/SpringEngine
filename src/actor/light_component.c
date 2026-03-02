#include "light_component.h"

#include <string.h>

PointLightComponentData *actor_find_point_light_component(Actor *actor) {
    if (!actor)
        return Null;

    for (usize component_index = 0; component_index < actor->component_count; ++component_index) {
        ActorComponent *component = &actor->components[component_index];
        if (component->descriptor.kind == ComponentBuiltin && component->descriptor.name && strcmp(component->descriptor.name, "PointLight") == 0)
            return (PointLightComponentData *)component->data;
    }

    return Null;
}

SpotLightComponentData *actor_find_spot_light_component(Actor *actor) {
    if (!actor)
        return Null;

    for (usize component_index = 0; component_index < actor->component_count; ++component_index) {
        ActorComponent *component = &actor->components[component_index];
        if (component->descriptor.kind == ComponentBuiltin && component->descriptor.name && strcmp(component->descriptor.name, "SpotLight") == 0)
            return (SpotLightComponentData *)component->data;
    }

    return Null;
}

DirectionLightComponentData *actor_find_direction_light_component(Actor *actor) {
    if (!actor)
        return Null;

    for (usize component_index = 0; component_index < actor->component_count; ++component_index) {
        ActorComponent *component = &actor->components[component_index];
        if (component->descriptor.kind == ComponentBuiltin && component->descriptor.name && strcmp(component->descriptor.name, "DirectionLight") == 0)
            return (DirectionLightComponentData *)component->data;
    }

    return Null;
}
