#include "runtime_loader.h"

#include "actor/actor.h"
#include "actor/camera_component.h"
#include "config/static_sprite_component.h"
#include "dj/dj.h"
#include "project_config.h"
#include "script/script_runtime.h"
#include "ui/ui_runtime.h"
#include "windowman/windowman.h"

#include "tomlc17.h"

#include <dirent.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

enum {
    RuntimeMaxAutoloadActors = 128,
    RuntimeMaxAutoloadActorIdLength = 128,
    RuntimeMaxPendingPrefabInstantiations = 256,
    RuntimeMaxRuntimeActorIdLength = 128,
};

typedef struct {
    char actor_id[RuntimeMaxRuntimeActorIdLength];
    char prefab_ref_id[RuntimeMaxRuntimeActorIdLength];
    boolean has_position;
    ActorTransform transform;
} PendingPrefabInstantiation;

typedef struct {
    ActorRegistry actor_registry;
    DJ dj;
    ScriptRuntime script_runtime;
    CameraComponentData *active_camera;
    Actor *active_camera_actor;
    boolean dj_enabled;
    boolean active;
    boolean has_pending_scene_load;
    usize autoload_actor_count;
    usize pending_prefab_instantiation_count;
    usize prefab_copy_counter;
    char project_root[PATH_MAX];
    char scenes_root[PATH_MAX];
    char prefabs_root[PATH_MAX];
    char ui_root[PATH_MAX];
    char autoload_data_path[PATH_MAX];
    char current_scene_path[PATH_MAX];
    char pending_scene_path[PATH_MAX];
    char autoload_actor_ids[RuntimeMaxAutoloadActors][RuntimeMaxAutoloadActorIdLength];
    PendingPrefabInstantiation pending_prefab_instantiations[RuntimeMaxPendingPrefabInstantiations];
    UiRuntime *ui_runtime;
} RuntimeState;

static RuntimeState runtime_state;

static result join_path(const char *base, const char *path, char *out_path, usize out_size);
static result parent_directory(const char *path, char *out_dir, usize out_size);
static result parse_toml_file(const char *path, toml_result_t *out_parsed);
static result load_autoload_actors(toml_datum_t autoload_toptab);
static result load_scene_actors(toml_datum_t scene_toptab, toml_datum_t data_toptab, const char *prefabs_root);
static result instantiate_actor_from_table(const char *actor_id, toml_datum_t actor_table, toml_datum_t prefab_refs, const char *prefabs_root, const char *success_log_label);
static Actor *find_actor_by_id(const char *actor_id);
static void refresh_component_actor_backrefs(void);
static void dispose_actor_components(Actor *actor);
static void destroy_actor_at_index(usize actor_index);
static void process_pending_actor_destroys(void);
static result process_pending_prefab_instantiations(void);
static result enqueue_prefab_instantiation(const char *prefab_ref_id, boolean has_position, float x, float y, float z, char *out_actor_id, usize out_actor_id_size);
static result resolve_prefab_path_from_ref(const char *prefab_ref_id, char *out_prefab_path, usize out_prefab_path_size);
static result load_scene_runtime(const char *scene_path);
static void process_pending_scene_load(void);
static const toml_datum_t *find_animated_sprite_component_table(toml_datum_t actor_table, toml_datum_t *out_animated_sprite_table);
static result animated_sprite_component_initialize(Actor *actor, ActorComponent *component, void *context);
static void animated_sprite_component_update(AnimatedSpriteState *state, float delta_time);
static void animated_sprite_component_draw(AnimatedSpriteState *state, CameraComponentData *active_camera, Actor *active_camera_actor);
static void animated_sprite_component_dispose(AnimatedSpriteState *state);
static void static_sprite_component_dispose(StaticSpriteState *state);
static void camera_component_dispose(CameraComponentData *state);

static boolean toml_number_to_float(toml_datum_t value, float *out_number) {
    if (!out_number)
        return False;

    if (value.type == TOML_FP64) {
        *out_number = (float)value.u.fp64;
        return True;
    }

    if (value.type == TOML_INT64) {
        *out_number = (float)value.u.int64;
        return True;
    }

    return False;
}

static Actor *find_actor_by_id(const char *actor_id) {
    if (!actor_id || actor_id[0] == '\0')
        return Null;

    for (usize index = 0; index < runtime_state.actor_registry.actor_count; ++index) {
        Actor *actor = &runtime_state.actor_registry.actors[index];
        if (!actor->id)
            continue;

        if (strcmp(actor->id, actor_id) == 0)
            return actor;
    }

    return Null;
}

static void refresh_component_actor_backrefs(void) {
    for (usize actor_index = 0; actor_index < runtime_state.actor_registry.actor_count; ++actor_index) {
        Actor *actor = &runtime_state.actor_registry.actors[actor_index];
        for (usize component_index = 0; component_index < actor->component_count; ++component_index) {
            ActorComponent *component = &actor->components[component_index];
            if (component->descriptor.kind != ComponentBuiltin || !component->descriptor.name || !component->data)
                continue;

            if (strcmp(component->descriptor.name, "AnimatedSprite") == 0) {
                AnimatedSpriteState *state = (AnimatedSpriteState *)component->data;
                state->actor = actor;
                continue;
            }

            if (strcmp(component->descriptor.name, "StaticSprite") == 0) {
                StaticSpriteState *state = (StaticSpriteState *)component->data;
                state->actor = actor;
            }
        }
    }
}

static void dispose_actor_components(Actor *actor) {
    if (!actor)
        return;

    for (usize component_index = 0; component_index < actor->component_count; ++component_index) {
        ActorComponent *component = &actor->components[component_index];
        if (component->descriptor.kind == ComponentScript)
            script_component_destroy(actor, component, &runtime_state.script_runtime);

        if (component->descriptor.kind == ComponentBuiltin && component->descriptor.name && strcmp(component->descriptor.name, "StaticSprite") == 0)
            static_sprite_component_dispose((StaticSpriteState *)component->data);

        if (component->descriptor.kind == ComponentBuiltin && component->descriptor.name && strcmp(component->descriptor.name, "AnimatedSprite") == 0)
            animated_sprite_component_dispose((AnimatedSpriteState *)component->data);

        if (component->descriptor.kind == ComponentBuiltin && component->descriptor.name && strcmp(component->descriptor.name, "Camera") == 0)
            camera_component_dispose((CameraComponentData *)component->data);
    }
}

static void destroy_actor_at_index(usize actor_index) {
    if (actor_index >= runtime_state.actor_registry.actor_count)
        return;

    Actor *actor = &runtime_state.actor_registry.actors[actor_index];
    dispose_actor_components(actor);
    actor_dispose(actor);

    const usize last_index = runtime_state.actor_registry.actor_count - 1;
    if (actor_index < last_index)
        memmove(&runtime_state.actor_registry.actors[actor_index], &runtime_state.actor_registry.actors[actor_index + 1], (last_index - actor_index) * sizeof(Actor));

    runtime_state.actor_registry.actor_count--;
    refresh_component_actor_backrefs();
}

static void process_pending_actor_destroys(void) {
    usize actor_index = 0;
    while (actor_index < runtime_state.actor_registry.actor_count) {
        Actor *actor = &runtime_state.actor_registry.actors[actor_index];
        if (!actor->pending_destroy) {
            actor_index++;
            continue;
        }

        destroy_actor_at_index(actor_index);
    }
}

static result resolve_prefab_path_from_ref(const char *prefab_ref_id, char *out_prefab_path, usize out_prefab_path_size) {
    if (!prefab_ref_id || prefab_ref_id[0] == '\0' || !out_prefab_path || out_prefab_path_size == 0)
        return Err;

    const char *resolved_ref_path = prefab_ref_id;

    if (runtime_state.current_scene_path[0] != '\0') {
        toml_result_t scene_toml = {0};
        if (parse_toml_file(runtime_state.current_scene_path, &scene_toml) == Ok) {
            toml_datum_t scene_table = toml_get(scene_toml.toptab, "Scene");
            toml_datum_t scene_data_file = toml_get(scene_table, "data_file");
            if (scene_data_file.type == TOML_STRING && scene_data_file.u.s && scene_data_file.u.s[0] != '\0') {
                char scene_directory[PATH_MAX] = {0};
                if (parent_directory(runtime_state.current_scene_path, scene_directory, sizeof(scene_directory)) == Ok) {
                    char scene_data_path[PATH_MAX] = {0};
                    if (join_path(scene_directory, scene_data_file.u.s, scene_data_path, sizeof(scene_data_path)) == Ok) {
                        toml_result_t scene_data_toml = {0};
                        if (parse_toml_file(scene_data_path, &scene_data_toml) == Ok) {
                            toml_datum_t prefab_refs = toml_get(scene_data_toml.toptab, "PrefabRefs");
                            if (prefab_refs.type == TOML_TABLE) {
                                toml_datum_t mapped = toml_get(prefab_refs, prefab_ref_id);
                                if (mapped.type == TOML_STRING && mapped.u.s && mapped.u.s[0] != '\0')
                                    resolved_ref_path = mapped.u.s;
                            }
                            toml_free(scene_data_toml);
                        }
                    }
                }
            }
            toml_free(scene_toml);
        }
    }

    if (join_path(runtime_state.prefabs_root, resolved_ref_path, out_prefab_path, out_prefab_path_size) == Ok && access(out_prefab_path, F_OK) == 0)
        return Ok;

    if (join_path(runtime_state.project_root, resolved_ref_path, out_prefab_path, out_prefab_path_size) != Ok)
        return Err;

    return access(out_prefab_path, F_OK) == 0 ? Ok : Err;
}

static result enqueue_prefab_instantiation(const char *prefab_ref_id, boolean has_position, float x, float y, float z, char *out_actor_id, usize out_actor_id_size) {
    if (!prefab_ref_id || prefab_ref_id[0] == '\0')
        return Err;

    if (runtime_state.pending_prefab_instantiation_count >= RuntimeMaxPendingPrefabInstantiations)
        return Err;

    runtime_state.prefab_copy_counter++;
    char generated_id[RuntimeMaxRuntimeActorIdLength] = {0};
    if (snprintf(generated_id, sizeof(generated_id), "prefab(copy%zu)", runtime_state.prefab_copy_counter) >= (int)sizeof(generated_id))
        return Err;

    if (out_actor_id && out_actor_id_size > 0) {
        if (snprintf(out_actor_id, out_actor_id_size, "%s", generated_id) >= (int)out_actor_id_size)
            return Err;
    }

    PendingPrefabInstantiation *pending = &runtime_state.pending_prefab_instantiations[runtime_state.pending_prefab_instantiation_count++];
    memset(pending, 0, sizeof(*pending));

    if (snprintf(pending->actor_id, sizeof(pending->actor_id), "%s", generated_id) >= (int)sizeof(pending->actor_id))
        return Err;
    if (snprintf(pending->prefab_ref_id, sizeof(pending->prefab_ref_id), "%s", prefab_ref_id) >= (int)sizeof(pending->prefab_ref_id))
        return Err;

    pending->has_position = has_position;
    pending->transform.position = (ActorVector3){x, y, z};
    pending->transform.rotation_euler = (ActorVector3){0.0f, 0.0f, 0.0f};
    pending->transform.scale = (ActorVector3){1.0f, 1.0f, 1.0f};
    return Ok;
}

static result process_pending_prefab_instantiations(void) {
    if (runtime_state.pending_prefab_instantiation_count == 0)
        return Ok;

    for (usize index = 0; index < runtime_state.pending_prefab_instantiation_count; ++index) {
        PendingPrefabInstantiation *pending = &runtime_state.pending_prefab_instantiations[index];
        char prefab_path[PATH_MAX] = {0};
        if (resolve_prefab_path_from_ref(pending->prefab_ref_id, prefab_path, sizeof(prefab_path)) != Ok) {
            log_err("Failed to resolve prefab ref '%s' for actor '%s'", pending->prefab_ref_id, pending->actor_id);
            continue;
        }

        toml_result_t prefab_toml = {0};
        if (parse_toml_file(prefab_path, &prefab_toml) != Ok)
            continue;

        toml_datum_t prefab_table = toml_get(prefab_toml.toptab, "Prefab");
        if (prefab_table.type != TOML_TABLE) {
            log_err("Prefab config '%s' is missing [Prefab] table", prefab_path);
            toml_free(prefab_toml);
            continue;
        }

        if (instantiate_actor_from_table(pending->actor_id, prefab_table, (toml_datum_t){0}, Null, "Instantiated prefab") == Ok && pending->has_position) {
            Actor *spawned = find_actor_by_id(pending->actor_id);
            if (spawned)
                spawned->transform = pending->transform;
        }

        toml_free(prefab_toml);
    }

    runtime_state.pending_prefab_instantiation_count = 0;
    return Ok;
}

