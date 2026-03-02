#include "actor.h"

#include <stdio.h>
#include <string.h>

void actor_transform_reset(Actor *actor) {
    if (!actor)
        return;

    actor->transform.position = (ActorVector3){0.0f, 0.0f, 0.0f};
    actor->transform.rotation_euler = (ActorVector3){0.0f, 0.0f, 0.0f};
    actor->transform.scale = (ActorVector3){1.0f, 1.0f, 1.0f};
}

void actor_capture_previous_transform(Actor *actor) {
    if (!actor)
        return;

    actor->previous_transform = actor->transform;
}

result actor_set_parent(Actor *actor, const char *parent_id) {
    if (!actor)
        return Err;

    actor->has_parent = False;
    actor->parent_id[0] = '\0';

    if (!parent_id || parent_id[0] == '\0')
        return Ok;

    if (snprintf(actor->parent_id, sizeof(actor->parent_id), "%s", parent_id) >= (int)sizeof(actor->parent_id))
        return Err;

    actor->has_parent = True;
    return Ok;
}

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

    actor->id_heap = NullHeap;
    actor->id = Null;
    if (id) {
        const usize id_length = strlen(id) + 1;
        actor->id_heap = allocate(id_length, sizeof(char));
        actor->id = (char *)actor->id_heap.pointer;
        if (!actor->id) {
            log_err("Failed to allocate actor id for '%s'", id);
        } else {
            memcpy(actor->id, id, id_length);
        }
    }

    actor->enabled = enabled;
    actor->destroy_on_load = True;
    actor->pending_destroy = False;
    actor->layer = layer;
    actor->tag_count = 0;
    memset(actor->tags, 0, sizeof(actor->tags));
    actor->has_parent = False;
    actor->parent_id[0] = '\0';
    actor_transform_reset(actor);
    actor_capture_previous_transform(actor);
    actor->components_heap = NullHeap;
    actor->components = Null;
    actor->component_count = 0;
    actor->component_capacity = 0;
}

void actor_dispose(Actor *actor) {
    if (!actor)
        return;

    if (actor->id_heap.pointer)
        deallocate(actor->id_heap);

    if (actor->components_heap.pointer)
        deallocate(actor->components_heap);

    actor->id_heap = NullHeap;
    actor->id = Null;
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
    if (!actor->id) {
        actor_dispose(actor);
        --registry->actor_count;
        return Null;
    }

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

boolean actor_has_tag(const Actor *actor, const char *tag) {
    if (!actor || !tag || tag[0] == '\0')
        return False;

    for (usize tag_index = 0; tag_index < actor->tag_count; ++tag_index) {
        if (strcmp(actor->tags[tag_index], tag) == 0)
            return True;
    }

    return False;
}
