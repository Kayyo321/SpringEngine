#include "light_component.h"

PointLightComponentData *actor_find_point_light_component(Actor *actor) {
    return actor_find_builtin_component_data_as(actor, "PointLight", PointLightComponentData);
}

SpotLightComponentData *actor_find_spot_light_component(Actor *actor) {
    return actor_find_builtin_component_data_as(actor, "SpotLight", SpotLightComponentData);
}

DirectionLightComponentData *actor_find_direction_light_component(Actor *actor) {
    return actor_find_builtin_component_data_as(actor, "DirectionLight", DirectionLightComponentData);
}