static result read_xy_array(toml_datum_t table, const char *key, Vector2 *out_position) {
    if (table.type != TOML_TABLE || !key || !out_position)
        return Err;

    toml_datum_t value = toml_get(table, key);
    if (value.type != TOML_ARRAY || value.u.arr.size < 2)
        return Err;

    float x = 0.0f;
    float y = 0.0f;
    if (!toml_number_to_float(value.u.arr.elem[0], &x) || !toml_number_to_float(value.u.arr.elem[1], &y))
        return Err;

    out_position->x = x;
    out_position->y = y;
    return Ok;
}

static result read_xyz_array(toml_datum_t table, const char *key, ActorVector3 *out_vector) {
    if (table.type != TOML_TABLE || !key || !out_vector)
        return Err;

    toml_datum_t value = toml_get(table, key);
    if (value.type != TOML_ARRAY || value.u.arr.size < 3)
        return Err;

    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    if (!toml_number_to_float(value.u.arr.elem[0], &x) || !toml_number_to_float(value.u.arr.elem[1], &y) || !toml_number_to_float(value.u.arr.elem[2], &z))
        return Err;

    out_vector->x = x;
    out_vector->y = y;
    out_vector->z = z;
    return Ok;
}

static result read_actor_transform(toml_datum_t actor_table, ActorTransform *out_transform) {
    if (!out_transform || actor_table.type != TOML_TABLE)
        return Err;

    toml_datum_t transform = toml_get(actor_table, "Transform");
    if (transform.type != TOML_TABLE)
        return Ok;

    toml_datum_t position = toml_get(transform, "position");
    if (position.type != TOML_UNKNOWN && read_xyz_array(transform, "position", &out_transform->position) != Ok)
        return Err;

    toml_datum_t rotation = toml_get(transform, "rotation_euler");
    if (rotation.type != TOML_UNKNOWN && read_xyz_array(transform, "rotation_euler", &out_transform->rotation_euler) != Ok)
        return Err;

    toml_datum_t scale = toml_get(transform, "scale");
    if (scale.type != TOML_UNKNOWN && read_xyz_array(transform, "scale", &out_transform->scale) != Ok)
        return Err;

    return Ok;
}

static result read_transform_anchor(toml_datum_t transform_table, SpriteAnchor *out_anchor) {
    if (!out_anchor)
        return Err;

    out_anchor->center = False;
    out_anchor->offset = (Vector2){0.0f, 0.0f};

    if (transform_table.type != TOML_TABLE)
        return Ok;

    toml_datum_t anchor = toml_get(transform_table, "anchor");
    if (anchor.type == TOML_ARRAY) {
        if (anchor.u.arr.size < 2)
            return Err;

        float x = 0.0f;
        float y = 0.0f;
        if (!toml_number_to_float(anchor.u.arr.elem[0], &x) || !toml_number_to_float(anchor.u.arr.elem[1], &y))
            return Err;

        out_anchor->offset = (Vector2){x, y};
        return Ok;
    }

    if (anchor.type == TOML_STRING) {
        if (anchor.u.s && strcmp(anchor.u.s, "center") == 0) {
            out_anchor->center = True;
            return Ok;
        }

        return Err;
    }

    if (anchor.type != TOML_UNKNOWN)
        return Err;

    return Ok;
}

static boolean has_transform_anchor(toml_datum_t transform_table) {
    if (transform_table.type != TOML_TABLE)
        return False;

    toml_datum_t anchor = toml_get(transform_table, "anchor");
    return anchor.type != TOML_UNKNOWN;
}

static result read_prefab_transform_anchor(toml_datum_t actor_table, toml_datum_t prefab_refs, const char *prefabs_root, SpriteAnchor *out_anchor) {
    if (!out_anchor)
        return Err;

    if (actor_table.type != TOML_TABLE || prefab_refs.type != TOML_TABLE || !prefabs_root)
        return Ok;

    toml_datum_t prefab = toml_get(actor_table, "prefab");
    if (prefab.type != TOML_STRING || !prefab.u.s || prefab.u.s[0] == '\0')
        return Ok;

    toml_datum_t prefab_path_ref = toml_get(prefab_refs, prefab.u.s);
    if (prefab_path_ref.type != TOML_STRING || !prefab_path_ref.u.s || prefab_path_ref.u.s[0] == '\0')
        return Ok;

    char prefab_path[PATH_MAX] = {0};
    char prefab_path_from_project_root[PATH_MAX] = {0};

    if (join_path(prefabs_root, prefab_path_ref.u.s, prefab_path, sizeof(prefab_path)) != Ok)
        return Err;

    if (access(prefab_path, F_OK) != 0) {
        if (!runtime_state.project_root[0])
            return Err;

        if (join_path(runtime_state.project_root, prefab_path_ref.u.s, prefab_path_from_project_root, sizeof(prefab_path_from_project_root)) != Ok)
            return Err;

        if (access(prefab_path_from_project_root, F_OK) != 0)
            return Err;

        if (snprintf(prefab_path, sizeof(prefab_path), "%s", prefab_path_from_project_root) >= (int)sizeof(prefab_path))
            return Err;
    }

    toml_result_t prefab_toml = {0};
    if (parse_toml_file(prefab_path, &prefab_toml) != Ok)
        return Err;

    toml_datum_t prefab_table = toml_get(prefab_toml.toptab, "Prefab");
    toml_datum_t prefab_transform = toml_get(prefab_table, "Transform");

    result read_result = Ok;
    if (has_transform_anchor(prefab_transform))
        read_result = read_transform_anchor(prefab_transform, out_anchor);

    toml_free(prefab_toml);
    return read_result;
}

static result read_actor_transform_anchor(toml_datum_t actor_table, toml_datum_t prefab_refs, const char *prefabs_root, SpriteAnchor *out_anchor) {
    if (!out_anchor)
        return Err;

    out_anchor->center = False;
    out_anchor->offset = (Vector2){0.0f, 0.0f};

    toml_datum_t transform = toml_get(actor_table, "Transform");
    if (has_transform_anchor(transform))
        return read_transform_anchor(transform, out_anchor);

    return read_prefab_transform_anchor(actor_table, prefab_refs, prefabs_root, out_anchor);
}

static const char *find_static_sprite_texture(toml_datum_t actor_table, toml_datum_t *out_static_sprite_table) {
    if (out_static_sprite_table)
        *out_static_sprite_table = (toml_datum_t){0};

    if (actor_table.type != TOML_TABLE)
        return Null;

    toml_datum_t overrides = toml_get(actor_table, "Overrides");
    if (overrides.type == TOML_TABLE) {
        toml_datum_t components = toml_get(overrides, "Components");
        if (components.type == TOML_TABLE) {
            toml_datum_t static_sprite = toml_get(components, "StaticSprite");
            if (static_sprite.type == TOML_TABLE) {
                toml_datum_t texture = toml_get(static_sprite, "texture");
                if (texture.type == TOML_STRING && texture.u.s && texture.u.s[0] != '\0') {
                    if (out_static_sprite_table)
                        *out_static_sprite_table = static_sprite;
                    return texture.u.s;
                }
            }
        }
    }

    toml_datum_t components = toml_get(actor_table, "Components");
    if (components.type == TOML_TABLE) {
        toml_datum_t static_sprite = toml_get(components, "StaticSprite");
        if (static_sprite.type == TOML_TABLE) {
            toml_datum_t texture = toml_get(static_sprite, "texture");
            if (texture.type == TOML_STRING && texture.u.s && texture.u.s[0] != '\0') {
                if (out_static_sprite_table)
                    *out_static_sprite_table = static_sprite;
                return texture.u.s;
            }
        }
    }

    return Null;
}

static const toml_datum_t *find_animated_sprite_component_table(toml_datum_t actor_table, toml_datum_t *out_animated_sprite_table) {
    if (out_animated_sprite_table)
        *out_animated_sprite_table = (toml_datum_t){0};

    if (actor_table.type != TOML_TABLE || !out_animated_sprite_table)
        return Null;

    toml_datum_t overrides = toml_get(actor_table, "Overrides");
    if (overrides.type == TOML_TABLE) {
        toml_datum_t components = toml_get(overrides, "Components");
        if (components.type == TOML_TABLE) {
            toml_datum_t animated_sprite = toml_get(components, "AnimatedSprite");
            if (animated_sprite.type == TOML_TABLE) {
                *out_animated_sprite_table = animated_sprite;
                return out_animated_sprite_table;
            }
        }
    }

    toml_datum_t components = toml_get(actor_table, "Components");
    if (components.type == TOML_TABLE) {
        toml_datum_t animated_sprite = toml_get(components, "AnimatedSprite");
        if (animated_sprite.type == TOML_TABLE) {
            *out_animated_sprite_table = animated_sprite;
            return out_animated_sprite_table;
        }
    }

    return Null;
}

static result static_sprite_component_initialize(Actor *actor, ActorComponent *component, void *context) {
    (void)actor;
    (void)context;

    if (!component || !component->data)
        return Err;

    StaticSpriteState *state = (StaticSpriteState *)component->data;
    if (!state->texture_path[0])
        return Err;

    if (join_path(runtime_state.project_root, state->texture_path, state->resolved_texture_path, sizeof(state->resolved_texture_path)) != Ok) {
        log_err("Failed to resolve static sprite texture path '%s'", state->texture_path);
        return Err;
    }

    state->attempted_load = False;
    state->loaded = False;
    return Ok;
}

static result camera_component_initialize(Actor *actor, ActorComponent *component, void *context) {
    (void)context;

    if (!actor || !component || !component->data)
        return Err;

    CameraComponentData *state = (CameraComponentData *)component->data;
    state->camera.position = (Vector3){
        actor->transform.position.x,
        actor->transform.position.y,
        actor->transform.position.z,
    };

    return Ok;
}

static void camera_component_dispose(CameraComponentData *state) {
    if (!state)
        return;

    if (state->heap.pointer)
        deallocate(state->heap);
}

static void static_sprite_component_draw(StaticSpriteState *state, CameraComponentData *active_camera, Actor *active_camera_actor) {
    if (!state)
        return;

    if (!state->loaded) {
        if (state->attempted_load)
            return;

        state->attempted_load = True;
        state->texture = LoadTexture(state->resolved_texture_path);
        if (state->texture.id == 0) {
            log_err("Failed to load static sprite texture '%s'", state->resolved_texture_path);
            return;
        }

        state->loaded = True;
    }

    Rectangle source = {
        0.0f,
        0.0f,
        (float)state->texture.width,
        (float)state->texture.height,
    };

    float actor_x = 0.0f;
    float actor_y = 0.0f;
    float actor_scale_x = 1.0f;
    float actor_scale_y = 1.0f;
    if (state->actor) {
        actor_x = state->actor->transform.position.x;
        actor_y = state->actor->transform.position.y;
        actor_scale_x = state->actor->transform.scale.x;
        actor_scale_y = state->actor->transform.scale.y;
    }

    float destination_x = actor_x + state->position.x;
    float destination_y = actor_y + state->position.y;
    float destination_width = (float)state->texture.width * state->scale * actor_scale_x;
    float destination_height = (float)state->texture.height * state->scale * actor_scale_y;

    if (active_camera && active_camera_actor) {
        float zoom = 1.0f;
        if (active_camera->camera.fovy > 0.001f)
            zoom = 60.0f / active_camera->camera.fovy;

        const float camera_x = active_camera_actor->transform.position.x;
        const float camera_y = active_camera_actor->transform.position.y;

        destination_x = (destination_x - camera_x) * zoom + ((float)GetScreenWidth() * 0.5f);
        destination_y = (destination_y - camera_y) * zoom + ((float)GetScreenHeight() * 0.5f);
        destination_width *= zoom;
        destination_height *= zoom;
    }

    Rectangle destination = {
        destination_x,
        destination_y,
        destination_width,
        destination_height,
    };

    Vector2 anchor = {
        state->anchor.offset.x * state->scale * actor_scale_x,
        state->anchor.offset.y * state->scale * actor_scale_y,
    };

    if (state->anchor.center) {
        anchor.x = destination.width * 0.5f;
        anchor.y = destination.height * 0.5f;
    }

    DrawTexturePro(state->texture, source, destination, anchor, state->rotation, state->tint);
}

