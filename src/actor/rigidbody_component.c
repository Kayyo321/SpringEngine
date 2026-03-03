#include "rigidbody_component.h"

#include <string.h>

RigidbodyComponentData *actor_find_rigidbody_component(Actor *actor) {
    return ActorFindBuiltinComponentDataAs(actor, "Rigidbody", RigidbodyComponentData);
}

const char *rigidbody_body_type_to_string(RigidbodyBodyType body_type) {
    switch (body_type) {
        case RigidbodyBodyTypeDynamic:
            return "dynamic";
        case RigidbodyBodyTypeKinematic:
            return "kinematic";
        case RigidbodyBodyTypeStatic:
            return "static";
        default:
            return "dynamic";
    }
}

result rigidbody_body_type_from_string(const char *value, RigidbodyBodyType *out_body_type) {
    if (!value || value[0] == '\0' || !out_body_type)
        return Err;

    if (strcmp(value, "dynamic") == 0) {
        *out_body_type = RigidbodyBodyTypeDynamic;
        return Ok;
    }

    if (strcmp(value, "kinematic") == 0) {
        *out_body_type = RigidbodyBodyTypeKinematic;
        return Ok;
    }

    if (strcmp(value, "static") == 0) {
        *out_body_type = RigidbodyBodyTypeStatic;
        return Ok;
    }

    return Err;
}
