#ifndef ACTOR_H
#define ACTOR_H

#include "common.h"

typedef struct Actor Actor;
typedef struct ActorComponent ActorComponent;

enum {
    ActorMaxTags = 16,
    ActorMaxTagLength = 64,
    ActorMaxParentIdLength = 128,
};

typedef struct {
    float x;
    float y;
    float z;
} ActorVector3;

typedef struct {
    ActorVector3 position;
    ActorVector3 rotation_euler;
    ActorVector3 scale;
} ActorTransform;

typedef enum {
    ComponentBuiltin,
    ComponentScript,
    ComponentCustom,
} ComponentKind;

typedef result (*ComponentInitializeFn)(Actor *actor, ActorComponent *component, void *context);

typedef struct {
    char *name;
    ComponentKind kind;
    ComponentInitializeFn initialize;
    void *context;
} ComponentDescriptor;

struct ActorComponent {
    ComponentDescriptor descriptor;
    void *data;
};

struct Actor {
    Heap id_heap;
    char *id;
    boolean enabled;
    boolean destroy_on_load;
    boolean pending_destroy;
    int layer;
    char tags[ActorMaxTags][ActorMaxTagLength];
    usize tag_count;
    boolean has_parent;
    char parent_id[ActorMaxParentIdLength];
    ActorTransform transform;
    ActorTransform previous_transform;

    Heap components_heap;
    ActorComponent *components;
    usize component_count;
    usize component_capacity;
};

typedef struct {
    ComponentDescriptor descriptor;
    void *data;
} ActorLoadComponent;

typedef struct {
    char *id;
    boolean enabled;
    int layer;
    const ActorLoadComponent *components;
    usize component_count;
} ActorLoadSpec;

typedef struct {
    Heap actors_heap;
    Actor *actors;
    usize actor_count;
    usize actor_capacity;
} ActorRegistry;

void actor_init(Actor *actor, char *id, boolean enabled, int layer);
void actor_dispose(Actor *actor);

result actor_add_component(Actor *actor, ComponentDescriptor descriptor, void *data);
result actor_initialize_components(Actor *actor);
void actor_transform_reset(Actor *actor);
void actor_capture_previous_transform(Actor *actor);
result actor_set_parent(Actor *actor, const char *parent_id);

void actor_registry_init(ActorRegistry *registry);
void actor_registry_dispose(ActorRegistry *registry);
Actor *actor_registry_create_actor(ActorRegistry *registry, char *id, boolean enabled, int layer);
result actor_registry_load_actors(ActorRegistry *registry, const ActorLoadSpec *specs, usize spec_count);

const char *component_kind_name(ComponentKind kind);
boolean actor_has_tag(const Actor *actor, const char *tag);

#endif // ACTOR_H