static void static_sprite_component_dispose(StaticSpriteState *state) {
    if (!state)
        return;

    if (state->loaded) {
        UnloadTexture(state->texture);
        state->loaded = False;
    }

    state->attempted_load = False;

    if (state->heap.pointer)
        deallocate(state->heap);
}

static boolean copy_toml_key(char *destination, usize destination_size, const char *source, int source_length) {
    if (!destination || destination_size == 0 || !source || source_length < 0)
        return False;

    if ((usize)source_length + 1 > destination_size)
        return False;

    memcpy(destination, source, (usize)source_length);
    destination[source_length] = '\0';
    return True;
}

static int animated_sprite_find_sheet_index(const AnimatedSpriteState *state, const char *sheet_key) {
    if (!state || !sheet_key || sheet_key[0] == '\0')
        return -1;

    for (int index = 0; index < state->sheet_count; ++index) {
        if (strcmp(state->sheets[index].key, sheet_key) == 0)
            return index;
    }

    return -1;
}

static int animated_sprite_find_state_index(const AnimatedSpriteState *state, const char *state_name) {
    if (!state || !state_name || state_name[0] == '\0')
        return -1;

    for (int index = 0; index < state->state_count; ++index) {
        if (strcmp(state->states[index].name, state_name) == 0)
            return index;
    }

    return -1;
}

static int animated_sprite_find_bool_param_index(const AnimatedSpriteState *state, const char *param_name) {
    if (!state || !param_name || param_name[0] == '\0')
        return -1;

    for (int index = 0; index < state->bool_param_count; ++index) {
        if (strcmp(state->bool_params[index].name, param_name) == 0)
            return index;
    }

    return -1;
}

static boolean animated_sprite_get_bool_param(const AnimatedSpriteState *state, const char *param_name) {
    const int param_index = animated_sprite_find_bool_param_index(state, param_name);
    if (param_index < 0)
        return False;

    return state->bool_params[param_index].value ? True : False;
}

static int animated_sprite_find_number_param_index(const AnimatedSpriteState *state, const char *param_name) {
    if (!state || !param_name || param_name[0] == '\0')
        return -1;

    for (int index = 0; index < state->number_param_count; ++index) {
        if (strcmp(state->number_params[index].name, param_name) == 0)
            return index;
    }

    return -1;
}

static float animated_sprite_get_number_param(const AnimatedSpriteState *state, const char *param_name, boolean *has_value) {
    if (has_value)
        *has_value = False;

    const int param_index = animated_sprite_find_number_param_index(state, param_name);
    if (param_index < 0)
        return 0.0f;

    if (has_value)
        *has_value = True;

    return state->number_params[param_index].value;
}

static AnimatedTransitionCondition animated_transition_condition_from_string(const char *value) {
    if (!value || value[0] == '\0')
        return AnimatedTransitionAlways;

    if (strcmp(value, "moving") == 0)
        return AnimatedTransitionMoving;

    if (strcmp(value, "not_moving") == 0)
        return AnimatedTransitionNotMoving;

    if (strcmp(value, "param_true") == 0)
        return AnimatedTransitionParamTrue;

    if (strcmp(value, "param_false") == 0)
        return AnimatedTransitionParamFalse;

    if (strcmp(value, "param_gt") == 0)
        return AnimatedTransitionParamGreater;

    if (strcmp(value, "param_lt") == 0)
        return AnimatedTransitionParamLess;

    return AnimatedTransitionAlways;
}

static boolean animated_transition_is_triggered(const AnimatedSpriteState *state, const AnimatedSpriteTransition *transition, float movement_speed) {
    if (!transition)
        return False;

    switch (transition->condition) {
        case AnimatedTransitionAlways:
            return True;
        case AnimatedTransitionMoving:
            return movement_speed >= transition->speed_threshold;
        case AnimatedTransitionNotMoving:
            return movement_speed < transition->speed_threshold;
        case AnimatedTransitionParamTrue:
            return animated_sprite_get_bool_param(state, transition->param_name) ? True : False;
        case AnimatedTransitionParamFalse:
            return animated_sprite_get_bool_param(state, transition->param_name) ? False : True;
        case AnimatedTransitionParamGreater: {
            boolean has_value = False;
            const float param_value = animated_sprite_get_number_param(state, transition->param_name, &has_value);
            return has_value && param_value > transition->speed_threshold;
        }
        case AnimatedTransitionParamLess: {
            boolean has_value = False;
            const float param_value = animated_sprite_get_number_param(state, transition->param_name, &has_value);
            return has_value && param_value < transition->speed_threshold;
        }
        default:
            return False;
    }
}

static const AnimatedSpriteStateDef *animated_sprite_current_state(const AnimatedSpriteState *state) {
    if (!state || state->current_state_index < 0 || state->current_state_index >= state->state_count)
        return Null;

    return &state->states[state->current_state_index];
}

static const AnimatedSpriteFrame *animated_sprite_current_frame(const AnimatedSpriteState *state) {
    const AnimatedSpriteStateDef *state_definition = animated_sprite_current_state(state);
    if (!state_definition || state_definition->frame_count <= 0)
        return Null;

    if (state->current_frame_offset < 0 || state->current_frame_offset >= state_definition->frame_count)
        return Null;

    const int frame_index = state_definition->frame_start + state->current_frame_offset;
    if (frame_index < 0 || frame_index >= state->frame_count)
        return Null;

    return &state->frames[frame_index];
}

static result animated_sprite_ensure_sheet_loaded(AnimatedSpriteState *state, int sheet_index) {
    if (!state || sheet_index < 0 || sheet_index >= state->sheet_count)
        return Err;

    AnimatedSpriteSheet *sheet = &state->sheets[sheet_index];
    if (sheet->loaded)
        return Ok;

    if (sheet->attempted_load)
        return Err;

    sheet->attempted_load = True;
    sheet->texture = LoadTexture(sheet->resolved_texture_path);
    if (sheet->texture.id == 0) {
        log_err("Failed to load animated sprite sheet '%s'", sheet->resolved_texture_path);
        return Err;
    }

    sheet->loaded = True;
    return Ok;
}

