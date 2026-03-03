#include "testing.h"

#include "actor/actor.h"

#include "common.h"

typedef struct {
    usize initialized_count;
    usize builtin_count;
    usize script_count;
} ActorInitState;

static result test_initialize_component(Actor *actor, ActorComponent *component, void *context) {
    (void)actor;

    ActorInitState *state = (ActorInitState *)context;
    if (!state)
        return Err;

    ++state->initialized_count;

    if (component->descriptor.kind == ComponentBuiltin)
        ++state->builtin_count;
    else if (component->descriptor.kind == ComponentScript)
        ++state->script_count;

    return Ok;
}

void run_actor_tests(void) {
#ifdef Testing
    usize failed = 0;

    log_msg("Running actor tests...");

    ActorRegistry registry;
    actor_registry_init(&registry);

    ActorInitState state = {0};

    const ActorLoadComponent actor_components[] = {
        {
            .descriptor = {
                .name = "Transform",
                .kind = ComponentBuiltin,
                .initialize = test_initialize_component,
                .context = &state,
            },
            .data = Null,
        },
        {
            .descriptor = {
                .name = "RigidBody",
                .kind = ComponentBuiltin,
                .initialize = test_initialize_component,
                .context = &state,
            },
            .data = Null,
        },
        {
            .descriptor = {
                .name = "scripts/player.lua",
                .kind = ComponentScript,
                .initialize = test_initialize_component,
                .context = &state,
            },
            .data = Null,
        },
    };

    const ActorLoadSpec specs[] = {
        {
            .id = "player",
            .enabled = True,
            .layer = 4,
            .components = actor_components,
            .component_count = sizeof(actor_components) / sizeof(actor_components[0]),
        },
    };

    if (actor_registry_load_actors(&registry, specs, sizeof(specs) / sizeof(specs[0])) != Ok) {
        log_err("actor_registry_load_actors should return Ok for valid actor spec");
        ++failed;
    }

    if (registry.actor_count != 1) {
        log_err("actor registry should contain exactly one actor");
        ++failed;
    }

    if (registry.actors[0].component_count != 3) {
        log_err("loaded actor should preserve all components");
        ++failed;
    }

    if (registry.actors[0].layer != 4) {
        log_err("loaded actor should preserve layer");
        ++failed;
    }

    if (state.initialized_count != 3 || state.builtin_count != 2 || state.script_count != 1) {
        log_err("all component initialize callbacks should run during actor load (builtin=2, script=1)");
        ++failed;
    }

    actor_registry_dispose(&registry);

    record_test_result("Actor tests", failed);
#endif // Testing
}
