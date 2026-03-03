#include "light_component.h"

PointLightComponentData *actor_find_point_light_component(Actor *actor) {
    return ActorFindBuiltinComponentDataAs(actor, "PointLight", PointLightComponentData);
}

SpotLightComponentData *actor_find_spot_light_component(Actor *actor) {
    return ActorFindBuiltinComponentDataAs(actor, "SpotLight", SpotLightComponentData);
}

DirectionLightComponentData *actor_find_direction_light_component(Actor *actor) {
    return ActorFindBuiltinComponentDataAs(actor, "DirectionLight", DirectionLightComponentData);
}