static result animated_sprite_component_initialize(Actor *actor, ActorComponent *component, void *context) {
    (void)actor;
    (void)context;

    if (!component || !component->data)
        return Err;

    AnimatedSpriteState *state = (AnimatedSpriteState *)component->data;
    if (!state->anim_path[0])
        return Err;

    if (join_path(runtime_state.project_root, state->anim_path, state->resolved_anim_path, sizeof(state->resolved_anim_path)) != Ok) {
        log_err("Failed to resolve animated sprite config path '%s'", state->anim_path);
        return Err;
    }

    toml_result_t animation_toml = {0};
    if (parse_toml_file(state->resolved_anim_path, &animation_toml) != Ok)
        return Err;

    result parse_result = Err;

    toml_datum_t animation_table = toml_get(animation_toml.toptab, "Animation");
    toml_datum_t sheets_table = toml_get(animation_toml.toptab, "Sheets");
    if (animation_table.type != TOML_TABLE || sheets_table.type != TOML_TABLE) {
        log_err("Animated sprite config '%s' must include [Animation] and [Sheets]", state->resolved_anim_path);
        goto cleanup;
    }

    toml_datum_t default_state_name = toml_get(animation_table, "default");
    if (default_state_name.type != TOML_STRING || !default_state_name.u.s || default_state_name.u.s[0] == '\0') {
        log_err("Animated sprite config '%s' is missing Animation.default", state->resolved_anim_path);
        goto cleanup;
    }

    state->flip_x = False;
    state->flip_y = False;
    state->auto_flip_x_from_actor_movement = False;
    state->flip_x_param[0] = '\0';
    state->flip_x_deadzone = 0.01f;
    state->flip_x_when_param_negative = True;

    toml_datum_t flip_x = toml_get(animation_table, "flip_x");
    if (flip_x.type == TOML_BOOLEAN)
        state->flip_x = flip_x.u.boolean ? True : False;

    toml_datum_t flip_y = toml_get(animation_table, "flip_y");
    if (flip_y.type == TOML_BOOLEAN)
        state->flip_y = flip_y.u.boolean ? True : False;

    toml_datum_t flip_x_from_actor_movement = toml_get(animation_table, "flip_x_from_actor_movement");
    if (flip_x_from_actor_movement.type == TOML_BOOLEAN)
        state->auto_flip_x_from_actor_movement = flip_x_from_actor_movement.u.boolean ? True : False;

    toml_datum_t flip_x_param = toml_get(animation_table, "flip_x_param");
    if (flip_x_param.type == TOML_STRING && flip_x_param.u.s && flip_x_param.u.s[0] != '\0') {
        if (snprintf(state->flip_x_param, sizeof(state->flip_x_param), "%s", flip_x_param.u.s) >= (int)sizeof(state->flip_x_param)) {
            log_err("Animated sprite config '%s' has flip_x_param that is too long", state->resolved_anim_path);
            goto cleanup;
        }
    }

    toml_datum_t flip_x_deadzone = toml_get(animation_table, "flip_x_deadzone");
    float flip_deadzone_value = 0.0f;
    if (toml_number_to_float(flip_x_deadzone, &flip_deadzone_value) && flip_deadzone_value >= 0.0f)
        state->flip_x_deadzone = flip_deadzone_value;

    toml_datum_t flip_x_when_param_negative = toml_get(animation_table, "flip_x_when_param_negative");
    if (flip_x_when_param_negative.type == TOML_BOOLEAN)
        state->flip_x_when_param_negative = flip_x_when_param_negative.u.boolean ? True : False;

    for (int sheet_index = 0; sheet_index < sheets_table.u.tab.size; ++sheet_index) {
        if (state->sheet_count >= AnimatedSpriteMaxSheets) {
            log_err("Animated sprite config '%s' exceeded max sheet count (%d)", state->resolved_anim_path, AnimatedSpriteMaxSheets);
            goto cleanup;
        }

        toml_datum_t texture_path = sheets_table.u.tab.value[sheet_index];
        if (texture_path.type != TOML_STRING || !texture_path.u.s || texture_path.u.s[0] == '\0') {
            log_err("Animated sprite config '%s' has invalid sheet path", state->resolved_anim_path);
            goto cleanup;
        }

        AnimatedSpriteSheet *sheet = &state->sheets[state->sheet_count];
        memset(sheet, 0, sizeof(*sheet));

        if (!copy_toml_key(sheet->key, sizeof(sheet->key), sheets_table.u.tab.key[sheet_index], sheets_table.u.tab.len[sheet_index])) {
            log_err("Animated sprite config '%s' has sheet key that is too long", state->resolved_anim_path);
            goto cleanup;
        }

        if (snprintf(sheet->texture_path, sizeof(sheet->texture_path), "%s", texture_path.u.s) >= (int)sizeof(sheet->texture_path)) {
            log_err("Animated sprite config '%s' has sheet path that is too long", state->resolved_anim_path);
            goto cleanup;
        }

        if (join_path(runtime_state.project_root, sheet->texture_path, sheet->resolved_texture_path, sizeof(sheet->resolved_texture_path)) != Ok) {
            log_err("Failed to resolve animated sprite sheet path '%s'", sheet->texture_path);
            goto cleanup;
        }

        state->sheet_count++;
    }

    for (int top_level_index = 0; top_level_index < animation_toml.toptab.u.tab.size; ++top_level_index) {
        toml_datum_t value = animation_toml.toptab.u.tab.value[top_level_index];
        if (value.type != TOML_TABLE)
            continue;

        const char *key = animation_toml.toptab.u.tab.key[top_level_index];
        int key_length = animation_toml.toptab.u.tab.len[top_level_index];
        if ((key_length == 9 && strncmp(key, "Animation", 9) == 0) || (key_length == 6 && strncmp(key, "Sheets", 6) == 0))
            continue;

        if (state->state_count >= AnimatedSpriteMaxStates) {
            log_err("Animated sprite config '%s' exceeded max state count (%d)", state->resolved_anim_path, AnimatedSpriteMaxStates);
            goto cleanup;
        }

        AnimatedSpriteStateDef *state_definition = &state->states[state->state_count];
        memset(state_definition, 0, sizeof(*state_definition));

        if (!copy_toml_key(state_definition->name, sizeof(state_definition->name), key, key_length)) {
            log_err("Animated sprite config '%s' has state name that is too long", state->resolved_anim_path);
            goto cleanup;
        }

        toml_datum_t state_sheet = toml_get(value, "sheet");
        if (state_sheet.type != TOML_STRING || !state_sheet.u.s || state_sheet.u.s[0] == '\0') {
            log_err("Animated sprite state '%s' is missing a valid sheet reference", state_definition->name);
            goto cleanup;
        }

        state_definition->sheet_index = animated_sprite_find_sheet_index(state, state_sheet.u.s);
        if (state_definition->sheet_index < 0) {
            log_err("Animated sprite state '%s' references unknown sheet '%s'", state_definition->name, state_sheet.u.s);
            goto cleanup;
        }

        state_definition->fps = 8.0f;
        state_definition->loop = True;
        state_definition->frame_start = state->frame_count;
        state_definition->transition_start = state->transition_count;

        toml_datum_t fps_value = toml_get(value, "fps");
        float parsed_fps = 0.0f;
        if (toml_number_to_float(fps_value, &parsed_fps) && parsed_fps > 0.0f)
            state_definition->fps = parsed_fps;

        toml_datum_t loop_value = toml_get(value, "loop");
        if (loop_value.type == TOML_BOOLEAN)
            state_definition->loop = loop_value.u.boolean ? True : False;

        toml_datum_t frames = toml_get(value, "frames");
        if (frames.type != TOML_ARRAY || frames.u.arr.size <= 0) {
            log_err("Animated sprite state '%s' must define frames", state_definition->name);
            goto cleanup;
        }

        for (int frame_index = 0; frame_index < frames.u.arr.size; ++frame_index) {
            if (state->frame_count >= AnimatedSpriteMaxFrames) {
                log_err("Animated sprite config '%s' exceeded max frame count (%d)", state->resolved_anim_path, AnimatedSpriteMaxFrames);
                goto cleanup;
            }

            toml_datum_t frame = frames.u.arr.elem[frame_index];
            if (frame.type != TOML_ARRAY || frame.u.arr.size < 4) {
                log_err("Animated sprite state '%s' frame %d must be [x, y, w, h, optional_duration]", state_definition->name, frame_index);
                goto cleanup;
            }

            float frame_numbers[5] = {0.0f};
            for (int value_index = 0; value_index < frame.u.arr.size && value_index < 5; ++value_index) {
                if (!toml_number_to_float(frame.u.arr.elem[value_index], &frame_numbers[value_index])) {
                    log_err("Animated sprite state '%s' frame %d contains non-numeric values", state_definition->name, frame_index);
                    goto cleanup;
                }
            }

            AnimatedSpriteFrame *destination_frame = &state->frames[state->frame_count++];
            destination_frame->source = (Rectangle){
                frame_numbers[0],
                frame_numbers[1],
                frame_numbers[2],
                frame_numbers[3],
            };
            destination_frame->duration = 0.0f;

            if (frame.u.arr.size >= 5) {
                if (frame_numbers[4] <= 0.0f) {
                    log_err("Animated sprite state '%s' frame %d has invalid duration", state_definition->name, frame_index);
                    goto cleanup;
                }
                destination_frame->duration = frame_numbers[4];
            }
        }

        state_definition->frame_count = state->frame_count - state_definition->frame_start;

        toml_datum_t plugs = toml_get(value, "plug");
        if (plugs.type == TOML_TABLE) {
            for (int transition_index = 0; transition_index < plugs.u.tab.size; ++transition_index) {
                if (state->transition_count >= AnimatedSpriteMaxTransitions) {
                    log_err("Animated sprite config '%s' exceeded max transition count (%d)", state->resolved_anim_path, AnimatedSpriteMaxTransitions);
                    goto cleanup;
                }

                toml_datum_t transition_table = plugs.u.tab.value[transition_index];
                if (transition_table.type != TOML_TABLE) {
                    log_err("Animated sprite state '%s' plug transitions must be tables", state_definition->name);
                    goto cleanup;
                }

                AnimatedSpriteTransition *transition = &state->transitions[state->transition_count++];
                memset(transition, 0, sizeof(*transition));

                if (!copy_toml_key(transition->target_state_name, sizeof(transition->target_state_name), plugs.u.tab.key[transition_index], plugs.u.tab.len[transition_index])) {
                    log_err("Animated sprite transition target name is too long in state '%s'", state_definition->name);
                    goto cleanup;
                }

                transition->target_state_index = -1;
                transition->condition = AnimatedTransitionAlways;
                transition->speed_threshold = 0.01f;
                transition->param_name[0] = '\0';

                toml_datum_t condition = toml_get(transition_table, "condition");
                if (condition.type == TOML_STRING && condition.u.s && condition.u.s[0] != '\0')
                    transition->condition = animated_transition_condition_from_string(condition.u.s);

                toml_datum_t threshold = toml_get(transition_table, "speed_threshold");
                float threshold_value = 0.0f;
                if (toml_number_to_float(threshold, &threshold_value) && threshold_value >= 0.0f)
                    transition->speed_threshold = threshold_value;

                toml_datum_t param = toml_get(transition_table, "param");
                if (param.type == TOML_STRING && param.u.s && param.u.s[0] != '\0') {
                    if (snprintf(transition->param_name, sizeof(transition->param_name), "%s", param.u.s) >= (int)sizeof(transition->param_name)) {
                        log_err("Animated sprite transition param name is too long in state '%s'", state_definition->name);
                        goto cleanup;
                    }
                }

                if ((transition->condition == AnimatedTransitionParamTrue || transition->condition == AnimatedTransitionParamFalse ||
                     transition->condition == AnimatedTransitionParamGreater || transition->condition == AnimatedTransitionParamLess) &&
                    transition->param_name[0] == '\0') {
                    log_err("Animated sprite transition in state '%s' uses param condition but has no param field", state_definition->name);
                    goto cleanup;
                }
            }
        }

        state_definition->transition_count = state->transition_count - state_definition->transition_start;
        state->state_count++;
    }

    if (state->state_count <= 0) {
        log_err("Animated sprite config '%s' must define at least one state table", state->resolved_anim_path);
        goto cleanup;
    }

    for (int transition_index = 0; transition_index < state->transition_count; ++transition_index) {
        AnimatedSpriteTransition *transition = &state->transitions[transition_index];
        transition->target_state_index = animated_sprite_find_state_index(state, transition->target_state_name);
        if (transition->target_state_index < 0) {
            log_err("Animated sprite transition references unknown state '%s'", transition->target_state_name);
            goto cleanup;
        }
    }

    state->current_state_index = animated_sprite_find_state_index(state, default_state_name.u.s);
    if (state->current_state_index < 0) {
        log_err("Animated sprite default state '%s' was not found", default_state_name.u.s);
        goto cleanup;
    }

    state->current_frame_offset = 0;
    state->frame_timer = 0.0f;
    state->has_previous_actor_position = False;
    state->valid = True;

    parse_result = Ok;

cleanup:
    toml_free(animation_toml);
    return parse_result;
}

static void animated_sprite_component_update(AnimatedSpriteState *state, float delta_time) {
    if (!state || !state->valid)
        return;

    const AnimatedSpriteStateDef *state_definition = animated_sprite_current_state(state);
    if (!state_definition)
        return;

    float movement_speed = 0.0f;
    if (state->actor) {
        const ActorVector3 current_position = state->actor->transform.position;
        if (state->has_previous_actor_position && delta_time > 0.0001f) {
            const float delta_x = current_position.x - state->previous_actor_position.x;
            const float delta_y = current_position.y - state->previous_actor_position.y;
            const float delta_z = current_position.z - state->previous_actor_position.z;
            movement_speed = sqrtf((delta_x * delta_x) + (delta_y * delta_y) + (delta_z * delta_z)) / delta_time;

            if (state->auto_flip_x_from_actor_movement) {
                const float velocity_x = delta_x / delta_time;
                if (velocity_x <= -state->flip_x_deadzone)
                    state->flip_x = state->flip_x_when_param_negative;
                else if (velocity_x >= state->flip_x_deadzone)
                    state->flip_x = state->flip_x_when_param_negative ? False : True;
            }
        }

        state->previous_actor_position = current_position;
        state->has_previous_actor_position = True;
    }

    if (state->flip_x_param[0] != '\0') {
        boolean has_flip_param = False;
        const float flip_param_value = animated_sprite_get_number_param(state, state->flip_x_param, &has_flip_param);
        if (has_flip_param) {
            if (flip_param_value <= -state->flip_x_deadzone)
                state->flip_x = state->flip_x_when_param_negative;
            else if (flip_param_value >= state->flip_x_deadzone)
                state->flip_x = state->flip_x_when_param_negative ? False : True;
        }
    }

    if (state_definition->transition_count > 0) {
        for (int transition_offset = 0; transition_offset < state_definition->transition_count; ++transition_offset) {
            const int transition_index = state_definition->transition_start + transition_offset;
            if (transition_index < 0 || transition_index >= state->transition_count)
                continue;

            const AnimatedSpriteTransition *transition = &state->transitions[transition_index];
            if (!animated_transition_is_triggered(state, transition, movement_speed))
                continue;

            if (transition->target_state_index < 0 || transition->target_state_index >= state->state_count)
                continue;

            if (transition->target_state_index != state->current_state_index) {
                state->current_state_index = transition->target_state_index;
                state->current_frame_offset = 0;
                state->frame_timer = 0.0f;
            }

            state_definition = animated_sprite_current_state(state);
            if (!state_definition)
                return;

            break;
        }
    }

    if (state_definition->frame_count <= 1)
        return;

    const int frame_index = state_definition->frame_start + state->current_frame_offset;
    if (frame_index < 0 || frame_index >= state->frame_count)
        return;

    const AnimatedSpriteFrame *frame = &state->frames[frame_index];
    const float frame_duration = frame->duration > 0.0f ? frame->duration : (1.0f / state_definition->fps);

    state->frame_timer += delta_time;
    while (state->frame_timer >= frame_duration) {
        state->frame_timer -= frame_duration;
        state->current_frame_offset++;

        if (state->current_frame_offset >= state_definition->frame_count) {
            if (state_definition->loop)
                state->current_frame_offset = 0;
            else
                state->current_frame_offset = state_definition->frame_count - 1;
        }
    }
}

static void animated_sprite_component_draw(AnimatedSpriteState *state, CameraComponentData *active_camera, Actor *active_camera_actor) {
    if (!state || !state->valid)
        return;

    const AnimatedSpriteStateDef *state_definition = animated_sprite_current_state(state);
    const AnimatedSpriteFrame *frame = animated_sprite_current_frame(state);
    if (!state_definition || !frame)
        return;

    if (animated_sprite_ensure_sheet_loaded(state, state_definition->sheet_index) != Ok)
        return;

    const AnimatedSpriteSheet *sheet = &state->sheets[state_definition->sheet_index];
    Rectangle source = frame->source;

    if (state->flip_x) {
        source.x += source.width;
        source.width = -source.width;
    }

    if (state->flip_y) {
        source.y += source.height;
        source.height = -source.height;
    }

    float actor_x = 0.0f;
    float actor_y = 0.0f;
    float actor_scale_x = 1.0f;
    float actor_scale_y = 1.0f;
    if (state->actor) {
        actor_x = state->actor->transform.position.x;
        actor_y = state->actor->transform.position.y;
        actor_scale_x = state->actor->transform.scale.x;
        actor_scale_y = state->actor->transform.scale.y;
    }

    float destination_x = actor_x + state->position.x;
    float destination_y = actor_y + state->position.y;
    float destination_width = frame->source.width * state->scale * actor_scale_x;
    float destination_height = frame->source.height * state->scale * actor_scale_y;

    if (active_camera && active_camera_actor) {
        float zoom = 1.0f;
        if (active_camera->camera.fovy > 0.001f)
            zoom = 60.0f / active_camera->camera.fovy;

        const float camera_x = active_camera_actor->transform.position.x;
        const float camera_y = active_camera_actor->transform.position.y;

        destination_x = (destination_x - camera_x) * zoom + ((float)GetScreenWidth() * 0.5f);
        destination_y = (destination_y - camera_y) * zoom + ((float)GetScreenHeight() * 0.5f);
        destination_width *= zoom;
        destination_height *= zoom;
    }

    Rectangle destination = {
        destination_x,
        destination_y,
        destination_width,
        destination_height,
    };

    Vector2 anchor = {
        state->anchor.offset.x * state->scale * actor_scale_x,
        state->anchor.offset.y * state->scale * actor_scale_y,
    };

    if (state->anchor.center) {
        anchor.x = destination.width * 0.5f;
        anchor.y = destination.height * 0.5f;
    }

    DrawTexturePro(sheet->texture, source, destination, anchor, state->rotation, state->tint);
}

