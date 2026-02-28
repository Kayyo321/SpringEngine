#include "actor.h"

#include <string.h>

static result ensure_actor_component_capacity(Actor *actor, usize required_capacity) {
    if (!actor)
        return Err;

    if (required_capacity <= actor->component_capacity)
        return Ok;

    usize next_capacity = actor->component_capacity == 0 ? 4 : actor->component_capacity * 2;
    while (next_capacity < required_capacity)
        next_capacity *= 2;

    const usize next_size = next_capacity * sizeof(ActorComponent);

    if (!actor->components) {
        actor->components_heap = allocate(next_capacity, sizeof(ActorComponent));
        actor->components = (ActorComponent *)actor->components_heap.pointer;
    } else {
        actor->components_heap = reallocate(actor->components_heap, next_size);
        actor->components = (ActorComponent *)actor->components_heap.pointer;
    }

    actor->component_capacity = next_capacity;
    return Ok;
}

void actor_init(Actor *actor, char *id, boolean enabled, int layer) {
    if (!actor)
        return;

    actor->id = id;
    actor->enabled = enabled;
    actor->layer = layer;
    actor->components_heap = NullHeap;
    actor->components = Null;
    actor->component_count = 0;
    actor->component_capacity = 0;
}

void actor_dispose(Actor *actor) {
    if (!actor)
        return;

    if (actor->components_heap.pointer)
        deallocate(actor->components_heap);

    actor->components_heap = NullHeap;
    actor->components = Null;
    actor->component_count = 0;
    actor->component_capacity = 0;
}

result actor_add_component(Actor *actor, ComponentDescriptor descriptor, void *data) {
    if (!actor || !descriptor.name)
        return Err;

    if (ensure_actor_component_capacity(actor, actor->component_count + 1) != Ok)
        return Err;

    ActorComponent component = {
        .descriptor = descriptor,
        .data = data,
    };

    actor->components[actor->component_count++] = component;
    return Ok;
}

result actor_initialize_components(Actor *actor) {
    if (!actor)
        return Err;

    for (usize component_index = 0; component_index < actor->component_count; ++component_index) {
        ActorComponent *component = &actor->components[component_index];

        if (component->descriptor.initialize) {
            if (component->descriptor.initialize(actor, component, component->descriptor.context) != Ok) {
                log_err("Failed to initialize component '%s' (%s) on actor '%s'", component->descriptor.name, component_kind_name(component->descriptor.kind), actor->id ? actor->id : "<unknown>");
                return Err;
            }
        }
    }

    return Ok;
}

static result ensure_registry_actor_capacity(ActorRegistry *registry, usize required_capacity) {
    if (!registry)
        return Err;

    if (required_capacity <= registry->actor_capacity)
        return Ok;

    usize next_capacity = registry->actor_capacity == 0 ? 4 : registry->actor_capacity * 2;
    while (next_capacity < required_capacity)
        next_capacity *= 2;

    const usize next_size = next_capacity * sizeof(Actor);

    if (!registry->actors) {
        registry->actors_heap = allocate(next_capacity, sizeof(Actor));
        registry->actors = (Actor *)registry->actors_heap.pointer;
        memset(registry->actors, 0, next_size);
    } else {
        const usize old_capacity = registry->actor_capacity;
        registry->actors_heap = reallocate(registry->actors_heap, next_size);
        registry->actors = (Actor *)registry->actors_heap.pointer;

        if (next_capacity > old_capacity) {
            memset(registry->actors + old_capacity, 0, (next_capacity - old_capacity) * sizeof(Actor));
        }
    }

    registry->actor_capacity = next_capacity;
    return Ok;
}

void actor_registry_init(ActorRegistry *registry) {
    if (!registry)
        return;

    registry->actors_heap = NullHeap;
    registry->actors = Null;
    registry->actor_count = 0;
    registry->actor_capacity = 0;
}

void actor_registry_dispose(ActorRegistry *registry) {
    if (!registry)
        return;

    for (usize actor_index = 0; actor_index < registry->actor_count; ++actor_index)
        actor_dispose(&registry->actors[actor_index]);

    if (registry->actors_heap.pointer)
        deallocate(registry->actors_heap);

    registry->actors_heap = NullHeap;
    registry->actors = Null;
    registry->actor_count = 0;
    registry->actor_capacity = 0;
}

Actor *actor_registry_create_actor(ActorRegistry *registry, char *id, boolean enabled, int layer) {
    if (!registry || !id)
        return Null;

    if (ensure_registry_actor_capacity(registry, registry->actor_count + 1) != Ok)
        return Null;

    Actor *actor = &registry->actors[registry->actor_count++];
    actor_init(actor, id, enabled, layer);

    return actor;
}

result actor_registry_load_actors(ActorRegistry *registry, const ActorLoadSpec *specs, usize spec_count) {
    if (!registry || (!specs && spec_count > 0))
        return Err;

    for (usize spec_index = 0; spec_index < spec_count; ++spec_index) {
        const ActorLoadSpec *spec = &specs[spec_index];
        Actor *actor = actor_registry_create_actor(registry, spec->id, spec->enabled, spec->layer);

        if (!actor) {
            log_err("Failed to allocate actor '%s'", spec->id ? spec->id : "<unknown>");
            return Err;
        }

        for (usize component_index = 0; component_index < spec->component_count; ++component_index) {
            const ActorLoadComponent *load_component = &spec->components[component_index];
            if (actor_add_component(actor, load_component->descriptor, load_component->data) != Ok) {
                log_err("Failed to add component '%s' to actor '%s'", load_component->descriptor.name ? load_component->descriptor.name : "<unknown>", actor->id);
                return Err;
            }
        }

        if (actor_initialize_components(actor) != Ok)
            return Err;
    }

    return Ok;
}

const char *component_kind_name(ComponentKind kind) {
    switch (kind) {
        case ComponentBuiltin:
            return "builtin";
        case ComponentScript:
            return "script";
        case ComponentCustom:
            return "custom";
        default:
            return "unknown";
    }
}
