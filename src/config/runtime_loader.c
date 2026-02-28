#include "runtime_loader.h"

#include "actor/actor.h"
#include "dj/dj.h"
#include "project_config.h"
#include "script/script_runtime.h"
#include "windowman/windowman.h"

#include "tomlc17.h"

#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

typedef struct {
    boolean center;
    Vector2 offset;
} SpriteAnchor;

typedef struct {
    char texture_path[PATH_MAX];
    char resolved_texture_path[PATH_MAX];
    Vector2 position;
    SpriteAnchor anchor;
    float scale;
    float rotation;
    Color tint;
    Texture2D texture;
    boolean loaded;
    boolean attempted_load;
    Heap heap;
} StaticSpriteState;

typedef struct {
    ActorRegistry actor_registry;
    DJ dj;
    ScriptRuntime script_runtime;
    boolean dj_enabled;
    boolean active;
    char project_root[PATH_MAX];
} RuntimeState;

static RuntimeState runtime_state;

static result join_path(const char *base, const char *path, char *out_path, usize out_size);
static result parse_toml_file(const char *path, toml_result_t *out_parsed);

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

static void static_sprite_component_draw(StaticSpriteState *state) {
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

    Rectangle destination = {
        state->position.x,
        state->position.y,
        (float)state->texture.width * state->scale,
        (float)state->texture.height * state->scale,
    };

    Vector2 anchor = {
        state->anchor.offset.x * state->scale,
        state->anchor.offset.y * state->scale,
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

static boolean contains_autoload_actor_id(toml_datum_t project_toptab, const char *actor_id) {
    if (project_toptab.type != TOML_TABLE || !actor_id || actor_id[0] == '\0')
        return False;

    toml_datum_t persistence = toml_get(project_toptab, "Persistence");
    if (persistence.type != TOML_TABLE)
        return False;

    toml_datum_t autoload_actor_ids = toml_get(persistence, "autoload_actor_ids");
    if (autoload_actor_ids.type != TOML_ARRAY)
        return False;

    for (int index = 0; index < autoload_actor_ids.u.arr.size; ++index) {
        toml_datum_t item = autoload_actor_ids.u.arr.elem[index];
        if (item.type == TOML_STRING && item.u.s && strcmp(item.u.s, actor_id) == 0)
            return True;
    }

    return False;
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
                static_sprite_component_draw((StaticSpriteState *)component->data);
        }
    }

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
        }
    }
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

    Actor *actor = actor_registry_create_actor(&runtime_state.actor_registry, (char *)actor_id, read_actor_enabled(actor_table), read_actor_layer(actor_table));
    if (!actor) {
        log_err("Failed to create actor '%s'", actor_id);
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

static result load_autoload_actors(toml_datum_t project_toptab, toml_datum_t autoload_toptab) {
    toml_datum_t persistence = toml_get(project_toptab, "Persistence");
    if (persistence.type != TOML_TABLE) {
        log_warn("Config is missing [Persistence] table; skipping autoload actors");
        return Ok;
    }

    toml_datum_t autoload_actor_ids = toml_get(persistence, "autoload_actor_ids");
    if (autoload_actor_ids.type != TOML_ARRAY) {
        log_warn("Config is missing Persistence.autoload_actor_ids array; skipping autoload actors");
        return Ok;
    }

    toml_datum_t data_actors = toml_get(autoload_toptab, "Actors");
    if (data_actors.type != TOML_ARRAY) {
        log_err("Autoload data is missing [[Actors]] array");
        return Err;
    }

    for (int index = 0; index < autoload_actor_ids.u.arr.size; ++index) {
        toml_datum_t actor_id = autoload_actor_ids.u.arr.elem[index];
        if (actor_id.type != TOML_STRING || !actor_id.u.s || actor_id.u.s[0] == '\0') {
            log_err("Persistence.autoload_actor_ids[%d] must be a non-empty string", index);
            return Err;
        }

        toml_datum_t actor_table = {0};
        if (find_actor_table_by_id(data_actors, actor_id.u.s, &actor_table) != Ok) {
            log_err("Autoload actor '%s' listed in Persistence.autoload_actor_ids was not found in autoload data", actor_id.u.s);
            return Err;
        }

        if (instantiate_actor_from_table(actor_id.u.s, actor_table, (toml_datum_t){0}, Null, "Instantiated autoload actor") != Ok)
            return Err;
    }

    return Ok;
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
    toml_result_t autoload_toml = {0};
    toml_result_t scene_toml = {0};
    toml_result_t scene_data_toml = {0};
    boolean project_ok = False;
    boolean autoload_ok = False;
    boolean scene_ok = False;
    boolean scene_data_ok = False;

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

    char scenes_root[PATH_MAX] = {0};
    char prefabs_root[PATH_MAX] = {0};
    if (snprintf(scenes_root, sizeof(scenes_root), "%s", project_path) >= (int)sizeof(scenes_root)) {
        log_err("Project path is too long: '%s'", project_path);
        goto fail;
    }
    if (snprintf(prefabs_root, sizeof(prefabs_root), "%s", project_path) >= (int)sizeof(prefabs_root)) {
        log_err("Project path is too long: '%s'", project_path);
        goto fail;
    }

    toml_datum_t paths_table = toml_get(project_toml.toptab, "Paths");
    if (paths_table.type == TOML_TABLE) {
        toml_datum_t scenes_dir = toml_get(paths_table, "scenes_dir");
        if (scenes_dir.type == TOML_STRING && scenes_dir.u.s && scenes_dir.u.s[0] != '\0') {
            if (join_path(project_path, scenes_dir.u.s, scenes_root, sizeof(scenes_root)) != Ok) {
                log_err("Failed to resolve Paths.scenes_dir from '%s'", springengine_config_path);
                goto fail;
            }
        }

        toml_datum_t prefabs_dir = toml_get(paths_table, "prefabs_dir");
        if (prefabs_dir.type == TOML_STRING && prefabs_dir.u.s && prefabs_dir.u.s[0] != '\0') {
            if (join_path(project_path, prefabs_dir.u.s, prefabs_root, sizeof(prefabs_root)) != Ok) {
                log_err("Failed to resolve Paths.prefabs_dir from '%s'", springengine_config_path);
                goto fail;
            }
        }
    }

    char autoload_path[PATH_MAX] = {0};
    if (join_path(project_path, autoload_data.u.s, autoload_path, sizeof(autoload_path)) != Ok) {
        log_err("Failed to resolve Boot.autoload_data path");
        goto fail;
    }

    if (parse_toml_file(autoload_path, &autoload_toml) != Ok)
        goto fail;
    autoload_ok = True;

    char first_scene_path[PATH_MAX] = {0};
    if (join_path(scenes_root, first_scene.u.s, first_scene_path, sizeof(first_scene_path)) != Ok) {
        log_err("Failed to resolve Boot.first_scene path");
        goto fail;
    }

    if (parse_toml_file(first_scene_path, &scene_toml) != Ok)
        goto fail;
    scene_ok = True;

    toml_datum_t scene_table = toml_get(scene_toml.toptab, "Scene");
    if (scene_table.type != TOML_TABLE) {
        log_err("Scene config '%s' is missing [Scene] table", first_scene_path);
        goto fail;
    }

    toml_datum_t scene_data_file = toml_get(scene_table, "data_file");
    if (scene_data_file.type != TOML_STRING || !scene_data_file.u.s || scene_data_file.u.s[0] == '\0') {
        log_err("Scene config '%s' is missing Scene.data_file", first_scene_path);
        goto fail;
    }

    char first_scene_directory[PATH_MAX] = {0};
    if (parent_directory(first_scene_path, first_scene_directory, sizeof(first_scene_directory)) != Ok) {
        log_err("Failed to resolve first scene directory for '%s'", first_scene_path);
        goto fail;
    }

    char scene_data_path[PATH_MAX] = {0};
    if (join_path(first_scene_directory, scene_data_file.u.s, scene_data_path, sizeof(scene_data_path)) != Ok) {
        log_err("Failed to resolve Scene.data_file path");
        goto fail;
    }

    if (parse_toml_file(scene_data_path, &scene_data_toml) != Ok)
        goto fail;
    scene_data_ok = True;

    if (snprintf(runtime_state.project_root, sizeof(runtime_state.project_root), "%s", project_path) >= (int)sizeof(runtime_state.project_root)) {
        log_err("Project path is too long: '%s'", project_path);
        goto fail;
    }

    runtime_state.dj_enabled = contains_autoload_actor_id(project_toml.toptab, "global_audio");
    if (runtime_state.dj_enabled) {
        runtime_state.dj = init_dj();
        log_msg("Initialized DJ runtime (Persistence.autoload_actor_ids contains 'global_audio')");
    }

    actor_registry_init(&runtime_state.actor_registry);
    runtime_state.active = True;
    if (script_runtime_init(&runtime_state.script_runtime, project_path, runtime_state.dj_enabled ? &runtime_state.dj : Null) != Ok)
        goto fail;

    if (load_autoload_actors(project_toml.toptab, autoload_toml.toptab) != Ok)
        goto fail;

    if (load_scene_actors(scene_toml.toptab, scene_data_toml.toptab, prefabs_root) != Ok)
        goto fail;

    log_msg("Loaded project config '%s'", springengine_config_path);
    log_msg("Loaded autoload data '%s'", autoload_path);
    log_msg("Loaded first scene '%s'", first_scene_path);
    log_msg("Loaded first scene data '%s'", scene_data_path);

    if (open_window(window_config) != Ok)
        goto fail;

    while (!update_window(frame_update)) {}

    close_window();

    dispose_runtime_components();
    actor_registry_dispose(&runtime_state.actor_registry);
    script_runtime_dispose(&runtime_state.script_runtime);
    if (runtime_state.dj_enabled) {
        dispose_dj(&runtime_state.dj);
        runtime_state.dj_enabled = False;
    }
    runtime_state.active = False;
    runtime_state.project_root[0] = '\0';

    if (scene_data_ok)
        toml_free(scene_data_toml);
    if (scene_ok)
        toml_free(scene_toml);
    if (autoload_ok)
        toml_free(autoload_toml);
    if (project_ok)
        toml_free(project_toml);

    return Ok;

fail:
    if (runtime_state.active) {
        dispose_runtime_components();
        actor_registry_dispose(&runtime_state.actor_registry);
        script_runtime_dispose(&runtime_state.script_runtime);
        if (runtime_state.dj_enabled) {
            dispose_dj(&runtime_state.dj);
            runtime_state.dj_enabled = False;
        }
        runtime_state.active = False;
        runtime_state.project_root[0] = '\0';
    }

    if (scene_data_ok)
        toml_free(scene_data_toml);
    if (scene_ok)
        toml_free(scene_toml);
    if (autoload_ok)
        toml_free(autoload_toml);
    if (project_ok)
        toml_free(project_toml);

    return Err;
 }