static void animated_sprite_component_dispose(AnimatedSpriteState *state) {
    if (!state)
        return;

    for (int sheet_index = 0; sheet_index < state->sheet_count; ++sheet_index) {
        AnimatedSpriteSheet *sheet = &state->sheets[sheet_index];
        if (sheet->loaded) {
            UnloadTexture(sheet->texture);
            sheet->loaded = False;
        }

        sheet->attempted_load = False;
    }

    state->valid = False;

    if (state->heap.pointer)
        deallocate(state->heap);
}

static void camera_component_sync(Actor *actor, CameraComponentData *camera_state) {
    if (!actor || !camera_state)
        return;

    camera_state->camera.position = (Vector3){
        actor->transform.position.x,
        actor->transform.position.y,
        actor->transform.position.z,
    };
}

static boolean contains_autoload_actor_id(const char *actor_id) {
    if (!actor_id || actor_id[0] == '\0')
        return False;

    for (usize index = 0; index < runtime_state.autoload_actor_count; ++index) {
        if (strcmp(runtime_state.autoload_actor_ids[index], actor_id) == 0)
            return True;
    }

    return False;
}

static result cache_autoload_actor_ids(toml_datum_t project_toptab) {
    runtime_state.autoload_actor_count = 0;

    if (project_toptab.type != TOML_TABLE)
        return Err;

    toml_datum_t persistence = toml_get(project_toptab, "Persistence");
    if (persistence.type != TOML_TABLE)
        return Ok;

    toml_datum_t autoload_actor_ids = toml_get(persistence, "autoload_actor_ids");
    if (autoload_actor_ids.type != TOML_ARRAY)
        return Ok;

    for (int index = 0; index < autoload_actor_ids.u.arr.size; ++index) {
        toml_datum_t item = autoload_actor_ids.u.arr.elem[index];
        if (item.type != TOML_STRING || !item.u.s || item.u.s[0] == '\0') {
            log_err("Persistence.autoload_actor_ids[%d] must be a non-empty string", index);
            return Err;
        }

        if (runtime_state.autoload_actor_count >= RuntimeMaxAutoloadActors) {
            log_warn("Persistence.autoload_actor_ids exceeded max %d entries; extras ignored", RuntimeMaxAutoloadActors);
            break;
        }

        if (snprintf(runtime_state.autoload_actor_ids[runtime_state.autoload_actor_count], RuntimeMaxAutoloadActorIdLength, "%s", item.u.s) >= RuntimeMaxAutoloadActorIdLength) {
            log_err("Autoload actor id is too long: '%s'", item.u.s);
            return Err;
        }

        runtime_state.autoload_actor_count++;
    }

    return Ok;
}

static boolean has_conf_extension(const char *file_name) {
    if (!file_name)
        return False;

    const usize name_len = strlen(file_name);
    const char *suffix = ".conf";
    const usize suffix_len = strlen(suffix);

    if (name_len < suffix_len)
        return False;

    return strcmp(file_name + (name_len - suffix_len), suffix) == 0;
}

static boolean is_absolute_path(const char *path) {
    return path && path[0] == '/';
}

static result join_path(const char *base, const char *path, char *out_path, usize out_size) {
    if (!base || !path || !out_path || out_size == 0)
        return Err;

    if (is_absolute_path(path)) {
        if (snprintf(out_path, out_size, "%s", path) >= (int)out_size)
            return Err;
        return Ok;
    }

    if (snprintf(out_path, out_size, "%s/%s", base, path) >= (int)out_size)
        return Err;

    return Ok;
}

static result parent_directory(const char *path, char *out_dir, usize out_size) {
    if (!path || !out_dir || out_size == 0)
        return Err;

    const char *last_slash = strrchr(path, '/');
    if (!last_slash) {
        if (snprintf(out_dir, out_size, ".") >= (int)out_size)
            return Err;
        return Ok;
    }

    const usize length = (usize)(last_slash - path);
    if (length == 0) {
        if (snprintf(out_dir, out_size, "/") >= (int)out_size)
            return Err;
        return Ok;
    }

    if (length + 1 > out_size)
        return Err;

    memcpy(out_dir, path, length);
    out_dir[length] = '\0';

    return Ok;
}

static result parse_toml_file(const char *path, toml_result_t *out_parsed) {
    if (!path || !out_parsed)
        return Err;

    toml_result_t parsed = toml_parse_file_ex(path);
    if (!parsed.ok) {
        log_err("Failed to parse config '%s': %s", path, parsed.errmsg);
        toml_free(parsed);
        return Err;
    }

    *out_parsed = parsed;
    return Ok;
}

static result validate_conf_files(const char *directory_path, usize *out_conf_count) {
    if (!directory_path || !out_conf_count)
        return Err;

    DIR *directory = opendir(directory_path);
    if (!directory) {
        log_err("Failed to open directory '%s'", directory_path);
        return Err;
    }

    struct dirent *entry = Null;
    while ((entry = readdir(directory)) != Null) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;

        char child_path[PATH_MAX] = {0};
        if (snprintf(child_path, sizeof(child_path), "%s/%s", directory_path, entry->d_name) >= (int)sizeof(child_path)) {
            log_err("Path too long while scanning '%s/%s'", directory_path, entry->d_name);
            closedir(directory);
            return Err;
        }

        struct stat stat_info = {0};
        if (lstat(child_path, &stat_info) != 0) {
            log_err("Failed to stat '%s'", child_path);
            closedir(directory);
            return Err;
        }

        if (S_ISDIR(stat_info.st_mode)) {
            if (validate_conf_files(child_path, out_conf_count) != Ok) {
                closedir(directory);
                return Err;
            }
            continue;
        }

        if (!S_ISREG(stat_info.st_mode) || !has_conf_extension(entry->d_name))
            continue;

        toml_result_t parsed = {0};
        if (parse_toml_file(child_path, &parsed) != Ok) {
            closedir(directory);
            return Err;
        }

        toml_free(parsed);
        (*out_conf_count)++;
    }

    closedir(directory);
    return Ok;
}

static void frame_update(void) {
    if (!runtime_state.active)
        return;

    process_pending_scene_load();
    (void)process_pending_prefab_instantiations();
    process_pending_actor_destroys();

    script_runtime_begin_frame(&runtime_state.script_runtime);
    const float delta_time = GetFrameTime();

    if (runtime_state.dj_enabled)
        update_dj(&runtime_state.dj);

    for (usize actor_index = 0; actor_index < runtime_state.actor_registry.actor_count; ++actor_index) {
        Actor *actor = &runtime_state.actor_registry.actors[actor_index];
        if (!actor->enabled)
            continue;

        for (usize component_index = 0; component_index < actor->component_count; ++component_index) {
            ActorComponent *component = &actor->components[component_index];
            if (component->descriptor.kind == ComponentScript)
                script_component_update(actor, component, &runtime_state.script_runtime);

            if (component->descriptor.kind == ComponentBuiltin && component->descriptor.name && strcmp(component->descriptor.name, "AnimatedSprite") == 0)
                animated_sprite_component_update((AnimatedSpriteState *)component->data, delta_time);

            if (actor->pending_destroy)
                break;
        }
    }

    process_pending_actor_destroys();

    runtime_state.active_camera = Null;
    runtime_state.active_camera_actor = Null;
    for (usize actor_index = 0; actor_index < runtime_state.actor_registry.actor_count; ++actor_index) {
        Actor *actor = &runtime_state.actor_registry.actors[actor_index];
        if (!actor->enabled)
            continue;

        CameraComponentData *camera_state = actor_find_camera_component(actor);
        if (!camera_state)
            continue;

        camera_component_sync(actor, camera_state);
        if (!runtime_state.active_camera && camera_state->active) {
            runtime_state.active_camera = camera_state;
            runtime_state.active_camera_actor = actor;
        }
    }

    usize enabled_actor_count = 0;
    for (usize actor_index = 0; actor_index < runtime_state.actor_registry.actor_count; ++actor_index) {
        Actor *actor = &runtime_state.actor_registry.actors[actor_index];
        if (actor->enabled)
            ++enabled_actor_count;
    }

    if (enabled_actor_count == 0)
        return;

    Heap draw_order_heap = allocate(enabled_actor_count, sizeof(Actor *));
    Actor **draw_order = (Actor **)draw_order_heap.pointer;
    if (!draw_order)
        return;

    usize draw_order_index = 0;
    for (usize actor_index = 0; actor_index < runtime_state.actor_registry.actor_count; ++actor_index) {
        Actor *actor = &runtime_state.actor_registry.actors[actor_index];
        if (actor->enabled)
            draw_order[draw_order_index++] = actor;
    }

    for (usize index = 1; index < draw_order_index; ++index) {
        Actor *candidate = draw_order[index];
        usize insertion_index = index;
        while (insertion_index > 0 && draw_order[insertion_index - 1]->layer > candidate->layer) {
            draw_order[insertion_index] = draw_order[insertion_index - 1];
            --insertion_index;
        }

        draw_order[insertion_index] = candidate;
    }

    for (usize index = 0; index < draw_order_index; ++index) {
        Actor *actor = draw_order[index];
        for (usize component_index = 0; component_index < actor->component_count; ++component_index) {
            ActorComponent *component = &actor->components[component_index];

            if (component->descriptor.kind == ComponentBuiltin && component->descriptor.name && strcmp(component->descriptor.name, "StaticSprite") == 0)
                static_sprite_component_draw((StaticSpriteState *)component->data, runtime_state.active_camera, runtime_state.active_camera_actor);

            if (component->descriptor.kind == ComponentBuiltin && component->descriptor.name && strcmp(component->descriptor.name, "AnimatedSprite") == 0)
                animated_sprite_component_draw((AnimatedSpriteState *)component->data, runtime_state.active_camera, runtime_state.active_camera_actor);
        }
    }

    if (runtime_state.ui_runtime)
        ui_runtime_draw(runtime_state.ui_runtime);

    deallocate(draw_order_heap);
}

static void dispose_runtime_components(void) {
    for (usize actor_index = 0; actor_index < runtime_state.actor_registry.actor_count; ++actor_index) {
        Actor *actor = &runtime_state.actor_registry.actors[actor_index];

        for (usize component_index = 0; component_index < actor->component_count; ++component_index) {
            ActorComponent *component = &actor->components[component_index];
            if (component->descriptor.kind == ComponentScript)
                script_component_destroy(actor, component, &runtime_state.script_runtime);

            if (component->descriptor.kind == ComponentBuiltin && component->descriptor.name && strcmp(component->descriptor.name, "StaticSprite") == 0)
                static_sprite_component_dispose((StaticSpriteState *)component->data);

            if (component->descriptor.kind == ComponentBuiltin && component->descriptor.name && strcmp(component->descriptor.name, "AnimatedSprite") == 0)
                animated_sprite_component_dispose((AnimatedSpriteState *)component->data);

            if (component->descriptor.kind == ComponentBuiltin && component->descriptor.name && strcmp(component->descriptor.name, "Camera") == 0)
                camera_component_dispose((CameraComponentData *)component->data);
        }
    }
}

static result read_vector3_from_table(toml_datum_t table, const char *key, Vector3 *out_vector) {
    if (!out_vector)
        return Err;

    ActorVector3 actor_vector = {0.0f, 0.0f, 0.0f};
    if (read_xyz_array(table, key, &actor_vector) != Ok)
        return Err;

    *out_vector = (Vector3){
        actor_vector.x,
        actor_vector.y,
        actor_vector.z,
    };

    return Ok;
}

