#include "runtime_loader_internal.h"

result runtime_instantiate_prefab(const char *prefab_ref_id, boolean has_position, float x, float y, float z, char *out_actor_id, usize out_actor_id_size) {
    RuntimeGuardActiveErr();

    return enqueue_prefab_instantiation(prefab_ref_id, has_position, x, y, z, out_actor_id, out_actor_id_size);
}

result runtime_destroy_actor(const char *actor_id) {
    RuntimeGuardActiveErr();
    if (!actor_id || actor_id[0] == '\0')
        return Err;

    Actor *actor = find_actor_by_id(actor_id);
    if (!actor)
        return Err;

    actor->pending_destroy = True;
    return Ok;
}

result runtime_set_actor_destroy_on_load(const char *actor_id, boolean destroy_on_load) {
    RuntimeGuardActiveErr();
    if (!actor_id || actor_id[0] == '\0')
        return Err;

    Actor *actor = find_actor_by_id(actor_id);
    if (!actor)
        return Err;

    actor->destroy_on_load = destroy_on_load ? True : False;
    return Ok;
}

result runtime_get_actor_destroy_on_load(const char *actor_id, boolean *out_destroy_on_load) {
    RuntimeGuardActiveErr();
    if (!actor_id || actor_id[0] == '\0' || !out_destroy_on_load)
        return Err;

    Actor *actor = find_actor_by_id(actor_id);
    if (!actor)
        return Err;

    *out_destroy_on_load = actor->destroy_on_load ? True : False;
    return Ok;
}
