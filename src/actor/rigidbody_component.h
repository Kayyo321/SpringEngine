#ifndef RIGIDBODY_COMPONENT_H
#define RIGIDBODY_COMPONENT_H

#include "actor.h"

typedef enum {
    RigidbodyBodyTypeDynamic,
    RigidbodyBodyTypeKinematic,
    RigidbodyBodyTypeStatic,
} RigidbodyBodyType;

typedef struct {
    RigidbodyBodyType body_type;
    boolean simulated;
    boolean use_gravity;
    float mass;
    float inverse_mass;
    float gravity_scale;
    float linear_drag;
    float angular_drag;
    float velocity_x;
    float velocity_y;
    float angular_velocity;
    float force_x;
    float force_y;
    float torque;
    boolean freeze_position_x;
    boolean freeze_position_y;
    boolean freeze_rotation;
    boolean is_grounded;
    Heap heap;
} RigidbodyComponentData;

RigidbodyComponentData *actor_find_rigidbody_component(Actor *actor);
const char *rigidbody_body_type_to_string(RigidbodyBodyType body_type);
result rigidbody_body_type_from_string(const char *value, RigidbodyBodyType *out_body_type);

#endif // RIGIDBODY_COMPONENT_H