static const toml_datum_t *find_camera_component_table(toml_datum_t actor_table, toml_datum_t *out_camera_table) {
    if (out_camera_table)
        *out_camera_table = (toml_datum_t){0};

    if (actor_table.type != TOML_TABLE || !out_camera_table)
        return Null;

    toml_datum_t overrides = toml_get(actor_table, "Overrides");
    if (overrides.type == TOML_TABLE) {
        toml_datum_t components = toml_get(overrides, "Components");
        if (components.type == TOML_TABLE) {
            toml_datum_t camera = toml_get(components, "Camera");
            if (camera.type == TOML_TABLE) {
                *out_camera_table = camera;
                return out_camera_table;
            }
        }
    }

    toml_datum_t components = toml_get(actor_table, "Components");
    if (components.type == TOML_TABLE) {
        toml_datum_t camera = toml_get(components, "Camera");
        if (camera.type == TOML_TABLE) {
            *out_camera_table = camera;
            return out_camera_table;
        }
    }

    return Null;
}

static result find_actor_table_by_id(toml_datum_t actors_array, const char *actor_id, toml_datum_t *out_actor_table) {
    if (actors_array.type != TOML_ARRAY || !actor_id || !out_actor_table)
        return Err;

    for (int index = 0; index < actors_array.u.arr.size; ++index) {
        toml_datum_t actor_table = actors_array.u.arr.elem[index];
        if (actor_table.type != TOML_TABLE)
            continue;

        toml_datum_t id = toml_get(actor_table, "id");
        if (id.type == TOML_STRING && id.u.s && strcmp(id.u.s, actor_id) == 0) {
            *out_actor_table = actor_table;
            return Ok;
        }
    }

    return Err;
}

static const char *find_script_module(toml_datum_t actor_table) {
    if (actor_table.type != TOML_TABLE)
        return Null;

    toml_datum_t overrides = toml_get(actor_table, "Overrides");
    if (overrides.type == TOML_TABLE) {
        toml_datum_t components = toml_get(overrides, "Components");
        if (components.type == TOML_TABLE) {
            toml_datum_t script = toml_get(components, "Script");
            if (script.type == TOML_TABLE) {
                toml_datum_t module = toml_get(script, "module");
                if (module.type == TOML_STRING && module.u.s && module.u.s[0] != '\0')
                    return module.u.s;
            }
        }
    }

    toml_datum_t components = toml_get(actor_table, "Components");
    if (components.type == TOML_TABLE) {
        toml_datum_t script = toml_get(components, "Script");
        if (script.type == TOML_TABLE) {
            toml_datum_t module = toml_get(script, "module");
            if (module.type == TOML_STRING && module.u.s && module.u.s[0] != '\0')
                return module.u.s;
        }
    }

    return Null;
}

static boolean read_actor_enabled(toml_datum_t actor_table) {
    toml_datum_t enabled = toml_get(actor_table, "enabled");
    if (enabled.type == TOML_BOOLEAN)
        return enabled.u.boolean ? True : False;

    return True;
}

static int read_actor_layer(toml_datum_t actor_table) {
    toml_datum_t layer = toml_get(actor_table, "layer");
    if (layer.type != TOML_INT64)
        return 0;

    if (layer.u.int64 > INT_MAX)
        return INT_MAX;

    if (layer.u.int64 < INT_MIN)
        return INT_MIN;

    return (int)layer.u.int64;
}

static result instantiate_actor_from_table(const char *actor_id, toml_datum_t actor_table, toml_datum_t prefab_refs, const char *prefabs_root, const char *success_log_label) {
    if (!actor_id || !success_log_label || actor_table.type != TOML_TABLE)
        return Err;

    if (find_actor_by_id(actor_id)) {
        log_err("Actor id '%s' already exists", actor_id);
        return Err;
    }

    Actor *actor = actor_registry_create_actor(&runtime_state.actor_registry, (char *)actor_id, read_actor_enabled(actor_table), read_actor_layer(actor_table));
    if (!actor) {
        log_err("Failed to create actor '%s'", actor_id);
        return Err;
    }

    actor->destroy_on_load = True;
    actor->pending_destroy = False;
    refresh_component_actor_backrefs();

    if (read_actor_transform(actor_table, &actor->transform) != Ok) {
        log_err("Actor '%s' has invalid Transform values", actor_id);
        return Err;
    }

    const char *module = find_script_module(actor_table);
    if (module) {
        void *script_state = script_component_state_create(module);
        if (!script_state) {
            log_err("Failed to create script state for actor '%s' (module '%s')", actor_id, module);
            return Err;
        }

        ComponentDescriptor descriptor = {
            .name = (char *)module,
            .kind = ComponentScript,
            .initialize = script_component_initialize,
            .context = &runtime_state.script_runtime,
        };

        if (actor_add_component(actor, descriptor, script_state) != Ok) {
            script_component_state_dispose(script_state);
            log_err("Failed to add script component '%s' to actor '%s'", module, actor_id);
            return Err;
        }
    }

    toml_datum_t camera_component_table = {0};
    if (find_camera_component_table(actor_table, &camera_component_table)) {
        Heap camera_heap = allocate(1, sizeof(CameraComponentData));
        CameraComponentData *camera_state = (CameraComponentData *)camera_heap.pointer;
        if (!camera_state) {
            log_err("Failed to allocate camera state for actor '%s'", actor_id);
            return Err;
        }

        memset(camera_state, 0, sizeof(*camera_state));
        camera_state->heap = camera_heap;
        camera_state->camera.position = (Vector3){
            actor->transform.position.x,
            actor->transform.position.y,
            actor->transform.position.z,
        };
        camera_state->camera.target = (Vector3){
            actor->transform.position.x,
            actor->transform.position.y,
            actor->transform.position.z - 1.0f,
        };
        camera_state->camera.up = (Vector3){0.0f, 1.0f, 0.0f};
        camera_state->camera.fovy = 60.0f;
        camera_state->camera.projection = CAMERA_PERSPECTIVE;
        camera_state->near_clip = 0.1f;
        camera_state->far_clip = 1000.0f;
        camera_state->active = True;

        toml_datum_t projection = toml_get(camera_component_table, "projection");
        if (projection.type == TOML_STRING && projection.u.s && projection.u.s[0] != '\0') {
            if (strcmp(projection.u.s, "orthographic") == 0)
                camera_state->camera.projection = CAMERA_ORTHOGRAPHIC;
            else
                camera_state->camera.projection = CAMERA_PERSPECTIVE;
        }

        float fov = 0.0f;
        toml_datum_t fov_value = toml_get(camera_component_table, "fov");
        if (toml_number_to_float(fov_value, &fov) && fov > 0.0f)
            camera_state->camera.fovy = fov;

        toml_datum_t fov_y_value = toml_get(camera_component_table, "fov_y");
        if (toml_number_to_float(fov_y_value, &fov) && fov > 0.0f)
            camera_state->camera.fovy = fov;

        float clip_value = 0.0f;
        toml_datum_t near_clip = toml_get(camera_component_table, "near_clip");
        if (toml_number_to_float(near_clip, &clip_value) && clip_value > 0.0f)
            camera_state->near_clip = clip_value;

        toml_datum_t far_clip = toml_get(camera_component_table, "far_clip");
        if (toml_number_to_float(far_clip, &clip_value) && clip_value > 0.0f)
            camera_state->far_clip = clip_value;

        toml_datum_t target = toml_get(camera_component_table, "target");
        if (target.type != TOML_UNKNOWN && read_vector3_from_table(camera_component_table, "target", &camera_state->camera.target) != Ok) {
            camera_component_dispose(camera_state);
            log_err("Actor '%s' has invalid Camera.target; expected [x, y, z]", actor_id);
            return Err;
        }

        toml_datum_t up = toml_get(camera_component_table, "up");
        if (up.type != TOML_UNKNOWN && read_vector3_from_table(camera_component_table, "up", &camera_state->camera.up) != Ok) {
            camera_component_dispose(camera_state);
            log_err("Actor '%s' has invalid Camera.up; expected [x, y, z]", actor_id);
            return Err;
        }

        toml_datum_t active = toml_get(camera_component_table, "active");
        if (active.type == TOML_BOOLEAN)
            camera_state->active = active.u.boolean ? True : False;

        ComponentDescriptor descriptor = {
            .name = "Camera",
            .kind = ComponentBuiltin,
            .initialize = camera_component_initialize,
            .context = Null,
        };

        if (actor_add_component(actor, descriptor, camera_state) != Ok) {
            camera_component_dispose(camera_state);
            log_err("Failed to add camera component to actor '%s'", actor_id);
            return Err;
        }
    }

    toml_datum_t animated_sprite_table = {0};
    if (find_animated_sprite_component_table(actor_table, &animated_sprite_table)) {
        toml_datum_t anim = toml_get(animated_sprite_table, "anim");
        if (anim.type != TOML_STRING || !anim.u.s || anim.u.s[0] == '\0') {
            log_err("Actor '%s' has AnimatedSprite component without a valid anim path", actor_id);
            return Err;
        }

        Heap animated_sprite_heap = allocate(1, sizeof(AnimatedSpriteState));
        AnimatedSpriteState *animated_sprite_state = (AnimatedSpriteState *)animated_sprite_heap.pointer;
        if (!animated_sprite_state) {
            log_err("Failed to allocate animated sprite state for actor '%s'", actor_id);
            return Err;
        }

        memset(animated_sprite_state, 0, sizeof(*animated_sprite_state));
        animated_sprite_state->heap = animated_sprite_heap;
        animated_sprite_state->actor = actor;
        animated_sprite_state->position = (Vector2){0.0f, 0.0f};
        animated_sprite_state->anchor = (SpriteAnchor){
            .center = False,
            .offset = (Vector2){0.0f, 0.0f},
        };
        animated_sprite_state->scale = 1.0f;
        animated_sprite_state->rotation = 0.0f;
        animated_sprite_state->tint = WHITE;
        animated_sprite_state->current_state_index = -1;

        if (snprintf(animated_sprite_state->anim_path, sizeof(animated_sprite_state->anim_path), "%s", anim.u.s) >= (int)sizeof(animated_sprite_state->anim_path)) {
            animated_sprite_component_dispose(animated_sprite_state);
            log_err("Animated sprite config path is too long for actor '%s'", actor_id);
            return Err;
        }

        toml_datum_t transform = toml_get(actor_table, "Transform");
        (void)read_xy_array(transform, "position", &animated_sprite_state->position);
        if (read_actor_transform_anchor(actor_table, prefab_refs, prefabs_root, &animated_sprite_state->anchor) != Ok) {
            animated_sprite_component_dispose(animated_sprite_state);
            log_err("Actor '%s' has invalid Transform.anchor; expected [x, y] or \"center\" (actor or prefab Transform)", actor_id);
            return Err;
        }

        (void)read_xy_array(animated_sprite_table, "position", &animated_sprite_state->position);

        toml_datum_t scale = toml_get(animated_sprite_table, "scale");
        float scale_value = 1.0f;
        if (toml_number_to_float(scale, &scale_value) && scale_value > 0.0f)
            animated_sprite_state->scale = scale_value;

        toml_datum_t rotation = toml_get(animated_sprite_table, "rotation");
        float rotation_value = 0.0f;
        if (toml_number_to_float(rotation, &rotation_value))
            animated_sprite_state->rotation = rotation_value;

        toml_datum_t tint = toml_get(animated_sprite_table, "tint");
        if (tint.type == TOML_ARRAY && tint.u.arr.size >= 4 &&
            tint.u.arr.elem[0].type == TOML_INT64 && tint.u.arr.elem[1].type == TOML_INT64 &&
            tint.u.arr.elem[2].type == TOML_INT64 && tint.u.arr.elem[3].type == TOML_INT64) {
            animated_sprite_state->tint = (Color){
                (unsigned char)tint.u.arr.elem[0].u.int64,
                (unsigned char)tint.u.arr.elem[1].u.int64,
                (unsigned char)tint.u.arr.elem[2].u.int64,
                (unsigned char)tint.u.arr.elem[3].u.int64,
            };
        }

        ComponentDescriptor descriptor = {
            .name = "AnimatedSprite",
            .kind = ComponentBuiltin,
            .initialize = animated_sprite_component_initialize,
            .context = Null,
        };

        if (actor_add_component(actor, descriptor, animated_sprite_state) != Ok) {
            animated_sprite_component_dispose(animated_sprite_state);
            log_err("Failed to add animated sprite component to actor '%s'", actor_id);
            return Err;
        }
    }

    toml_datum_t static_sprite_table = {0};
    const char *static_sprite_texture = find_static_sprite_texture(actor_table, &static_sprite_table);
    if (static_sprite_texture) {
        Heap sprite_heap = allocate(1, sizeof(StaticSpriteState));
        StaticSpriteState *sprite_state = (StaticSpriteState *)sprite_heap.pointer;
        if (!sprite_state) {
            log_err("Failed to allocate static sprite state for actor '%s'", actor_id);
            return Err;
        }

        memset(sprite_state, 0, sizeof(*sprite_state));
        sprite_state->heap = sprite_heap;
        sprite_state->actor = actor;
        sprite_state->position = (Vector2){0.0f, 0.0f};
        sprite_state->anchor = (SpriteAnchor){
            .center = False,
            .offset = (Vector2){0.0f, 0.0f},
        };
        sprite_state->scale = 1.0f;
        sprite_state->rotation = 0.0f;
        sprite_state->tint = WHITE;

        if (snprintf(sprite_state->texture_path, sizeof(sprite_state->texture_path), "%s", static_sprite_texture) >= (int)sizeof(sprite_state->texture_path)) {
            static_sprite_component_dispose(sprite_state);
            log_err("Static sprite texture path is too long for actor '%s'", actor_id);
            return Err;
        }

        toml_datum_t transform = toml_get(actor_table, "Transform");
        (void)read_xy_array(transform, "position", &sprite_state->position);
        if (read_actor_transform_anchor(actor_table, prefab_refs, prefabs_root, &sprite_state->anchor) != Ok) {
            static_sprite_component_dispose(sprite_state);
            log_err("Actor '%s' has invalid Transform.anchor; expected [x, y] or \"center\" (actor or prefab Transform)", actor_id);
            return Err;
        }
        (void)read_xy_array(static_sprite_table, "position", &sprite_state->position);

        toml_datum_t scale = toml_get(static_sprite_table, "scale");
        float scale_value = 1.0f;
        if (toml_number_to_float(scale, &scale_value) && scale_value > 0.0f)
            sprite_state->scale = scale_value;

        toml_datum_t rotation = toml_get(static_sprite_table, "rotation");
        float rotation_value = 0.0f;
        if (toml_number_to_float(rotation, &rotation_value))
            sprite_state->rotation = rotation_value;

        toml_datum_t tint = toml_get(static_sprite_table, "tint");
        if (tint.type == TOML_ARRAY && tint.u.arr.size >= 4 &&
            tint.u.arr.elem[0].type == TOML_INT64 && tint.u.arr.elem[1].type == TOML_INT64 &&
            tint.u.arr.elem[2].type == TOML_INT64 && tint.u.arr.elem[3].type == TOML_INT64) {
            sprite_state->tint = (Color){
                (unsigned char)tint.u.arr.elem[0].u.int64,
                (unsigned char)tint.u.arr.elem[1].u.int64,
                (unsigned char)tint.u.arr.elem[2].u.int64,
                (unsigned char)tint.u.arr.elem[3].u.int64,
            };
        }

        ComponentDescriptor descriptor = {
            .name = "StaticSprite",
            .kind = ComponentBuiltin,
            .initialize = static_sprite_component_initialize,
            .context = Null,
        };

        if (actor_add_component(actor, descriptor, sprite_state) != Ok) {
            static_sprite_component_dispose(sprite_state);
            log_err("Failed to add static sprite component to actor '%s'", actor_id);
            return Err;
        }
    }

    if (actor_initialize_components(actor) != Ok)
        return Err;

    log_msg("%s '%s'", success_log_label, actor_id);
    return Ok;
}

