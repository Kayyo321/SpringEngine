#include "program.h"
#include "testing/testing.h"

#include "actor/actor.h"
#include "windowman/windowman.h"

void test_update(void) {
    DrawText("Running tests...", 10, 10, 20, BLACK);
}

static result initialize_component(Actor *actor, ActorComponent *component, void *context) {
    (void)context;

    log_msg("Initialized %s component '%s' on actor '%s'", component_kind_name(component->descriptor.kind), component->descriptor.name, actor->id);
    return Ok;
}

static result load_bootstrap_scene(ActorRegistry *registry) {
    const ActorLoadComponent player_components[] = {
        {
            .descriptor = {
                .name = "Transform",
                .kind = ComponentBuiltin,
                .initialize = initialize_component,
                .context = 0,
            },
            .data = 0,
        },
        {
            .descriptor = {
                .name = "MeshRenderer",
                .kind = ComponentBuiltin,
                .initialize = initialize_component,
                .context = 0,
            },
            .data = 0,
        },
        {
            .descriptor = {
                .name = "scripts/player_controller.lua",
                .kind = ComponentScript,
                .initialize = initialize_component,
                .context = 0,
            },
            .data = 0,
        },
    };

    const ActorLoadComponent camera_components[] = {
        {
            .descriptor = {
                .name = "Transform",
                .kind = ComponentBuiltin,
                .initialize = initialize_component,
                .context = 0,
            },
            .data = 0,
        },
        {
            .descriptor = {
                .name = "Camera",
                .kind = ComponentBuiltin,
                .initialize = initialize_component,
                .context = 0,
            },
            .data = 0,
        },
    };

    const ActorLoadSpec scene_specs[] = {
        {
            .id = "player",
            .enabled = True,
            .components = player_components,
            .component_count = sizeof(player_components) / sizeof(player_components[0]),
        },
        {
            .id = "main_camera",
            .enabled = True,
            .components = camera_components,
            .component_count = sizeof(camera_components) / sizeof(camera_components[0]),
        },
    };

    return actor_registry_load_actors(registry, scene_specs, sizeof(scene_specs) / sizeof(scene_specs[0]));
}

void run_program(void) {
    ActorRegistry registry;
    actor_registry_init(&registry);

    if (load_bootstrap_scene(&registry) != Ok) {
        actor_registry_dispose(&registry);
        log_err("Failed to load bootstrap actors.");
        return;
    }

    open_window(DefaultWindowConfig);

    do {
        //
    } while (!update_window(test_update));

    close_window();
    actor_registry_dispose(&registry);
}