static result load_autoload_actors(toml_datum_t autoload_toptab) {
    if (runtime_state.autoload_actor_count == 0)
        return Ok;

    toml_datum_t data_actors = toml_get(autoload_toptab, "Actors");
    if (data_actors.type != TOML_ARRAY) {
        log_err("Autoload data is missing [[Actors]] array");
        return Err;
    }

    for (usize index = 0; index < runtime_state.autoload_actor_count; ++index) {
        const char *actor_id = runtime_state.autoload_actor_ids[index];

        toml_datum_t actor_table = {0};
        if (find_actor_table_by_id(data_actors, actor_id, &actor_table) != Ok) {
            log_err("Autoload actor '%s' listed in Persistence.autoload_actor_ids was not found in autoload data", actor_id);
            return Err;
        }

        if (instantiate_actor_from_table(actor_id, actor_table, (toml_datum_t){0}, Null, "Instantiated autoload actor") != Ok)
            return Err;
    }

    return Ok;
}

static result load_scene_runtime(const char *scene_path) {
    if (!scene_path || scene_path[0] == '\0')
        return Err;

    toml_result_t scene_toml = {0};
    toml_result_t scene_data_toml = {0};
    toml_result_t autoload_toml = {0};
    boolean scene_ok = False;
    boolean scene_data_ok = False;
    boolean autoload_ok = False;

    char resolved_scene_path[PATH_MAX] = {0};
    if (join_path(runtime_state.scenes_root, scene_path, resolved_scene_path, sizeof(resolved_scene_path)) != Ok) {
        log_err("Failed to resolve scene path '%s'", scene_path);
        goto fail;
    }

    if (parse_toml_file(resolved_scene_path, &scene_toml) != Ok)
        goto fail;
    scene_ok = True;

    toml_datum_t scene_table = toml_get(scene_toml.toptab, "Scene");
    if (scene_table.type != TOML_TABLE) {
        log_err("Scene config '%s' is missing [Scene] table", resolved_scene_path);
        goto fail;
    }

    toml_datum_t scene_data_file = toml_get(scene_table, "data_file");
    if (scene_data_file.type != TOML_STRING || !scene_data_file.u.s || scene_data_file.u.s[0] == '\0') {
        log_err("Scene config '%s' is missing Scene.data_file", resolved_scene_path);
        goto fail;
    }

    char scene_directory[PATH_MAX] = {0};
    if (parent_directory(resolved_scene_path, scene_directory, sizeof(scene_directory)) != Ok) {
        log_err("Failed to resolve scene directory for '%s'", resolved_scene_path);
        goto fail;
    }

    char scene_data_path[PATH_MAX] = {0};
    if (join_path(scene_directory, scene_data_file.u.s, scene_data_path, sizeof(scene_data_path)) != Ok) {
        log_err("Failed to resolve Scene.data_file path for '%s'", resolved_scene_path);
        goto fail;
    }

    if (parse_toml_file(scene_data_path, &scene_data_toml) != Ok)
        goto fail;
    scene_data_ok = True;

    if (parse_toml_file(runtime_state.autoload_data_path, &autoload_toml) != Ok)
        goto fail;
    autoload_ok = True;

    runtime_state.pending_prefab_instantiation_count = 0;

    dispose_runtime_components();
    actor_registry_dispose(&runtime_state.actor_registry);
    actor_registry_init(&runtime_state.actor_registry);
    script_runtime_bind_registry(&runtime_state.script_runtime, &runtime_state.actor_registry);
    runtime_state.active_camera = Null;
    runtime_state.active_camera_actor = Null;

    if (load_autoload_actors(autoload_toml.toptab) != Ok)
        goto fail;

    if (load_scene_actors(scene_toml.toptab, scene_data_toml.toptab, runtime_state.prefabs_root) != Ok)
        goto fail;

    if (runtime_state.ui_runtime) {
        if (ui_runtime_unload_scene_documents(runtime_state.ui_runtime) != Ok)
            goto fail;

        if (ui_runtime_load_scene_documents(runtime_state.ui_runtime, scene_toml.toptab) != Ok)
            goto fail;
    }

    if (snprintf(runtime_state.current_scene_path, sizeof(runtime_state.current_scene_path), "%s", resolved_scene_path) >= (int)sizeof(runtime_state.current_scene_path)) {
        log_err("Resolved scene path is too long: '%s'", resolved_scene_path);
        goto fail;
    }

    log_msg("Loaded scene '%s'", resolved_scene_path);

    if (autoload_ok)
        toml_free(autoload_toml);
    if (scene_data_ok)
        toml_free(scene_data_toml);
    if (scene_ok)
        toml_free(scene_toml);

    return Ok;

fail:
    if (autoload_ok)
        toml_free(autoload_toml);
    if (scene_data_ok)
        toml_free(scene_data_toml);
    if (scene_ok)
        toml_free(scene_toml);

    return Err;
}

static void process_pending_scene_load(void) {
    if (!runtime_state.has_pending_scene_load)
        return;

    char scene_path[PATH_MAX] = {0};
    if (snprintf(scene_path, sizeof(scene_path), "%s", runtime_state.pending_scene_path) >= (int)sizeof(scene_path)) {
        log_err("Pending scene path too long");
        runtime_state.has_pending_scene_load = False;
        runtime_state.pending_scene_path[0] = '\0';
        return;
    }

    runtime_state.has_pending_scene_load = False;
    runtime_state.pending_scene_path[0] = '\0';

    if (load_scene_runtime(scene_path) != Ok)
        log_err("Failed to switch to scene '%s'", scene_path);
}

static result load_scene_actors(toml_datum_t scene_toptab, toml_datum_t data_toptab, const char *prefabs_root) {
    toml_datum_t scene_table = toml_get(scene_toptab, "Scene");
    if (scene_table.type != TOML_TABLE) {
        log_err("Scene manifest is missing [Scene] table");
        return Err;
    }

    toml_datum_t scene_load = toml_get(scene_table, "Load");
    if (scene_load.type != TOML_TABLE) {
        log_err("Scene manifest is missing [Scene.Load] table");
        return Err;
    }

    toml_datum_t scene_actor_ids = toml_get(scene_load, "actors");
    if (scene_actor_ids.type != TOML_ARRAY) {
        log_err("Scene manifest is missing Scene.Load.actors array");
        return Err;
    }

    toml_datum_t data_actors = toml_get(data_toptab, "Actors");
    if (data_actors.type != TOML_ARRAY) {
        log_err("Scene data is missing [[Actors]] array");
        return Err;
    }

    toml_datum_t prefab_refs = toml_get(data_toptab, "PrefabRefs");

    for (int index = 0; index < scene_actor_ids.u.arr.size; ++index) {
        toml_datum_t actor_id = scene_actor_ids.u.arr.elem[index];
        if (actor_id.type != TOML_STRING || !actor_id.u.s || actor_id.u.s[0] == '\0') {
            log_err("Scene.Load.actors[%d] must be a non-empty string", index);
            return Err;
        }

        toml_datum_t actor_table = {0};
        if (find_actor_table_by_id(data_actors, actor_id.u.s, &actor_table) != Ok) {
            log_err("Actor '%s' listed in Scene.Load.actors was not found in scene data", actor_id.u.s);
            return Err;
        }

        if (instantiate_actor_from_table(actor_id.u.s, actor_table, prefab_refs, prefabs_root, "Instantiated actor") != Ok)
            return Err;
    }

    return Ok;
}

result run_project_runtime(const char *project_path) {
    toml_result_t project_toml = {0};
    boolean project_ok = False;
    boolean window_opened = False;

    struct stat project_stat = {0};
    if (!project_path || stat(project_path, &project_stat) != 0 || !S_ISDIR(project_stat.st_mode)) {
        log_err("Project path '%s' is not a valid directory", project_path ? project_path : "<null>");
        return Err;
    }

    usize conf_count = 0;
    if (validate_conf_files(project_path, &conf_count) != Ok)
        return Err;

    if (conf_count == 0) {
        log_err("No .conf files found under '%s'", project_path);
        return Err;
    }

    memset(&runtime_state, 0, sizeof(runtime_state));

    char springengine_config_path[PATH_MAX] = {0};
    if (join_path(project_path, "springengine.conf", springengine_config_path, sizeof(springengine_config_path)) != Ok) {
        log_err("Failed to resolve springengine.conf path for '%s'", project_path);
        return Err;
    }

    if (access(springengine_config_path, F_OK) != 0) {
        log_err("Required config missing: '%s'", springengine_config_path);
        return Err;
    }

    WindowConfig window_config = DefaultWindowConfig;
    if (load_project_window_config(springengine_config_path, &window_config) != Ok) {
        log_err("Failed to load required window settings from '%s'", springengine_config_path);
        return Err;
    }

    if (parse_toml_file(springengine_config_path, &project_toml) != Ok)
        return Err;
    project_ok = True;

    toml_datum_t boot_table = toml_get(project_toml.toptab, "Boot");
    if (boot_table.type != TOML_TABLE) {
        log_err("Config '%s' is missing [Boot] table", springengine_config_path);
        goto fail;
    }

    toml_datum_t first_scene = toml_get(boot_table, "first_scene");
    if (first_scene.type != TOML_STRING || !first_scene.u.s || first_scene.u.s[0] == '\0') {
        log_err("Config '%s' is missing Boot.first_scene", springengine_config_path);
        goto fail;
    }

    toml_datum_t autoload_data = toml_get(boot_table, "autoload_data");
    if (autoload_data.type != TOML_STRING || !autoload_data.u.s || autoload_data.u.s[0] == '\0') {
        log_err("Config '%s' is missing Boot.autoload_data", springengine_config_path);
        goto fail;
    }

    if (snprintf(runtime_state.scenes_root, sizeof(runtime_state.scenes_root), "%s", project_path) >= (int)sizeof(runtime_state.scenes_root)) {
        log_err("Project path is too long: '%s'", project_path);
        goto fail;
    }
    if (snprintf(runtime_state.prefabs_root, sizeof(runtime_state.prefabs_root), "%s", project_path) >= (int)sizeof(runtime_state.prefabs_root)) {
        log_err("Project path is too long: '%s'", project_path);
        goto fail;
    }
    if (snprintf(runtime_state.ui_root, sizeof(runtime_state.ui_root), "%s", project_path) >= (int)sizeof(runtime_state.ui_root)) {
        log_err("Project path is too long: '%s'", project_path);
        goto fail;
    }

    toml_datum_t paths_table = toml_get(project_toml.toptab, "Paths");
    if (paths_table.type == TOML_TABLE) {
        toml_datum_t scenes_dir = toml_get(paths_table, "scenes_dir");
        if (scenes_dir.type == TOML_STRING && scenes_dir.u.s && scenes_dir.u.s[0] != '\0') {
            if (join_path(project_path, scenes_dir.u.s, runtime_state.scenes_root, sizeof(runtime_state.scenes_root)) != Ok) {
                log_err("Failed to resolve Paths.scenes_dir from '%s'", springengine_config_path);
                goto fail;
            }
        }

        toml_datum_t prefabs_dir = toml_get(paths_table, "prefabs_dir");
        if (prefabs_dir.type == TOML_STRING && prefabs_dir.u.s && prefabs_dir.u.s[0] != '\0') {
            if (join_path(project_path, prefabs_dir.u.s, runtime_state.prefabs_root, sizeof(runtime_state.prefabs_root)) != Ok) {
                log_err("Failed to resolve Paths.prefabs_dir from '%s'", springengine_config_path);
                goto fail;
            }
        }

        toml_datum_t ui_dir = toml_get(paths_table, "ui_dir");
        if (ui_dir.type == TOML_STRING && ui_dir.u.s && ui_dir.u.s[0] != '\0') {
            if (join_path(project_path, ui_dir.u.s, runtime_state.ui_root, sizeof(runtime_state.ui_root)) != Ok) {
                log_err("Failed to resolve Paths.ui_dir from '%s'", springengine_config_path);
                goto fail;
            }
        }
    }

    if (join_path(project_path, autoload_data.u.s, runtime_state.autoload_data_path, sizeof(runtime_state.autoload_data_path)) != Ok) {
        log_err("Failed to resolve Boot.autoload_data path");
        goto fail;
    }

    if (snprintf(runtime_state.project_root, sizeof(runtime_state.project_root), "%s", project_path) >= (int)sizeof(runtime_state.project_root)) {
        log_err("Project path is too long: '%s'", project_path);
        goto fail;
    }

    runtime_state.has_pending_scene_load = False;
    runtime_state.pending_scene_path[0] = '\0';
    runtime_state.current_scene_path[0] = '\0';
    runtime_state.ui_runtime = Null;

    if (cache_autoload_actor_ids(project_toml.toptab) != Ok)
        goto fail;

    runtime_state.dj_enabled = contains_autoload_actor_id("global_audio");
    if (runtime_state.dj_enabled) {
        runtime_state.dj = init_dj();
        log_msg("Initialized DJ runtime (Persistence.autoload_actor_ids contains 'global_audio')");
    }

    actor_registry_init(&runtime_state.actor_registry);
    runtime_state.active = True;
    if (script_runtime_init(&runtime_state.script_runtime, project_path, runtime_state.dj_enabled ? &runtime_state.dj : Null) != Ok)
        goto fail;
    script_runtime_bind_registry(&runtime_state.script_runtime, &runtime_state.actor_registry);

    if (ui_runtime_create(&runtime_state.ui_runtime, project_path, runtime_state.ui_root, &runtime_state.script_runtime) != Ok)
        goto fail;

    if (load_scene_runtime(first_scene.u.s) != Ok)
        goto fail;

    log_msg("Loaded project config '%s'", springengine_config_path);
    log_msg("Loaded autoload data '%s'", runtime_state.autoload_data_path);

    if (open_window(window_config) != Ok)
        goto fail;
    window_opened = True;

    while (!update_window(frame_update)) {}

    dispose_runtime_components();
    actor_registry_dispose(&runtime_state.actor_registry);
    script_runtime_dispose(&runtime_state.script_runtime);
    if (runtime_state.ui_runtime) {
        ui_runtime_destroy(runtime_state.ui_runtime);
        runtime_state.ui_runtime = Null;
    }
    if (runtime_state.dj_enabled) {
        dispose_dj(&runtime_state.dj);
        runtime_state.dj_enabled = False;
    }

    if (window_opened)
        close_window();

    runtime_state.active = False;
    runtime_state.project_root[0] = '\0';
    runtime_state.scenes_root[0] = '\0';
    runtime_state.prefabs_root[0] = '\0';
    runtime_state.ui_root[0] = '\0';
    runtime_state.autoload_data_path[0] = '\0';
    runtime_state.current_scene_path[0] = '\0';
    runtime_state.pending_scene_path[0] = '\0';
    runtime_state.has_pending_scene_load = False;
    runtime_state.autoload_actor_count = 0;

    if (project_ok)
        toml_free(project_toml);

    return Ok;

fail:
    if (runtime_state.active) {
        dispose_runtime_components();
        actor_registry_dispose(&runtime_state.actor_registry);
        script_runtime_dispose(&runtime_state.script_runtime);
        if (runtime_state.ui_runtime) {
            ui_runtime_destroy(runtime_state.ui_runtime);
            runtime_state.ui_runtime = Null;
        }
        if (runtime_state.dj_enabled) {
            dispose_dj(&runtime_state.dj);
            runtime_state.dj_enabled = False;
        }

        if (window_opened)
            close_window();

        runtime_state.active = False;
        runtime_state.project_root[0] = '\0';
        runtime_state.scenes_root[0] = '\0';
        runtime_state.prefabs_root[0] = '\0';
        runtime_state.ui_root[0] = '\0';
        runtime_state.autoload_data_path[0] = '\0';
        runtime_state.current_scene_path[0] = '\0';
        runtime_state.pending_scene_path[0] = '\0';
        runtime_state.has_pending_scene_load = False;
        runtime_state.autoload_actor_count = 0;
    }

    if (project_ok)
        toml_free(project_toml);

    return Err;
}

result runtime_request_scene_load(const char *scene_path) {
    if (!runtime_state.active || !scene_path || scene_path[0] == '\0')
        return Err;

    if (snprintf(runtime_state.pending_scene_path, sizeof(runtime_state.pending_scene_path), "%s", scene_path) >= (int)sizeof(runtime_state.pending_scene_path)) {
        log_err("Requested scene path is too long: '%s'", scene_path);
        return Err;
    }

    runtime_state.has_pending_scene_load = True;
    return Ok;
}

const char *runtime_current_scene_path(void) {
    if (!runtime_state.current_scene_path[0])
        return Null;

    return runtime_state.current_scene_path;
}

result runtime_instantiate_prefab(const char *prefab_ref_id, boolean has_position, float x, float y, float z, char *out_actor_id, usize out_actor_id_size) {
    if (!runtime_state.active)
        return Err;

    return enqueue_prefab_instantiation(prefab_ref_id, has_position, x, y, z, out_actor_id, out_actor_id_size);
}

result runtime_destroy_actor(const char *actor_id) {
    if (!runtime_state.active || !actor_id || actor_id[0] == '\0')
        return Err;

    Actor *actor = find_actor_by_id(actor_id);
    if (!actor)
        return Err;

    actor->pending_destroy = True;
    return Ok;
}

result runtime_set_actor_destroy_on_load(const char *actor_id, boolean destroy_on_load) {
    if (!runtime_state.active || !actor_id || actor_id[0] == '\0')
        return Err;

    Actor *actor = find_actor_by_id(actor_id);
    if (!actor)
        return Err;

    actor->destroy_on_load = destroy_on_load ? True : False;
    return Ok;
}

result runtime_get_actor_destroy_on_load(const char *actor_id, boolean *out_destroy_on_load) {
    if (!runtime_state.active || !actor_id || actor_id[0] == '\0' || !out_destroy_on_load)
        return Err;

    Actor *actor = find_actor_by_id(actor_id);
    if (!actor)
        return Err;

    *out_destroy_on_load = actor->destroy_on_load ? True : False;
    return Ok;
}

result runtime_ui_node_exists(const char *document_id, const char *node_id) {
    if (!runtime_state.active || !runtime_state.ui_runtime || !document_id || !node_id)
        return Err;

    return ui_runtime_node_exists(runtime_state.ui_runtime, document_id, node_id);
}

result runtime_ui_set_visible(const char *document_id, const char *node_id, boolean visible) {
    if (!runtime_state.active || !runtime_state.ui_runtime || !document_id || !node_id)
        return Err;

    return ui_runtime_set_node_visible(runtime_state.ui_runtime, document_id, node_id, visible);
}

result runtime_ui_set_text(const char *document_id, const char *node_id, const char *text) {
    if (!runtime_state.active || !runtime_state.ui_runtime || !document_id || !node_id || !text)
        return Err;

    return ui_runtime_set_node_text(runtime_state.ui_runtime, document_id, node_id, text);
}

result runtime_ui_push_document(const char *document_path) {
    if (!runtime_state.active || !runtime_state.ui_runtime || !document_path)
        return Err;

    return ui_runtime_push_document(runtime_state.ui_runtime, document_path);
}

result runtime_ui_pop_document(const char *document_id) {
    if (!runtime_state.active || !runtime_state.ui_runtime || !document_id)
        return Err;

    return ui_runtime_pop_document(runtime_state.ui_runtime, document_id);
}

result runtime_ui_set_document_layer(const char *document_id, int layer) {
    if (!runtime_state.active || !runtime_state.ui_runtime || !document_id)
        return Err;

    return ui_runtime_set_document_layer(runtime_state.ui_runtime, document_id, layer);
}

result runtime_ui_bring_document_to_front(const char *document_id) {
    if (!runtime_state.active || !runtime_state.ui_runtime || !document_id)
        return Err;

    return ui_runtime_bring_document_to_front(runtime_state.ui_runtime, document_id);
}

result runtime_ui_send_document_to_back(const char *document_id) {
    if (!runtime_state.active || !runtime_state.ui_runtime || !document_id)
        return Err;

    return ui_runtime_send_document_to_back(runtime_state.ui_runtime, document_id);
}

result runtime_ui_get_document_count(usize *out_count) {
    if (!runtime_state.active || !runtime_state.ui_runtime || !out_count)
        return Err;

    return ui_runtime_get_document_count(runtime_state.ui_runtime, out_count);
}

const char *runtime_ui_get_document_id_at(usize index) {
    if (!runtime_state.active || !runtime_state.ui_runtime)
        return Null;

    const char *document_id = Null;
    if (ui_runtime_get_document_id_at(runtime_state.ui_runtime, index, &document_id) != Ok)
        return Null;

    return document_id;
}
