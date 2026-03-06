#include "runtime_loader.h"
#include "runtime_loader_internal.h"

#include "actor/actor.h"
#include "actor/camera_component.h"
#include "actor/collider_component.h"
#include "actor/light_component.h"
#include "actor/rigidbody_component.h"
#include "config/static_sprite_component.h"
#include "dj/dj.h"
#include "project_config.h"
#include "script/script_runtime.h"
#include "version_config.h"
#include "ui/ui_runtime.h"
#include "vfs.h"
#include "windowman/windowman.h"

#include "tomlc17.h"

#include <dirent.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct SharedShaderEntry SharedShaderEntry;
typedef struct PostFxPassConfig PostFxPassConfig;

static result join_path(const char *base, const char *path, char *out_path, usize out_size);
static result parent_directory(const char *path, char *out_dir, usize out_size);
static result parse_toml_file(const char *path, toml_result_t *out_parsed);
static result load_autoload_actors(toml_datum_t autoload_toptab);
static result load_scene_actors(toml_datum_t scene_toptab, toml_datum_t data_toptab, const char *prefabs_root);
static result instantiate_actor_from_table(const char *actor_id, toml_datum_t actor_table, toml_datum_t prefab_refs, const char *prefabs_root, const char *success_log_label);
static int find_actor_index_by_id(const char *actor_id);
static result validate_actor_parent_links(void);
static void apply_parent_transform_deltas(void);
static void sync_parented_actor_positions(void);
static void capture_actor_previous_transforms(void);
Actor *find_actor_by_id(const char *actor_id);
static void refresh_component_actor_backrefs(void);
static void dispose_actor_components(Actor *actor);
static void destroy_actor_at_index(usize actor_index);
static void process_pending_actor_destroys(void);
static result process_pending_prefab_instantiations(void);
result enqueue_prefab_instantiation(const char *prefab_ref_id, boolean has_position, float x, float y, float z, char *out_actor_id, usize out_actor_id_size);
static result resolve_prefab_path_from_ref(const char *prefab_ref_id, char *out_prefab_path, usize out_prefab_path_size);
static result load_scene_runtime(const char *scene_path);
static void process_pending_scene_load(void);
static void apply_resolved_scene_lighting(const LightingSceneSelection *selection);
static const toml_datum_t *find_animated_sprite_component_table(toml_datum_t actor_table, toml_datum_t *out_animated_sprite_table);
static result animated_sprite_component_initialize(Actor *actor, ActorComponent *component, void *context);
static result collider_component_initialize(Actor *actor, ActorComponent *component, void *context);
static result rigidbody_component_initialize(Actor *actor, ActorComponent *component, void *context);
static result point_light_component_initialize(Actor *actor, ActorComponent *component, void *context);
static result spot_light_component_initialize(Actor *actor, ActorComponent *component, void *context);
static result direction_light_component_initialize(Actor *actor, ActorComponent *component, void *context);
static result texture_cache_acquire(const char *resolved_texture_path, Texture2D *out_texture);
static void texture_cache_release(const char *resolved_texture_path, Texture2D texture);
static void texture_cache_reset(void);
static void shader_cache_reset(void);
static result shader_cache_acquire_by_id(const char *shader_id, SharedShaderEntry **out_entry);
static result postfx_load_stack_config(const ShaderGlobalConfig *global_config, const ShaderLibrary *shader_library);
static void postfx_reset(void);
static result postfx_ensure_targets(int width, int height);
static void draw_texture_fullscreen(Texture2D texture, int width, int height);
static result shader_cache_acquire_for_material(const char *material_id, Shader **out_shader, int *out_time_loc, int *out_speed_loc, int *out_freq_loc, int *out_strength_loc);
static int find_postfx_pass_index_by_alias(const char *material_alias);
static boolean material_alias_exists_internal(const char *material_alias);
static int find_material_override_index(const char *material_alias, const char *property_name);
static boolean material_override_get_float(const char *material_alias, const char *property_name, float *out_value);
static void material_override_reset(void);
static float normalize_material_property_value(const char *property_name, float value);
static boolean postfx_pass_get_default_property(const PostFxPassConfig *pass, const char *property_name, float *out_value);
static boolean toml_number_to_float(toml_datum_t value, float *out_number);
static unsigned char scale_color_channel(unsigned char channel, float multiplier);
static Color apply_global_light_to_color(Color color);
static void animated_sprite_component_update(AnimatedSpriteState *state, float delta_time);
static result static_color_component_initialize(Actor *actor, ActorComponent *component, void *context);
static void static_color_component_draw(StaticColorState *state, CameraComponentData *active_camera, Actor *active_camera_actor);
static void animated_sprite_component_draw(AnimatedSpriteState *state, CameraComponentData *active_camera, Actor *active_camera_actor);
static void animated_sprite_component_dispose(AnimatedSpriteState *state);
static void static_sprite_component_dispose(StaticSpriteState *state);
static void static_color_component_dispose(StaticColorState *state);
static void camera_component_dispose(CameraComponentData *state);
static void collider_component_dispose(ColliderComponentData *state);
static void rigidbody_component_dispose(RigidbodyComponentData *state);
static void point_light_component_dispose(PointLightComponentData *state);
static void spot_light_component_dispose(SpotLightComponentData *state);
static void direction_light_component_dispose(DirectionLightComponentData *state);
static void rigidbody_component_integrate(Actor *actor, RigidbodyComponentData *state, float delta_time);
static void rigidbody_component_resolve_collisions(void);
static void draw_point_light_overlays(CameraComponentData *active_camera, Actor *active_camera_actor);

static const float RuntimePhysicsGravityX = 0.0f;
static const float RuntimePhysicsGravityY = 240.0f;

enum {
    RuntimeMaxActorScriptComponents = 16,
    RuntimeMaxPostFxPasses = 4,
    RuntimeMaxMaterialOverrides = 256,
};

enum {
    RuntimeMaxMaterialPropertyLength = 64,
};

typedef struct {
    char resolved_texture_path[PATH_MAX];
    Texture2D texture;
    usize ref_count;
} SharedTextureEntry;

typedef struct SharedShaderEntry {
    char shader_id[ShaderRegistryMaxIdLength];
    Shader shader;
    int time_location;
    int screen_size_location;
    int pixel_size_location;
    int vignette_inner_location;
    int vignette_outer_location;
    int edge_glow_location;
    int pulse_speed_location;
    int intensity_location;
    int distort_speed_location;
    int distort_frequency_location;
    int distort_strength_location;
    boolean loaded;
    boolean attempted_load;
} SharedShaderEntry;

struct PostFxPassConfig {
    char shader_id[ShaderRegistryMaxIdLength];
    char material_alias[ShaderRegistryMaxIdLength];
    float pixel_size;
    float vignette_inner;
    float vignette_outer;
    float edge_glow;
    float pulse_speed;
    float intensity;
};

typedef struct {
    boolean enabled;
    usize pass_count;
    char source_path[PATH_MAX];
    PostFxPassConfig passes[RuntimeMaxPostFxPasses];
} PostFxStackConfig;

static Heap shared_texture_entries_heap = {0};
static SharedTextureEntry *shared_texture_entries = 0;
static usize shared_texture_entry_count = 0;
static usize shared_texture_entry_capacity = 0;

static Heap shared_shader_entries_heap = {0};
static SharedShaderEntry *shared_shader_entries = 0;
static usize shared_shader_entry_count = 0;
static usize shared_shader_entry_capacity = 0;

static PostFxStackConfig runtime_postfx_stack = {0};
static RenderTexture2D runtime_postfx_scene_target = {0};
static RenderTexture2D runtime_postfx_ping_target = {0};
static boolean runtime_postfx_targets_loaded = False;

typedef struct {
    char material_alias[ShaderRegistryMaxIdLength];
    char property_name[RuntimeMaxMaterialPropertyLength];
    float value;
} MaterialPropertyOverride;

static MaterialPropertyOverride runtime_material_overrides[RuntimeMaxMaterialOverrides] = {0};
static usize runtime_material_override_count = 0;

static result ensure_shared_texture_capacity(usize required_capacity) {
    if (required_capacity <= shared_texture_entry_capacity)
        return Ok;

    usize next_capacity = shared_texture_entry_capacity == 0 ? 8 : shared_texture_entry_capacity * 2;
    while (next_capacity < required_capacity)
        next_capacity *= 2;

    const usize next_size = next_capacity * sizeof(SharedTextureEntry);
    if (!shared_texture_entries) {
        shared_texture_entries_heap = allocate(next_capacity, sizeof(SharedTextureEntry));
        shared_texture_entries = (SharedTextureEntry *)shared_texture_entries_heap.pointer;
        if (!shared_texture_entries)
            return Err;
    } else {
        shared_texture_entries_heap = reallocate(shared_texture_entries_heap, next_size);
        shared_texture_entries = (SharedTextureEntry *)shared_texture_entries_heap.pointer;
        if (!shared_texture_entries)
            return Err;
    }

    if (next_capacity > shared_texture_entry_capacity) {
        memset(shared_texture_entries + shared_texture_entry_capacity, 0, (next_capacity - shared_texture_entry_capacity) * sizeof(SharedTextureEntry));
    }

    shared_texture_entry_capacity = next_capacity;
    return Ok;
}

static int find_shared_texture_index(const char *resolved_texture_path) {
    if (!resolved_texture_path || resolved_texture_path[0] == '\0')
        return -1;

    for (usize index = 0; index < shared_texture_entry_count; ++index) {
        if (strcmp(shared_texture_entries[index].resolved_texture_path, resolved_texture_path) == 0)
            return (int)index;
    }

    return -1;
}

static result texture_cache_acquire(const char *resolved_texture_path, Texture2D *out_texture) {
    if (!resolved_texture_path || resolved_texture_path[0] == '\0' || !out_texture)
        return Err;

    const int cached_index = find_shared_texture_index(resolved_texture_path);
    if (cached_index >= 0) {
        SharedTextureEntry *entry = &shared_texture_entries[cached_index];
        entry->ref_count++;
        *out_texture = entry->texture;
        return Ok;
    }

    Texture2D loaded_texture = {0};
    if (vfs_load_texture(resolved_texture_path, &loaded_texture) != Ok)
        return Err;

    if (ensure_shared_texture_capacity(shared_texture_entry_count + 1) != Ok) {
        UnloadTexture(loaded_texture);
        return Err;
    }

    SharedTextureEntry *entry = &shared_texture_entries[shared_texture_entry_count++];
    memset(entry, 0, sizeof(*entry));
    if (snprintf(entry->resolved_texture_path, sizeof(entry->resolved_texture_path), "%s", resolved_texture_path) >= (int)sizeof(entry->resolved_texture_path)) {
        UnloadTexture(loaded_texture);
        shared_texture_entry_count--;
        return Err;
    }

    entry->texture = loaded_texture;
    entry->ref_count = 1;

    *out_texture = loaded_texture;
    return Ok;
}

static void texture_cache_release(const char *resolved_texture_path, Texture2D texture) {
    if (!resolved_texture_path || resolved_texture_path[0] == '\0') {
        if (texture.id != 0)
            UnloadTexture(texture);
        return;
    }

    const int cached_index = find_shared_texture_index(resolved_texture_path);
    if (cached_index < 0) {
        if (texture.id != 0)
            UnloadTexture(texture);
        return;
    }

    SharedTextureEntry *entry = &shared_texture_entries[cached_index];
    if (entry->ref_count > 1) {
        entry->ref_count--;
        return;
    }

    UnloadTexture(entry->texture);

    const usize last_index = shared_texture_entry_count - 1;
    if ((usize)cached_index < last_index)
        shared_texture_entries[cached_index] = shared_texture_entries[last_index];

    memset(&shared_texture_entries[last_index], 0, sizeof(SharedTextureEntry));
    shared_texture_entry_count--;
}

static void texture_cache_reset(void) {
    for (usize index = 0; index < shared_texture_entry_count; ++index) {
        if (shared_texture_entries[index].texture.id != 0)
            UnloadTexture(shared_texture_entries[index].texture);
    }

    if (shared_texture_entries_heap.pointer)
        deallocate(shared_texture_entries_heap);

    shared_texture_entries_heap = NullHeap;
    shared_texture_entries = Null;
    shared_texture_entry_count = 0;
    shared_texture_entry_capacity = 0;
}

static result ensure_shared_shader_capacity(usize required_capacity) {
    if (required_capacity <= shared_shader_entry_capacity)
        return Ok;

    usize next_capacity = shared_shader_entry_capacity == 0 ? 8 : shared_shader_entry_capacity * 2;
    while (next_capacity < required_capacity)
        next_capacity *= 2;

    const usize next_size = next_capacity * sizeof(SharedShaderEntry);
    if (!shared_shader_entries) {
        shared_shader_entries_heap = allocate(next_capacity, sizeof(SharedShaderEntry));
        shared_shader_entries = (SharedShaderEntry *)shared_shader_entries_heap.pointer;
        if (!shared_shader_entries)
            return Err;
    } else {
        shared_shader_entries_heap = reallocate(shared_shader_entries_heap, next_size);
        shared_shader_entries = (SharedShaderEntry *)shared_shader_entries_heap.pointer;
        if (!shared_shader_entries)
            return Err;
    }

    if (next_capacity > shared_shader_entry_capacity) {
        memset(shared_shader_entries + shared_shader_entry_capacity, 0, (next_capacity - shared_shader_entry_capacity) * sizeof(SharedShaderEntry));
    }

    shared_shader_entry_capacity = next_capacity;
    return Ok;
}

static int find_shared_shader_index(const char *shader_id) {
    if (!shader_id || shader_id[0] == '\0')
        return -1;

    for (usize index = 0; index < shared_shader_entry_count; ++index) {
        if (strcmp(shared_shader_entries[index].shader_id, shader_id) == 0)
            return (int)index;
    }

    return -1;
}

static const ShaderDescriptor *find_shader_descriptor_by_id(const char *shader_id) {
    if (!shader_id || shader_id[0] == '\0')
        return Null;

    for (usize index = 0; index < runtime_state.shader_library.shader_count; ++index) {
        const ShaderDescriptor *descriptor = &runtime_state.shader_library.shaders[index];
        if (strcmp(descriptor->id, shader_id) == 0)
            return descriptor;
    }

    return Null;
}

static const ShaderDescriptor *find_shader_descriptor_by_id_in_library(const ShaderLibrary *library, const char *shader_id) {
    if (!library || !shader_id || shader_id[0] == '\0')
        return Null;

    for (usize index = 0; index < library->shader_count; ++index) {
        const ShaderDescriptor *descriptor = &library->shaders[index];
        if (strcmp(descriptor->id, shader_id) == 0)
            return descriptor;
    }

    return Null;
}

static result shader_cache_acquire_for_material(const char *material_id, Shader **out_shader, int *out_time_loc, int *out_speed_loc, int *out_freq_loc, int *out_strength_loc) {
    if (!material_id || material_id[0] == '\0' || !out_shader)
        return Err;

    *out_shader = Null;
    if (out_time_loc)
        *out_time_loc = -1;
    if (out_speed_loc)
        *out_speed_loc = -1;
    if (out_freq_loc)
        *out_freq_loc = -1;
    if (out_strength_loc)
        *out_strength_loc = -1;

    const MaterialDescriptor *material = material_library_find_by_id(&runtime_state.material_library, material_id);
    if (!material)
        return Err;

    const char *shader_id = material->shader_id;
    if (!shader_id || shader_id[0] == '\0')
        return Err;

    SharedShaderEntry *entry = Null;
    if (shader_cache_acquire_by_id(shader_id, &entry) != Ok || !entry)
        return Err;

    *out_shader = &entry->shader;
    if (out_time_loc)
        *out_time_loc = entry->time_location;
    if (out_speed_loc)
        *out_speed_loc = entry->distort_speed_location;
    if (out_freq_loc)
        *out_freq_loc = entry->distort_frequency_location;
    if (out_strength_loc)
        *out_strength_loc = entry->distort_strength_location;

    return Ok;
}

static result shader_cache_acquire_by_id(const char *shader_id, SharedShaderEntry **out_entry) {
    if (!shader_id || shader_id[0] == '\0' || !out_entry)
        return Err;

    *out_entry = Null;

    const int cached_index = find_shared_shader_index(shader_id);
    if (cached_index >= 0) {
        SharedShaderEntry *entry = &shared_shader_entries[cached_index];
        if (!entry->loaded)
            return Err;
        *out_entry = entry;
        return Ok;
    }

    if (ensure_shared_shader_capacity(shared_shader_entry_count + 1) != Ok)
        return Err;

    SharedShaderEntry *entry = &shared_shader_entries[shared_shader_entry_count++];
    memset(entry, 0, sizeof(*entry));

    if (snprintf(entry->shader_id, sizeof(entry->shader_id), "%s", shader_id) >= (int)sizeof(entry->shader_id))
        return Err;

    if (vfs_is_archive_mode()) {
        log_warn("Shader loading from archive mode is not supported yet for shader '%s'", shader_id);
        return Err;
    }

    const ShaderDescriptor *shader_descriptor = find_shader_descriptor_by_id(shader_id);
    if (!shader_descriptor)
        return Err;

    char vertex_path[PATH_MAX] = {0};
    char fragment_path[PATH_MAX] = {0};
    if (join_path(runtime_state.shader_global_config.shader_root, shader_descriptor->vertex, vertex_path, sizeof(vertex_path)) != Ok ||
        join_path(runtime_state.shader_global_config.shader_root, shader_descriptor->fragment, fragment_path, sizeof(fragment_path)) != Ok) {
        log_err("Failed to resolve shader paths for '%s'", shader_id);
        return Err;
    }

    entry->attempted_load = True;
    entry->shader = LoadShader(vertex_path, fragment_path);
    if (entry->shader.id == 0) {
        log_err("Failed to load shader '%s' (%s, %s)", shader_id, vertex_path, fragment_path);
        return Err;
    }

    entry->time_location = GetShaderLocation(entry->shader, "u_time");
    entry->screen_size_location = GetShaderLocation(entry->shader, "u_screen_size");
    entry->pixel_size_location = GetShaderLocation(entry->shader, "u_pixel_size");
    entry->vignette_inner_location = GetShaderLocation(entry->shader, "u_vignette_inner");
    entry->vignette_outer_location = GetShaderLocation(entry->shader, "u_vignette_outer");
    entry->edge_glow_location = GetShaderLocation(entry->shader, "u_edge_glow");
    entry->pulse_speed_location = GetShaderLocation(entry->shader, "u_pulse_speed");
    entry->intensity_location = GetShaderLocation(entry->shader, "u_intensity");
    entry->distort_speed_location = GetShaderLocation(entry->shader, "u_distort_speed");
    entry->distort_frequency_location = GetShaderLocation(entry->shader, "u_distort_frequency");
    entry->distort_strength_location = GetShaderLocation(entry->shader, "u_distort_strength");
    entry->loaded = True;

    *out_entry = entry;
    return Ok;
}

static int find_postfx_pass_index_by_alias(const char *material_alias) {
    if (!material_alias || material_alias[0] == '\0')
        return -1;

    for (usize index = 0; index < runtime_postfx_stack.pass_count; ++index) {
        const PostFxPassConfig *pass = &runtime_postfx_stack.passes[index];
        if (strcmp(pass->material_alias, material_alias) == 0)
            return (int)index;

        if (strcmp(pass->shader_id, material_alias) == 0)
            return (int)index;
    }

    return -1;
}

static boolean material_alias_exists_internal(const char *material_alias) {
    if (!material_alias || material_alias[0] == '\0')
        return False;

    if (material_library_find_by_id(&runtime_state.material_library, material_alias))
        return True;

    return find_postfx_pass_index_by_alias(material_alias) >= 0 ? True : False;
}

static int find_material_override_index(const char *material_alias, const char *property_name) {
    if (!material_alias || material_alias[0] == '\0' || !property_name || property_name[0] == '\0')
        return -1;

    for (usize index = 0; index < runtime_material_override_count; ++index) {
        const MaterialPropertyOverride *entry = &runtime_material_overrides[index];
        if (strcmp(entry->material_alias, material_alias) == 0 && strcmp(entry->property_name, property_name) == 0)
            return (int)index;
    }

    return -1;
}

static boolean material_override_get_float(const char *material_alias, const char *property_name, float *out_value) {
    if (!out_value)
        return False;

    const int index = find_material_override_index(material_alias, property_name);
    if (index < 0)
        return False;

    *out_value = runtime_material_overrides[index].value;
    return True;
}

static void material_override_reset(void) {
    memset(runtime_material_overrides, 0, sizeof(runtime_material_overrides));
    runtime_material_override_count = 0;
}

static float normalize_material_property_value(const char *property_name, float value) {
    if (!property_name)
        return value;

    if (strcmp(property_name, "intensity") != 0)
        return value;

    if (value >= 0.0f && value <= 1.0f)
        return value;

    // Convenience mapping for health-like values: 100 HP -> 0 intensity, <=15 HP -> max intensity.
    float health = value;
    if (health < 0.0f)
        health = 0.0f;
    if (health > 100.0f)
        health = 100.0f;

    if (health >= 100.0f)
        return 0.0f;

    if (health <= 15.0f)
        return 1.0f;

    return (100.0f - health) / 85.0f;
}

static boolean postfx_pass_get_default_property(const PostFxPassConfig *pass, const char *property_name, float *out_value) {
    if (!pass || !property_name || property_name[0] == '\0' || !out_value)
        return False;

    if (strcmp(property_name, "pixel_size") == 0) {
        *out_value = pass->pixel_size;
        return True;
    }

    if (strcmp(property_name, "vignette_inner") == 0) {
        *out_value = pass->vignette_inner;
        return True;
    }

    if (strcmp(property_name, "vignette_outer") == 0) {
        *out_value = pass->vignette_outer;
        return True;
    }

    if (strcmp(property_name, "edge_glow") == 0) {
        *out_value = pass->edge_glow;
        return True;
    }

    if (strcmp(property_name, "pulse_speed") == 0) {
        *out_value = pass->pulse_speed;
        return True;
    }

    if (strcmp(property_name, "intensity") == 0) {
        *out_value = pass->intensity;
        return True;
    }

    return False;
}

static result postfx_load_stack_config(const ShaderGlobalConfig *global_config, const ShaderLibrary *shader_library) {
    if (!global_config || !shader_library)
        return Err;

    (void)shader_library;

    memset(&runtime_postfx_stack, 0, sizeof(runtime_postfx_stack));

    if (global_config->default_post_stack[0] == '\0')
        return Ok;

    char postfx_path[PATH_MAX] = {0};
    if (join_path(global_config->material_root, global_config->default_post_stack, postfx_path, sizeof(postfx_path)) != Ok) {
        log_err("ShaderGlobal.Defaults.post_stack path is too long: '%s'", global_config->default_post_stack);
        return Err;
    }

    if (vfs_file_exists(postfx_path) != True) {
        log_msg("PostFX stack '%s' not found; fullscreen shaders disabled", postfx_path);
        return Ok;
    }

    toml_result_t parsed = {0};
    if (vfs_parse_toml_file(postfx_path, &parsed) != Ok) {
        log_err("Failed to parse postfx stack '%s'", postfx_path);
        return Err;
    }

    toml_datum_t postfx = toml_get(parsed.toptab, "PostFX");
    if (postfx.type != TOML_TABLE) {
        log_err("PostFX stack '%s' is missing [PostFX] table", postfx_path);
        toml_free(parsed);
        return Err;
    }

    if (snprintf(runtime_postfx_stack.source_path, sizeof(runtime_postfx_stack.source_path), "%s", postfx_path) >= (int)sizeof(runtime_postfx_stack.source_path)) {
        toml_free(parsed);
        return Err;
    }

    runtime_postfx_stack.enabled = True;
    toml_datum_t enabled = toml_get(postfx, "enabled");
    if (enabled.type == TOML_BOOLEAN)
        runtime_postfx_stack.enabled = enabled.u.boolean ? True : False;

    if (!runtime_postfx_stack.enabled) {
        toml_free(parsed);
        return Ok;
    }

    toml_datum_t passes = toml_get(postfx, "Passes");
    if (passes.type != TOML_ARRAY || passes.u.arr.size <= 0) {
        log_err("PostFX stack '%s' requires [[PostFX.Passes]] entries", postfx_path);
        toml_free(parsed);
        return Err;
    }

    for (int index = 0; index < passes.u.arr.size; ++index) {
        if (runtime_postfx_stack.pass_count >= RuntimeMaxPostFxPasses) {
            log_err("PostFX stack '%s' exceeds max passes (%d)", postfx_path, RuntimeMaxPostFxPasses);
            toml_free(parsed);
            return Err;
        }

        toml_datum_t pass = passes.u.arr.elem[index];
        if (pass.type != TOML_TABLE) {
            log_err("PostFX.Passes[%d] must be a table in '%s'", index, postfx_path);
            toml_free(parsed);
            return Err;
        }

        toml_datum_t shader = toml_get(pass, "shader");
        if (shader.type != TOML_STRING || !shader.u.s || shader.u.s[0] == '\0') {
            log_err("PostFX.Passes[%d].shader is required in '%s'", index, postfx_path);
            toml_free(parsed);
            return Err;
        }

        if (!find_shader_descriptor_by_id_in_library(shader_library, shader.u.s)) {
            log_err("PostFX pass references unknown shader '%s' in '%s'", shader.u.s, postfx_path);
            toml_free(parsed);
            return Err;
        }

        PostFxPassConfig *out_pass = &runtime_postfx_stack.passes[runtime_postfx_stack.pass_count];
        memset(out_pass, 0, sizeof(*out_pass));
        if (snprintf(out_pass->shader_id, sizeof(out_pass->shader_id), "%s", shader.u.s) >= (int)sizeof(out_pass->shader_id)) {
            toml_free(parsed);
            return Err;
        }
        if (snprintf(out_pass->material_alias, sizeof(out_pass->material_alias), "%s", shader.u.s) >= (int)sizeof(out_pass->material_alias)) {
            toml_free(parsed);
            return Err;
        }

        toml_datum_t material_alias = toml_get(pass, "material_alias");
        if (material_alias.type != TOML_STRING || !material_alias.u.s || material_alias.u.s[0] == '\0')
            material_alias = toml_get(pass, "alias");
        if (material_alias.type != TOML_STRING || !material_alias.u.s || material_alias.u.s[0] == '\0')
            material_alias = toml_get(pass, "material");
        if (material_alias.type == TOML_STRING && material_alias.u.s && material_alias.u.s[0] != '\0') {
            if (snprintf(out_pass->material_alias, sizeof(out_pass->material_alias), "%s", material_alias.u.s) >= (int)sizeof(out_pass->material_alias)) {
                toml_free(parsed);
                return Err;
            }
        }

        out_pass->pixel_size = 4.0f;
        out_pass->vignette_inner = 0.55f;
        out_pass->vignette_outer = 0.98f;
        out_pass->edge_glow = 0.35f;
        out_pass->pulse_speed = 1.3f;
        out_pass->intensity = 1.0f;

        float numeric_value = 0.0f;
        toml_datum_t pixel_size = toml_get(pass, "pixel_size");
        if (toml_number_to_float(pixel_size, &numeric_value) && numeric_value >= 1.0f)
            out_pass->pixel_size = numeric_value;

        toml_datum_t vignette_inner = toml_get(pass, "vignette_inner");
        if (toml_number_to_float(vignette_inner, &numeric_value) && numeric_value >= 0.0f)
            out_pass->vignette_inner = numeric_value;

        toml_datum_t vignette_outer = toml_get(pass, "vignette_outer");
        if (toml_number_to_float(vignette_outer, &numeric_value) && numeric_value > 0.0f)
            out_pass->vignette_outer = numeric_value;

        toml_datum_t edge_glow = toml_get(pass, "edge_glow");
        if (toml_number_to_float(edge_glow, &numeric_value) && numeric_value >= 0.0f)
            out_pass->edge_glow = numeric_value;

        toml_datum_t pulse_speed = toml_get(pass, "pulse_speed");
        if (toml_number_to_float(pulse_speed, &numeric_value) && numeric_value >= 0.0f)
            out_pass->pulse_speed = numeric_value;

        toml_datum_t intensity = toml_get(pass, "intensity");
        if (toml_number_to_float(intensity, &numeric_value) && numeric_value >= 0.0f)
            out_pass->intensity = numeric_value;

        runtime_postfx_stack.pass_count++;
    }

    toml_free(parsed);
    log_msg("Loaded PostFX stack '%s' with %lu pass(es)", postfx_path, runtime_postfx_stack.pass_count);
    return Ok;
}

static void postfx_reset(void) {
    if (runtime_postfx_targets_loaded) {
        if (runtime_postfx_scene_target.id != 0)
            UnloadRenderTexture(runtime_postfx_scene_target);
        if (runtime_postfx_ping_target.id != 0)
            UnloadRenderTexture(runtime_postfx_ping_target);
    }

    runtime_postfx_scene_target = (RenderTexture2D){0};
    runtime_postfx_ping_target = (RenderTexture2D){0};
    runtime_postfx_targets_loaded = False;
    memset(&runtime_postfx_stack, 0, sizeof(runtime_postfx_stack));
    material_override_reset();
}

static result postfx_ensure_targets(int width, int height) {
    if (width <= 0 || height <= 0)
        return Err;

    if (runtime_postfx_targets_loaded &&
        runtime_postfx_scene_target.texture.id != 0 &&
        runtime_postfx_scene_target.texture.width == width &&
        runtime_postfx_scene_target.texture.height == height &&
        runtime_postfx_ping_target.texture.id != 0 &&
        runtime_postfx_ping_target.texture.width == width &&
        runtime_postfx_ping_target.texture.height == height) {
        return Ok;
    }

    if (runtime_postfx_targets_loaded) {
        if (runtime_postfx_scene_target.id != 0)
            UnloadRenderTexture(runtime_postfx_scene_target);
        if (runtime_postfx_ping_target.id != 0)
            UnloadRenderTexture(runtime_postfx_ping_target);
    }

    runtime_postfx_scene_target = LoadRenderTexture(width, height);
    runtime_postfx_ping_target = LoadRenderTexture(width, height);
    if (runtime_postfx_scene_target.id == 0 || runtime_postfx_ping_target.id == 0) {
        log_err("Failed to allocate postfx render targets %dx%d", width, height);
        runtime_postfx_scene_target = (RenderTexture2D){0};
        runtime_postfx_ping_target = (RenderTexture2D){0};
        runtime_postfx_targets_loaded = False;
        return Err;
    }

    runtime_postfx_targets_loaded = True;
    return Ok;
}

static void draw_texture_fullscreen(Texture2D texture, int width, int height) {
    Rectangle source = {
        .x = 0.0f,
        .y = 0.0f,
        .width = (float)texture.width,
        .height = -(float)texture.height,
    };

    Rectangle destination = {
        .x = 0.0f,
        .y = 0.0f,
        .width = (float)width,
        .height = (float)height,
    };

    DrawTexturePro(texture, source, destination, (Vector2){0.0f, 0.0f}, 0.0f, WHITE);
}

static void shader_cache_reset(void) {
    for (usize index = 0; index < shared_shader_entry_count; ++index) {
        if (shared_shader_entries[index].loaded && shared_shader_entries[index].shader.id != 0)
            UnloadShader(shared_shader_entries[index].shader);
    }

    if (shared_shader_entries_heap.pointer)
        deallocate(shared_shader_entries_heap);

    shared_shader_entries_heap = NullHeap;
    shared_shader_entries = Null;
    shared_shader_entry_count = 0;
    shared_shader_entry_capacity = 0;
}

typedef struct {
    const char *module_path;
    const char *module_alias;
} ScriptModuleBinding;

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

static unsigned char scale_color_channel(unsigned char channel, float multiplier) {
    if (multiplier <= 0.0f)
        return 0;

    float scaled = (float)channel * multiplier;
    if (scaled < 0.0f)
        scaled = 0.0f;
    if (scaled > 255.0f)
        scaled = 255.0f;

    return (unsigned char)scaled;
}

static Color apply_global_light_to_color(Color color) {
    const float multiplier = runtime_state.scene_light_multiplier;
    if (multiplier == 1.0f)
        return color;

    return (Color){
        scale_color_channel(color.r, multiplier),
        scale_color_channel(color.g, multiplier),
        scale_color_channel(color.b, multiplier),
        color.a,
    };
}

Actor *find_actor_by_id(const char *actor_id) {
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

static int find_actor_index_by_id(const char *actor_id) {
    if (!actor_id || actor_id[0] == '\0')
        return -1;

    for (usize index = 0; index < runtime_state.actor_registry.actor_count; ++index) {
        Actor *actor = &runtime_state.actor_registry.actors[index];
        if (!actor->id)
            continue;

        if (strcmp(actor->id, actor_id) == 0)
            return (int)index;
    }

    return -1;
}

static result validate_actor_parent_links(void) {
    const usize actor_count = runtime_state.actor_registry.actor_count;
    if (actor_count == 0)
        return Ok;

    Heap state_heap = allocate(actor_count, sizeof(unsigned char));
    unsigned char *state = (unsigned char *)state_heap.pointer;
    if (!state)
        return Err;

    memset(state, 0, actor_count * sizeof(unsigned char));

    for (usize index = 0; index < actor_count; ++index) {
        Actor *actor = &runtime_state.actor_registry.actors[index];
        if (!actor->has_parent)
            continue;

        if (!actor->parent_id[0]) {
            log_err("Actor '%s' has empty parent id", actor->id ? actor->id : "<unknown>");
            deallocate(state_heap);
            return Err;
        }

        if (actor->id && strcmp(actor->id, actor->parent_id) == 0) {
            log_err("Actor '%s' cannot be its own parent", actor->id);
            deallocate(state_heap);
            return Err;
        }

        if (find_actor_index_by_id(actor->parent_id) < 0) {
            log_err("Actor '%s' references missing parent '%s'", actor->id ? actor->id : "<unknown>", actor->parent_id);
            deallocate(state_heap);
            return Err;
        }
    }

    for (usize start_index = 0; start_index < actor_count; ++start_index) {
        if (state[start_index] == 2)
            continue;

        int cursor = (int)start_index;
        while (cursor >= 0) {
            if (state[cursor] == 1) {
                Actor *cycle_actor = &runtime_state.actor_registry.actors[cursor];
                log_err("Actor hierarchy cycle detected at '%s'", cycle_actor->id ? cycle_actor->id : "<unknown>");
                deallocate(state_heap);
                return Err;
            }

            if (state[cursor] == 2)
                break;

            state[cursor] = 1;
            Actor *actor = &runtime_state.actor_registry.actors[cursor];
            if (!actor->has_parent)
                break;

            cursor = find_actor_index_by_id(actor->parent_id);
        }

        cursor = (int)start_index;
        while (cursor >= 0 && state[cursor] == 1) {
            state[cursor] = 2;
            Actor *actor = &runtime_state.actor_registry.actors[cursor];
            if (!actor->has_parent)
                break;

            cursor = find_actor_index_by_id(actor->parent_id);
        }
    }

    deallocate(state_heap);
    return Ok;
}

static result apply_parent_transform_delta_recursive(int actor_index, unsigned char *visiting, unsigned char *applied, usize actor_count) {
    if (actor_index < 0 || (usize)actor_index >= actor_count)
        return Err;

    if (applied[actor_index])
        return Ok;

    if (visiting[actor_index])
        return Err;

    visiting[actor_index] = 1;
    Actor *actor = &runtime_state.actor_registry.actors[actor_index];
    if (actor->has_parent && actor->parent_id[0] != '\0') {
        const int parent_index = find_actor_index_by_id(actor->parent_id);
        if (parent_index >= 0) {
            if (apply_parent_transform_delta_recursive(parent_index, visiting, applied, actor_count) != Ok) {
                visiting[actor_index] = 0;
                return Err;
            }

            Actor *parent = &runtime_state.actor_registry.actors[parent_index];
            const ActorVector3 parent_delta_position = {
                parent->transform.position.x - parent->previous_transform.position.x,
                parent->transform.position.y - parent->previous_transform.position.y,
                parent->transform.position.z - parent->previous_transform.position.z,
            };
            const ActorVector3 parent_delta_rotation = {
                parent->transform.rotation_euler.x - parent->previous_transform.rotation_euler.x,
                parent->transform.rotation_euler.y - parent->previous_transform.rotation_euler.y,
                parent->transform.rotation_euler.z - parent->previous_transform.rotation_euler.z,
            };

            actor->transform.position.x += parent_delta_position.x;
            actor->transform.position.y += parent_delta_position.y;
            actor->transform.position.z += parent_delta_position.z;

            actor->transform.rotation_euler.x += parent_delta_rotation.x;
            actor->transform.rotation_euler.y += parent_delta_rotation.y;
            actor->transform.rotation_euler.z += parent_delta_rotation.z;

            if (fabsf(parent->previous_transform.scale.x) > 0.0001f)
                actor->transform.scale.x *= parent->transform.scale.x / parent->previous_transform.scale.x;
            if (fabsf(parent->previous_transform.scale.y) > 0.0001f)
                actor->transform.scale.y *= parent->transform.scale.y / parent->previous_transform.scale.y;
            if (fabsf(parent->previous_transform.scale.z) > 0.0001f)
                actor->transform.scale.z *= parent->transform.scale.z / parent->previous_transform.scale.z;
        }
    }

    visiting[actor_index] = 0;
    applied[actor_index] = 1;
    return Ok;
}

static void apply_parent_transform_deltas(void) {
    const usize actor_count = runtime_state.actor_registry.actor_count;
    if (actor_count == 0)
        return;

    Heap visiting_heap = allocate(actor_count, sizeof(unsigned char));
    Heap applied_heap = allocate(actor_count, sizeof(unsigned char));
    unsigned char *visiting = (unsigned char *)visiting_heap.pointer;
    unsigned char *applied = (unsigned char *)applied_heap.pointer;
    if (!visiting || !applied) {
        if (visiting_heap.pointer)
            deallocate(visiting_heap);
        if (applied_heap.pointer)
            deallocate(applied_heap);
        return;
    }

    memset(visiting, 0, actor_count * sizeof(unsigned char));
    memset(applied, 0, actor_count * sizeof(unsigned char));

    for (usize index = 0; index < actor_count; ++index)
        (void)apply_parent_transform_delta_recursive((int)index, visiting, applied, actor_count);

    deallocate(visiting_heap);
    deallocate(applied_heap);
}

static void sync_parented_actor_positions(void) {
    for (usize actor_index = 0; actor_index < runtime_state.actor_registry.actor_count; ++actor_index) {
        Actor *actor = &runtime_state.actor_registry.actors[actor_index];
        if (!actor->has_parent || actor->parent_id[0] == '\0') {
            actor->parent_anchor_initialized = False;
            continue;
        }

        const int parent_index = find_actor_index_by_id(actor->parent_id);
        if (parent_index < 0)
            continue;

        Actor *parent = &runtime_state.actor_registry.actors[parent_index];
        if (!actor->parent_anchor_initialized) {
            actor->parent_local_anchor = (ActorVector3){
                actor->transform.position.x - parent->transform.position.x,
                actor->transform.position.y - parent->transform.position.y,
                actor->transform.position.z - parent->transform.position.z,
            };
            actor->parent_anchor_initialized = True;
        }

        actor->transform.position.x = parent->transform.position.x + actor->parent_local_anchor.x;
        actor->transform.position.y = parent->transform.position.y + actor->parent_local_anchor.y;
        actor->transform.position.z = parent->transform.position.z + actor->parent_local_anchor.z;
    }
}

static void capture_actor_previous_transforms(void) {
    for (usize index = 0; index < runtime_state.actor_registry.actor_count; ++index)
        actor_capture_previous_transform(&runtime_state.actor_registry.actors[index]);
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
                continue;
            }

            if (strcmp(component->descriptor.name, "StaticColor") == 0) {
                StaticColorState *state = (StaticColorState *)component->data;
                state->actor = actor;
                continue;
            }

            if (strcmp(component->descriptor.name, "PointLight") == 0) {
                PointLightComponentData *state = (PointLightComponentData *)component->data;
                state->actor = actor;
                continue;
            }

            if (strcmp(component->descriptor.name, "SpotLight") == 0) {
                SpotLightComponentData *state = (SpotLightComponentData *)component->data;
                state->actor = actor;
                continue;
            }

            if (strcmp(component->descriptor.name, "DirectionLight") == 0) {
                DirectionLightComponentData *state = (DirectionLightComponentData *)component->data;
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

        if (component->descriptor.kind == ComponentBuiltin && component->descriptor.name && strcmp(component->descriptor.name, "StaticColor") == 0)
            static_color_component_dispose((StaticColorState *)component->data);

        if (component->descriptor.kind == ComponentBuiltin && component->descriptor.name && strcmp(component->descriptor.name, "AnimatedSprite") == 0)
            animated_sprite_component_dispose((AnimatedSpriteState *)component->data);

        if (component->descriptor.kind == ComponentBuiltin && component->descriptor.name && strcmp(component->descriptor.name, "Camera") == 0)
            camera_component_dispose((CameraComponentData *)component->data);

        if (component->descriptor.kind == ComponentBuiltin && component->descriptor.name && strcmp(component->descriptor.name, "Collider") == 0)
            collider_component_dispose((ColliderComponentData *)component->data);

        if (component->descriptor.kind == ComponentBuiltin && component->descriptor.name && strcmp(component->descriptor.name, "Rigidbody") == 0)
            rigidbody_component_dispose((RigidbodyComponentData *)component->data);

        if (component->descriptor.kind == ComponentBuiltin && component->descriptor.name && strcmp(component->descriptor.name, "PointLight") == 0)
            point_light_component_dispose((PointLightComponentData *)component->data);

        if (component->descriptor.kind == ComponentBuiltin && component->descriptor.name && strcmp(component->descriptor.name, "SpotLight") == 0)
            spot_light_component_dispose((SpotLightComponentData *)component->data);

        if (component->descriptor.kind == ComponentBuiltin && component->descriptor.name && strcmp(component->descriptor.name, "DirectionLight") == 0)
            direction_light_component_dispose((DirectionLightComponentData *)component->data);
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
    char resolved_ref_path_storage[PATH_MAX] = {0};

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
                                if (mapped.type == TOML_STRING && mapped.u.s && mapped.u.s[0] != '\0') {
                                    if (snprintf(resolved_ref_path_storage, sizeof(resolved_ref_path_storage), "%s", mapped.u.s) >= (int)sizeof(resolved_ref_path_storage)) {
                                        toml_free(scene_data_toml);
                                        toml_free(scene_toml);
                                        return Err;
                                    }

                                    resolved_ref_path = resolved_ref_path_storage;
                                }
                            }
                            toml_free(scene_data_toml);
                        }
                    }
                }
            }
            toml_free(scene_toml);
        }
    }

    if (join_path(runtime_state.prefabs_root, resolved_ref_path, out_prefab_path, out_prefab_path_size) == Ok && vfs_file_exists(out_prefab_path) == True)
        return Ok;

    if (join_path(runtime_state.project_root, resolved_ref_path, out_prefab_path, out_prefab_path_size) != Ok)
        return Err;

    return vfs_file_exists(out_prefab_path) == True ? Ok : Err;
}

static boolean pending_prefab_actor_id_exists(const char *actor_id) {
    if (!actor_id || actor_id[0] == '\0')
        return False;

    for (usize index = 0; index < runtime_state.pending_prefab_instantiation_count; ++index) {
        PendingPrefabInstantiation *pending = &runtime_state.pending_prefab_instantiations[index];
        if (strcmp(pending->actor_id, actor_id) == 0)
            return True;
    }

    return False;
}

static boolean runtime_actor_id_exists(const char *actor_id) {
    if (!actor_id || actor_id[0] == '\0')
        return False;

    if (find_actor_by_id(actor_id))
        return True;

    return pending_prefab_actor_id_exists(actor_id);
}

static result generate_prefab_runtime_actor_id(const char *prefab_ref_id, char *out_actor_id, usize out_actor_id_size) {
    if (!prefab_ref_id || prefab_ref_id[0] == '\0' || !out_actor_id || out_actor_id_size == 0)
        return Err;

    char candidate[RuntimeMaxRuntimeActorIdLength] = {0};
    for (usize copy_index = 1; copy_index < 1000000; ++copy_index) {
        if (snprintf(candidate, sizeof(candidate), "%s(copy%lu)", prefab_ref_id, (unsigned long)copy_index) >= (int)sizeof(candidate))
            return Err;

        if (runtime_actor_id_exists(candidate))
            continue;

        if (snprintf(out_actor_id, out_actor_id_size, "%s", candidate) >= (int)out_actor_id_size)
            return Err;

        return Ok;
    }

    return Err;
}

result enqueue_prefab_instantiation(const char *prefab_ref_id, boolean has_position, float x, float y, float z, char *out_actor_id, usize out_actor_id_size) {
    if (!prefab_ref_id || prefab_ref_id[0] == '\0')
        return Err;

    if (runtime_state.pending_prefab_instantiation_count >= RuntimeMaxPendingPrefabInstantiations)
        return Err;

    char generated_id[RuntimeMaxRuntimeActorIdLength] = {0};
    if (generate_prefab_runtime_actor_id(prefab_ref_id, generated_id, sizeof(generated_id)) != Ok)
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

    if (vfs_file_exists(prefab_path) != True) {
        if (!runtime_state.project_root[0])
            return Err;

        if (join_path(runtime_state.project_root, prefab_path_ref.u.s, prefab_path_from_project_root, sizeof(prefab_path_from_project_root)) != Ok)
            return Err;

        if (vfs_file_exists(prefab_path_from_project_root) != True)
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

static const toml_datum_t *find_static_color_component_table(toml_datum_t actor_table, toml_datum_t *out_static_color_table) {
    if (out_static_color_table)
        *out_static_color_table = (toml_datum_t){0};

    if (actor_table.type != TOML_TABLE || !out_static_color_table)
        return Null;

    toml_datum_t overrides = toml_get(actor_table, "Overrides");
    if (overrides.type == TOML_TABLE) {
        toml_datum_t components = toml_get(overrides, "Components");
        if (components.type == TOML_TABLE) {
            toml_datum_t static_color = toml_get(components, "StaticColor");
            if (static_color.type == TOML_TABLE) {
                *out_static_color_table = static_color;
                return out_static_color_table;
            }
        }
    }

    toml_datum_t components = toml_get(actor_table, "Components");
    if (components.type == TOML_TABLE) {
        toml_datum_t static_color = toml_get(components, "StaticColor");
        if (static_color.type == TOML_TABLE) {
            *out_static_color_table = static_color;
            return out_static_color_table;
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

static const char *find_material_main_texture_ref(const char *material_id) {
    if (!material_id || material_id[0] == '\0')
        return Null;

    const MaterialDescriptor *material = material_library_find_by_id(&runtime_state.material_library, material_id);
    if (!material)
        return Null;

    return material_descriptor_find_texture_slot(material, "main");
}

static result static_sprite_component_initialize(Actor *actor, ActorComponent *component, void *context) {
    (void)actor;
    (void)context;

    if (!component || !component->data)
        return Err;

    StaticSpriteState *state = (StaticSpriteState *)component->data;
    if (!state->texture_path[0])
        return Err;

    const char *material_main_texture = find_material_main_texture_ref(state->material_id);
    if (material_main_texture && material_main_texture[0] != '\0') {
        if (snprintf(state->texture_path, sizeof(state->texture_path), "%s", material_main_texture) >= (int)sizeof(state->texture_path)) {
            log_err("Material main texture path is too long for actor '%s'", actor && actor->id ? actor->id : "<unknown>");
            return Err;
        }
    }

    if (join_path(runtime_state.project_root, state->texture_path, state->resolved_texture_path, sizeof(state->resolved_texture_path)) != Ok) {
        log_err("Failed to resolve static sprite texture path '%s'", state->texture_path);
        return Err;
    }

    state->attempted_load = False;
    state->loaded = False;
    state->material_checked = False;
    state->material_resolved = False;
    return Ok;
}

static result static_color_component_initialize(Actor *actor, ActorComponent *component, void *context) {
    (void)actor;
    (void)context;

    if (!component || !component->data)
        return Err;

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

static result collider_component_initialize(Actor *actor, ActorComponent *component, void *context) {
    (void)actor;
    (void)context;

    if (!component || !component->data)
        return Err;

    return Ok;
}

static result rigidbody_component_initialize(Actor *actor, ActorComponent *component, void *context) {
    (void)actor;
    (void)context;

    if (!component || !component->data)
        return Err;

    RigidbodyComponentData *state = (RigidbodyComponentData *)component->data;
    if (state->mass <= 0.0f)
        state->mass = 1.0f;

    state->inverse_mass = state->mass > 0.0f ? (1.0f / state->mass) : 0.0f;
    state->is_grounded = False;
    return Ok;
}

static result point_light_component_initialize(Actor *actor, ActorComponent *component, void *context) {
    (void)context;

    if (!actor || !component || !component->data)
        return Err;

    PointLightComponentData *state = (PointLightComponentData *)component->data;
    state->actor = actor;

    if (state->intensity < 0.0f)
        state->intensity = 0.0f;
    if (state->range <= 0.0f)
        state->range = 0.01f;

    return Ok;
}

static result spot_light_component_initialize(Actor *actor, ActorComponent *component, void *context) {
    (void)context;

    if (!actor || !component || !component->data)
        return Err;

    SpotLightComponentData *state = (SpotLightComponentData *)component->data;
    state->actor = actor;

    if (fabsf(state->direction.x) < 0.0001f && fabsf(state->direction.y) < 0.0001f && fabsf(state->direction.z) < 0.0001f)
        return Err;

    if (state->intensity < 0.0f)
        state->intensity = 0.0f;
    if (state->range <= 0.0f)
        state->range = 0.01f;
    if (state->angle <= 0.0f)
        state->angle = 35.0f;

    return Ok;
}

static result direction_light_component_initialize(Actor *actor, ActorComponent *component, void *context) {
    (void)context;

    if (!actor || !component || !component->data)
        return Err;

    DirectionLightComponentData *state = (DirectionLightComponentData *)component->data;
    state->actor = actor;

    if (fabsf(state->direction.x) < 0.0001f && fabsf(state->direction.y) < 0.0001f && fabsf(state->direction.z) < 0.0001f)
        return Err;

    if (state->intensity < 0.0f)
        state->intensity = 0.0f;

    return Ok;
}

static void camera_component_dispose(CameraComponentData *state) {
    if (!state)
        return;

    if (state->heap.pointer)
        deallocate(state->heap);
}

static void collider_component_dispose(ColliderComponentData *state) {
    if (!state)
        return;

    if (state->heap.pointer)
        deallocate(state->heap);
}

static void rigidbody_component_dispose(RigidbodyComponentData *state) {
    if (!state)
        return;

    if (state->heap.pointer)
        deallocate(state->heap);
}

static void point_light_component_dispose(PointLightComponentData *state) {
    if (!state)
        return;

    if (state->heap.pointer)
        deallocate(state->heap);
}

static void spot_light_component_dispose(SpotLightComponentData *state) {
    if (!state)
        return;

    if (state->heap.pointer)
        deallocate(state->heap);
}

static void direction_light_component_dispose(DirectionLightComponentData *state) {
    if (!state)
        return;

    if (state->heap.pointer)
        deallocate(state->heap);
}

static float clamp_non_negative(float value) {
    return value < 0.0f ? 0.0f : value;
}

static void rigidbody_component_integrate(Actor *actor, RigidbodyComponentData *state, float delta_time) {
    if (!actor || !state || delta_time <= 0.0f)
        return;

    state->is_grounded = False;

    if (!state->simulated)
        return;

    if (state->body_type == RigidbodyBodyTypeStatic) {
        state->velocity_x = 0.0f;
        state->velocity_y = 0.0f;
        state->angular_velocity = 0.0f;
        state->force_x = 0.0f;
        state->force_y = 0.0f;
        state->torque = 0.0f;
        return;
    }

    if (state->body_type == RigidbodyBodyTypeDynamic) {
        float acceleration_x = state->force_x * state->inverse_mass;
        float acceleration_y = state->force_y * state->inverse_mass;

        if (state->use_gravity) {
            acceleration_x += RuntimePhysicsGravityX * state->gravity_scale;
            acceleration_y += RuntimePhysicsGravityY * state->gravity_scale;
        }

        state->velocity_x += acceleration_x * delta_time;
        state->velocity_y += acceleration_y * delta_time;

        const float linear_drag = clamp_non_negative(state->linear_drag);
        if (linear_drag > 0.0f) {
            const float drag_multiplier = 1.0f / (1.0f + (linear_drag * delta_time));
            state->velocity_x *= drag_multiplier;
            state->velocity_y *= drag_multiplier;
        }

        state->angular_velocity += state->torque * state->inverse_mass * delta_time;
        const float angular_drag = clamp_non_negative(state->angular_drag);
        if (angular_drag > 0.0f) {
            const float drag_multiplier = 1.0f / (1.0f + (angular_drag * delta_time));
            state->angular_velocity *= drag_multiplier;
        }
    }

    if (!state->freeze_position_x)
        actor->transform.position.x += state->velocity_x * delta_time;
    else
        state->velocity_x = 0.0f;

    if (!state->freeze_position_y)
        actor->transform.position.y += state->velocity_y * delta_time;
    else
        state->velocity_y = 0.0f;

    if (!state->freeze_rotation)
        actor->transform.rotation_euler.z += state->angular_velocity * delta_time;
    else
        state->angular_velocity = 0.0f;

    state->force_x = 0.0f;
    state->force_y = 0.0f;
    state->torque = 0.0f;
}

static void rigidbody_component_resolve_collisions(void) {
    for (usize left_index = 0; left_index < runtime_state.actor_registry.actor_count; ++left_index) {
        Actor *left_actor = &runtime_state.actor_registry.actors[left_index];
        if (!left_actor->enabled)
            continue;

        ColliderComponentData *left_collider = ActorFindBuiltinComponentDataAs(left_actor, "Collider", ColliderComponentData);
        RigidbodyComponentData *left_rigidbody = ActorFindBuiltinComponentDataAs(left_actor, "Rigidbody", RigidbodyComponentData);
        if (!left_collider || !left_rigidbody || !left_collider->enabled || left_collider->is_trigger)
            continue;

        if (!left_rigidbody->simulated || left_rigidbody->body_type != RigidbodyBodyTypeDynamic)
            continue;

        for (usize right_index = 0; right_index < runtime_state.actor_registry.actor_count; ++right_index) {
            if (right_index == left_index)
                continue;

            Actor *right_actor = &runtime_state.actor_registry.actors[right_index];
            if (!right_actor->enabled)
                continue;

            ColliderComponentData *right_collider = ActorFindBuiltinComponentDataAs(right_actor, "Collider", ColliderComponentData);
            if (!right_collider || !right_collider->enabled || right_collider->is_trigger)
                continue;

            if (!collider_components_overlap(left_actor, left_collider, right_actor, right_collider))
                continue;

            const Rectangle left_bounds = collider_world_bounds(left_actor, left_collider);
            const Rectangle right_bounds = collider_world_bounds(right_actor, right_collider);
            const float overlap_x = fminf(left_bounds.x + left_bounds.width, right_bounds.x + right_bounds.width) - fmaxf(left_bounds.x, right_bounds.x);
            const float overlap_y = fminf(left_bounds.y + left_bounds.height, right_bounds.y + right_bounds.height) - fmaxf(left_bounds.y, right_bounds.y);

            if (overlap_x <= 0.0f || overlap_y <= 0.0f)
                continue;

            if (overlap_x < overlap_y && !left_rigidbody->freeze_position_x) {
                if (left_bounds.x < right_bounds.x)
                    left_actor->transform.position.x -= overlap_x;
                else
                    left_actor->transform.position.x += overlap_x;

                left_rigidbody->velocity_x = 0.0f;
                continue;
            }

            if (left_rigidbody->freeze_position_y)
                continue;

            if (left_bounds.y < right_bounds.y) {
                left_actor->transform.position.y -= overlap_y;
                left_rigidbody->is_grounded = True;
            } else {
                left_actor->transform.position.y += overlap_y;
            }

            left_rigidbody->velocity_y = 0.0f;
        }
    }
}

static void validate_sprite_material_reference(
    const char *component_name,
    const Actor *actor,
    const char *material_id,
    boolean *in_out_checked,
    boolean *in_out_resolved) {
    if (!material_id || material_id[0] == '\0' || !in_out_checked || !in_out_resolved)
        return;

    if (*in_out_checked)
        return;

    *in_out_checked = True;
    *in_out_resolved = False;

    const MaterialDescriptor *material = material_library_find_by_id(&runtime_state.material_library, material_id);
    if (material) {
        *in_out_resolved = True;
        return;
    }

    log_warn(
        "%s on actor '%s' references unknown material '%s'; falling back to legacy sprite draw",
        component_name ? component_name : "Sprite",
        (actor && actor->id) ? actor->id : "<unknown>",
        material_id);
}

static void static_sprite_component_draw(StaticSpriteState *state, CameraComponentData *active_camera, Actor *active_camera_actor) {
    if (!state)
        return;

    validate_sprite_material_reference(
        "StaticSprite",
        state->actor,
        state->material_id,
        &state->material_checked,
        &state->material_resolved);

    if (!state->loaded) {
        if (state->attempted_load)
            return;

        state->attempted_load = True;
        if (texture_cache_acquire(state->resolved_texture_path, &state->texture) != Ok) {
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

    float actor_rotation_z = 0.0f;
    if (state->actor)
        actor_rotation_z = state->actor->transform.rotation_euler.z;

    Shader *shader = Null;
    int time_location = -1;
    int speed_location = -1;
    int frequency_location = -1;
    int strength_location = -1;
    if (state->material_resolved && shader_cache_acquire_for_material(state->material_id, &shader, &time_location, &speed_location, &frequency_location, &strength_location) == Ok && shader) {
        const float runtime_time = (float)GetTime();
        const float default_speed = 3.5f;
        const float default_frequency = 22.0f;
        const float default_strength = 0.018f;

        if (time_location >= 0)
            SetShaderValue(*shader, time_location, &runtime_time, SHADER_UNIFORM_FLOAT);
        if (speed_location >= 0)
            SetShaderValue(*shader, speed_location, &default_speed, SHADER_UNIFORM_FLOAT);
        if (frequency_location >= 0)
            SetShaderValue(*shader, frequency_location, &default_frequency, SHADER_UNIFORM_FLOAT);
        if (strength_location >= 0)
            SetShaderValue(*shader, strength_location, &default_strength, SHADER_UNIFORM_FLOAT);

        BeginShaderMode(*shader);
        DrawTexturePro(state->texture, source, destination, anchor, state->rotation + actor_rotation_z, apply_global_light_to_color(state->tint));
        EndShaderMode();
        return;
    }

    DrawTexturePro(state->texture, source, destination, anchor, state->rotation + actor_rotation_z, apply_global_light_to_color(state->tint));
}

static void static_color_component_draw(StaticColorState *state, CameraComponentData *active_camera, Actor *active_camera_actor) {
    if (!state)
        return;

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
    float destination_width = state->size.x * actor_scale_x;
    float destination_height = state->size.y * actor_scale_y;

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
        state->anchor.offset.x * actor_scale_x,
        state->anchor.offset.y * actor_scale_y,
    };

    if (state->anchor.center) {
        anchor.x = destination.width * 0.5f;
        anchor.y = destination.height * 0.5f;
    }

    float actor_rotation_z = 0.0f;
    if (state->actor)
        actor_rotation_z = state->actor->transform.rotation_euler.z;

    DrawRectanglePro(destination, anchor, state->rotation + actor_rotation_z, state->color);
}

static void static_sprite_component_dispose(StaticSpriteState *state) {
    if (!state)
        return;

    if (state->loaded) {
        texture_cache_release(state->resolved_texture_path, state->texture);
        state->texture = (Texture2D){0};
        state->loaded = False;
    }

    state->attempted_load = False;

    if (state->heap.pointer)
        deallocate(state->heap);
}

static void static_color_component_dispose(StaticColorState *state) {
    if (!state)
        return;

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
    if (texture_cache_acquire(sheet->resolved_texture_path, &sheet->texture) != Ok) {
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

    const char *material_main_texture = find_material_main_texture_ref(state->material_id);
    if (material_main_texture && material_main_texture[0] != '\0') {
        for (int sheet_index = 0; sheet_index < state->sheet_count; ++sheet_index) {
            AnimatedSpriteSheet *sheet = &state->sheets[sheet_index];
            if (snprintf(sheet->texture_path, sizeof(sheet->texture_path), "%s", material_main_texture) >= (int)sizeof(sheet->texture_path)) {
                log_err("Material main texture path is too long for actor '%s'", actor && actor->id ? actor->id : "<unknown>");
                goto cleanup;
            }

            if (join_path(runtime_state.project_root, sheet->texture_path, sheet->resolved_texture_path, sizeof(sheet->resolved_texture_path)) != Ok) {
                log_err("Failed to resolve material main texture path '%s'", sheet->texture_path);
                goto cleanup;
            }
        }
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
    state->material_checked = False;
    state->material_resolved = False;

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

    validate_sprite_material_reference(
        "AnimatedSprite",
        state->actor,
        state->material_id,
        &state->material_checked,
        &state->material_resolved);

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

    float actor_rotation_z = 0.0f;
    if (state->actor)
        actor_rotation_z = state->actor->transform.rotation_euler.z;

    Shader *shader = Null;
    int time_location = -1;
    int speed_location = -1;
    int frequency_location = -1;
    int strength_location = -1;
    if (state->material_resolved && shader_cache_acquire_for_material(state->material_id, &shader, &time_location, &speed_location, &frequency_location, &strength_location) == Ok && shader) {
        const float runtime_time = (float)GetTime();
        const float default_speed = 3.5f;
        const float default_frequency = 22.0f;
        const float default_strength = 0.018f;

        if (time_location >= 0)
            SetShaderValue(*shader, time_location, &runtime_time, SHADER_UNIFORM_FLOAT);
        if (speed_location >= 0)
            SetShaderValue(*shader, speed_location, &default_speed, SHADER_UNIFORM_FLOAT);
        if (frequency_location >= 0)
            SetShaderValue(*shader, frequency_location, &default_frequency, SHADER_UNIFORM_FLOAT);
        if (strength_location >= 0)
            SetShaderValue(*shader, strength_location, &default_strength, SHADER_UNIFORM_FLOAT);

        BeginShaderMode(*shader);
        DrawTexturePro(sheet->texture, source, destination, anchor, state->rotation + actor_rotation_z, apply_global_light_to_color(state->tint));
        EndShaderMode();
        return;
    }

    DrawTexturePro(sheet->texture, source, destination, anchor, state->rotation + actor_rotation_z, apply_global_light_to_color(state->tint));
}

static void animated_sprite_component_dispose(AnimatedSpriteState *state) {
    if (!state)
        return;

    for (int sheet_index = 0; sheet_index < state->sheet_count; ++sheet_index) {
        AnimatedSpriteSheet *sheet = &state->sheets[sheet_index];
        if (sheet->loaded) {
            texture_cache_release(sheet->resolved_texture_path, sheet->texture);
            sheet->texture = (Texture2D){0};
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

static result join_path(const char *base, const char *path, char *out_path, usize out_size) {
    return vfs_resolve_path(base, path, out_path, out_size);
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

    toml_result_t parsed = {0};
    if (vfs_parse_toml_file(path, &parsed) != Ok) {
        log_err("Failed to parse config '%s'", path);
        return Err;
    }

    *out_parsed = parsed;
    return Ok;
}

static result validate_conf_files(const char *directory_path, usize *out_conf_count) {
    if (!directory_path || !out_conf_count)
        return Err;

    if (vfs_is_archive_mode()) {
        usize conf_count = 0;
        if (vfs_count_conf_files(&conf_count) != Ok)
            return Err;

        *out_conf_count = conf_count;
        return Ok;
    }

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

#ifdef Debug
static boolean debug_collider_toggle_pressed(void) {
    return IsKeyPressed(KEY_ONE) && (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT));
}

enum {
    DebugLightEditPosition = 0,
    DebugLightEditRange,
    DebugLightEditIntensity,
    DebugLightEditColor,
    DebugLightEditModeCount,
};

static boolean debug_light_toggle_pressed(void) {
    return IsKeyPressed(KEY_TWO) && (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT));
}

static boolean debug_is_shift_down(void) {
    return IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
}

static float debug_collider_edit_step(void) {
    if (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL))
        return 8.0f;

    return 1.0f;
}

static void debug_adjust_color_channel(unsigned char *channel, int delta) {
    if (!channel || delta == 0)
        return;

    int next_value = (int)*channel + delta;
    if (next_value < 0)
        next_value = 0;
    if (next_value > 255)
        next_value = 255;

    *channel = (unsigned char)next_value;
}

static const char *debug_light_mode_name(usize mode) {
    switch (mode) {
    case DebugLightEditPosition:
        return "Position";
    case DebugLightEditRange:
        return "Range";
    case DebugLightEditIntensity:
        return "Intensity";
    case DebugLightEditColor:
        return "Color";
    default:
        return "Unknown";
    }
}

static boolean debug_get_visible_collider_at(usize target_index, Actor **out_actor, ColliderComponentData **out_collider, usize *out_count) {
    if (out_actor)
        *out_actor = Null;
    if (out_collider)
        *out_collider = Null;

    usize visible_index = 0;
    for (usize actor_index = 0; actor_index < runtime_state.actor_registry.actor_count; ++actor_index) {
        Actor *actor = &runtime_state.actor_registry.actors[actor_index];
        if (!actor->enabled)
            continue;

        ColliderComponentData *collider = ActorFindBuiltinComponentDataAs(actor, "Collider", ColliderComponentData);
        if (!collider || !collider->enabled)
            continue;

        if (visible_index == target_index) {
            if (out_actor)
                *out_actor = actor;
            if (out_collider)
                *out_collider = collider;
        }

        ++visible_index;
    }

    if (out_count)
        *out_count = visible_index;

    return (out_actor && out_collider && *out_actor && *out_collider) ? True : False;
}

static void debug_log_selected_collider(usize collider_index, const Actor *actor, const ColliderComponentData *collider) {
    if (!actor || !collider)
        return;

    const char *actor_id = actor->id ? actor->id : "<unknown>";
    log_msg(
        "Debug collider[%zu] actor=%s offset=(%.1f, %.1f) size=(%.1f, %.1f)",
        collider_index,
        actor_id,
        collider->offset.x,
        collider->offset.y,
        collider->size.x,
        collider->size.y
    );
}

static void debug_copy_selected_collider_to_clipboard(usize collider_index, const Actor *actor, const ColliderComponentData *collider) {
    if (!actor || !collider)
        return;

    const char *actor_id = actor->id ? actor->id : "<unknown>";
    char clipboard_text[512] = {0};
    const int written = snprintf(
        clipboard_text,
        sizeof(clipboard_text),
        "# Collider debug export for actor '%s' (index %zu)\n"
        "offset = [%.1f, %.1f]\n"
        "size = [%.1f, %.1f]\n"
        "enabled = %s\n"
        "is_trigger = %s\n",
        actor_id,
        collider_index,
        collider->offset.x,
        collider->offset.y,
        collider->size.x,
        collider->size.y,
        collider->enabled ? "true" : "false",
        collider->is_trigger ? "true" : "false"
    );

    if (written < 0 || written >= (int)sizeof(clipboard_text)) {
        log_err("Failed to export collider values to clipboard: output too long");
        return;
    }

    SetClipboardText(clipboard_text);
    log_msg("Copied collider[%zu] for actor '%s' to clipboard", collider_index, actor_id);
}

static boolean debug_get_visible_light_at(usize target_index, Actor **out_actor, PointLightComponentData **out_light, usize *out_count) {
    if (out_actor)
        *out_actor = Null;
    if (out_light)
        *out_light = Null;

    usize visible_index = 0;
    for (usize actor_index = 0; actor_index < runtime_state.actor_registry.actor_count; ++actor_index) {
        Actor *actor = &runtime_state.actor_registry.actors[actor_index];
        if (!actor->enabled)
            continue;

        PointLightComponentData *point_light = ActorFindBuiltinComponentDataAs(actor, "PointLight", PointLightComponentData);
        if (!point_light)
            continue;

        if (visible_index == target_index) {
            if (out_actor)
                *out_actor = actor;
            if (out_light)
                *out_light = point_light;
        }

        ++visible_index;
    }

    if (out_count)
        *out_count = visible_index;

    return (out_actor && out_light && *out_actor && *out_light) ? True : False;
}

static void debug_log_selected_light(usize light_index, const Actor *actor, const PointLightComponentData *light) {
    if (!actor || !light)
        return;

    const char *actor_id = actor->id ? actor->id : "<unknown>";
    log_msg(
        "Debug point_light[%zu] actor=%s position=(%.1f, %.1f, %.1f) color=(%u, %u, %u, %u) intensity=%.2f range=%.2f enabled=%s",
        light_index,
        actor_id,
        light->position.x,
        light->position.y,
        light->position.z,
        (unsigned)light->color.r,
        (unsigned)light->color.g,
        (unsigned)light->color.b,
        (unsigned)light->color.a,
        light->intensity,
        light->range,
        light->enabled ? "true" : "false"
    );
}

static void debug_copy_selected_light_to_clipboard(usize light_index, const Actor *actor, const PointLightComponentData *light) {
    if (!actor || !light)
        return;

    const char *actor_id = actor->id ? actor->id : "<unknown>";
    char clipboard_text[768] = {0};
    const int written = snprintf(
        clipboard_text,
        sizeof(clipboard_text),
        "# PointLight debug export for actor '%s' (index %zu)\n"
        "[Actors.Components.PointLight]\n"
        "position = [%.2f, %.2f, %.2f]\n"
        "color = [%u, %u, %u, %u]\n"
        "intensity = %.3f\n"
        "range = %.3f\n"
        "enabled = %s\n",
        actor_id,
        light_index,
        light->position.x,
        light->position.y,
        light->position.z,
        (unsigned)light->color.r,
        (unsigned)light->color.g,
        (unsigned)light->color.b,
        (unsigned)light->color.a,
        light->intensity,
        light->range,
        light->enabled ? "true" : "false"
    );

    if (written < 0 || written >= (int)sizeof(clipboard_text)) {
        log_err("Failed to export point light values to clipboard: output too long");
        return;
    }

    SetClipboardText(clipboard_text);
    log_msg("Copied point_light[%zu] for actor '%s' to clipboard", light_index, actor_id);
}

static void debug_update_light_editor(void) {
    if (!runtime_state.debug_show_light_gizmos)
        return;

    usize light_count = 0;
    (void)debug_get_visible_light_at(0, Null, Null, &light_count);
    if (light_count == 0)
        return;

    if (runtime_state.debug_selected_light_index >= light_count)
        runtime_state.debug_selected_light_index = 0;

    if (IsKeyPressed(KEY_K))
        runtime_state.debug_selected_light_index = (runtime_state.debug_selected_light_index + 1) % light_count;

    if (IsKeyPressed(KEY_J))
        runtime_state.debug_selected_light_index = (runtime_state.debug_selected_light_index + light_count - 1) % light_count;

    if (runtime_state.debug_light_edit_mode >= DebugLightEditModeCount)
        runtime_state.debug_light_edit_mode = DebugLightEditPosition;

    if (IsKeyPressed(KEY_TAB))
        runtime_state.debug_light_edit_mode = (runtime_state.debug_light_edit_mode + 1) % DebugLightEditModeCount;

    Actor *selected_actor = Null;
    PointLightComponentData *selected_light = Null;
    if (!debug_get_visible_light_at(runtime_state.debug_selected_light_index, &selected_actor, &selected_light, Null))
        return;

    const float position_step = debug_collider_edit_step();
    const float range_step = debug_collider_edit_step();
    const float intensity_step = (debug_collider_edit_step() > 1.0f) ? 1.0f : 0.1f;
    const int color_step = (debug_collider_edit_step() > 1.0f) ? 16 : 4;
    boolean changed = False;

    if (runtime_state.debug_light_edit_mode == DebugLightEditPosition) {
        if (IsKeyPressed(KEY_LEFT)) {
            selected_light->position.x -= position_step;
            changed = True;
        }
        if (IsKeyPressed(KEY_RIGHT)) {
            selected_light->position.x += position_step;
            changed = True;
        }
        if (IsKeyPressed(KEY_UP)) {
            selected_light->position.y -= position_step;
            changed = True;
        }
        if (IsKeyPressed(KEY_DOWN)) {
            selected_light->position.y += position_step;
            changed = True;
        }
        if (IsKeyPressed(KEY_Z)) {
            selected_light->position.z -= position_step;
            changed = True;
        }
        if (IsKeyPressed(KEY_X)) {
            selected_light->position.z += position_step;
            changed = True;
        }
    } else if (runtime_state.debug_light_edit_mode == DebugLightEditRange) {
        if (IsKeyPressed(KEY_LEFT) || IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_LEFT_BRACKET)) {
            selected_light->range -= range_step;
            changed = True;
        }
        if (IsKeyPressed(KEY_RIGHT) || IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_RIGHT_BRACKET)) {
            selected_light->range += range_step;
            changed = True;
        }

        if (selected_light->range < 1.0f)
            selected_light->range = 1.0f;
    } else if (runtime_state.debug_light_edit_mode == DebugLightEditIntensity) {
        if (IsKeyPressed(KEY_LEFT) || IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_MINUS)) {
            selected_light->intensity -= intensity_step;
            changed = True;
        }
        if (IsKeyPressed(KEY_RIGHT) || IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_EQUAL)) {
            selected_light->intensity += intensity_step;
            changed = True;
        }

        if (selected_light->intensity < 0.0f)
            selected_light->intensity = 0.0f;
    } else if (runtime_state.debug_light_edit_mode == DebugLightEditColor) {
        if (IsKeyPressed(KEY_LEFT)) {
            debug_adjust_color_channel(&selected_light->color.r, -color_step);
            changed = True;
        }
        if (IsKeyPressed(KEY_RIGHT)) {
            debug_adjust_color_channel(&selected_light->color.r, color_step);
            changed = True;
        }
        if (IsKeyPressed(KEY_DOWN)) {
            debug_adjust_color_channel(&selected_light->color.g, -color_step);
            changed = True;
        }
        if (IsKeyPressed(KEY_UP)) {
            debug_adjust_color_channel(&selected_light->color.g, color_step);
            changed = True;
        }
        if (IsKeyPressed(KEY_N)) {
            debug_adjust_color_channel(&selected_light->color.b, -color_step);
            changed = True;
        }
        if (IsKeyPressed(KEY_B)) {
            debug_adjust_color_channel(&selected_light->color.b, color_step);
            changed = True;
        }
    }

    if (IsKeyPressed(KEY_E)) {
        selected_light->enabled = !selected_light->enabled;
        changed = True;
    }

    if (changed || IsKeyPressed(KEY_J) || IsKeyPressed(KEY_K) || IsKeyPressed(KEY_TAB))
        debug_log_selected_light(runtime_state.debug_selected_light_index, selected_actor, selected_light);

    if (IsKeyPressed(KEY_C))
        debug_copy_selected_light_to_clipboard(runtime_state.debug_selected_light_index, selected_actor, selected_light);
}

static void debug_update_collider_editor(void) {
    if (!runtime_state.debug_show_collider_borders)
        return;

    usize collider_count = 0;
    (void)debug_get_visible_collider_at(0, Null, Null, &collider_count);
    if (collider_count == 0)
        return;

    if (runtime_state.debug_selected_collider_index >= collider_count)
        runtime_state.debug_selected_collider_index = 0;

    if (IsKeyPressed(KEY_K))
        runtime_state.debug_selected_collider_index = (runtime_state.debug_selected_collider_index + 1) % collider_count;

    if (IsKeyPressed(KEY_J))
        runtime_state.debug_selected_collider_index = (runtime_state.debug_selected_collider_index + collider_count - 1) % collider_count;

    Actor *selected_actor = Null;
    ColliderComponentData *selected_collider = Null;
    if (!debug_get_visible_collider_at(runtime_state.debug_selected_collider_index, &selected_actor, &selected_collider, Null))
        return;

    const float step = debug_collider_edit_step();
    const boolean edit_size = debug_is_shift_down();
    boolean changed = False;

    if (!edit_size) {
        if (IsKeyPressed(KEY_LEFT)) {
            selected_collider->offset.x -= step;
            changed = True;
        }
        if (IsKeyPressed(KEY_RIGHT)) {
            selected_collider->offset.x += step;
            changed = True;
        }
        if (IsKeyPressed(KEY_UP)) {
            selected_collider->offset.y -= step;
            changed = True;
        }
        if (IsKeyPressed(KEY_DOWN)) {
            selected_collider->offset.y += step;
            changed = True;
        }
    } else {
        if (IsKeyPressed(KEY_LEFT)) {
            selected_collider->size.x -= step;
            changed = True;
        }
        if (IsKeyPressed(KEY_RIGHT)) {
            selected_collider->size.x += step;
            changed = True;
        }
        if (IsKeyPressed(KEY_UP)) {
            selected_collider->size.y += step;
            changed = True;
        }
        if (IsKeyPressed(KEY_DOWN)) {
            selected_collider->size.y -= step;
            changed = True;
        }

        if (selected_collider->size.x < 1.0f)
            selected_collider->size.x = 1.0f;
        if (selected_collider->size.y < 1.0f)
            selected_collider->size.y = 1.0f;
    }

    if (changed || IsKeyPressed(KEY_J) || IsKeyPressed(KEY_K))
        debug_log_selected_collider(runtime_state.debug_selected_collider_index, selected_actor, selected_collider);

    if (IsKeyPressed(KEY_C))
        debug_copy_selected_collider_to_clipboard(runtime_state.debug_selected_collider_index, selected_actor, selected_collider);
}

static Rectangle normalize_rectangle(Rectangle rectangle) {
    if (rectangle.width < 0.0f) {
        rectangle.x += rectangle.width;
        rectangle.width = -rectangle.width;
    }

    if (rectangle.height < 0.0f) {
        rectangle.y += rectangle.height;
        rectangle.height = -rectangle.height;
    }

    return rectangle;
}

static Rectangle project_world_rectangle(Rectangle world_bounds, CameraComponentData *active_camera, Actor *active_camera_actor) {
    if (!active_camera || !active_camera_actor)
        return world_bounds;

    float zoom = 1.0f;
    if (active_camera->camera.fovy > 0.001f)
        zoom = 60.0f / active_camera->camera.fovy;

    const float camera_x = active_camera_actor->transform.position.x;
    const float camera_y = active_camera_actor->transform.position.y;

    Rectangle projected = {
        .x = (world_bounds.x - camera_x) * zoom + ((float)GetScreenWidth() * 0.5f),
        .y = (world_bounds.y - camera_y) * zoom + ((float)GetScreenHeight() * 0.5f),
        .width = world_bounds.width * zoom,
        .height = world_bounds.height * zoom,
    };

    return projected;
}
static void debug_draw_collider_borders(void) {
    usize visible_index = 0;
    usize collider_count = 0;
    (void)debug_get_visible_collider_at(0, Null, Null, &collider_count);

    for (usize actor_index = 0; actor_index < runtime_state.actor_registry.actor_count; ++actor_index) {
        Actor *actor = &runtime_state.actor_registry.actors[actor_index];
        if (!actor->enabled)
            continue;

        ColliderComponentData *collider = ActorFindBuiltinComponentDataAs(actor, "Collider", ColliderComponentData);
        if (!collider || !collider->enabled)
            continue;

        Rectangle collider_bounds = collider_world_bounds(actor, collider);
        collider_bounds = project_world_rectangle(collider_bounds, runtime_state.active_camera, runtime_state.active_camera_actor);
        collider_bounds = normalize_rectangle(collider_bounds);

        const boolean selected = (visible_index == runtime_state.debug_selected_collider_index);
        const Color border_color = selected ? YELLOW : (collider->is_trigger ? ORANGE : LIME);
        DrawRectangleLinesEx(collider_bounds, selected ? 2.0f : 1.0f, border_color);

        if (actor->id && actor->id[0] != '\0')
            DrawText(actor->id, (int)collider_bounds.x, (int)(collider_bounds.y - 14.0f), 10, border_color);

        ++visible_index;
    }

    if (collider_count > 0) {
        char hud_line[256] = {0};
        (void)snprintf(
            hud_line,
            sizeof(hud_line),
            "Collider Debug: J/K select (%zu/%zu) | Arrows: offset | Shift+Arrows: size | Ctrl: step x8 | C: copy current offset/size to clipboard",
            runtime_state.debug_selected_collider_index + 1,
            collider_count
        );

        const int footer_height = 18;
        int footer_y = GetScreenHeight() - footer_height;
        if (footer_y < 0)
            footer_y = 0;

        DrawRectangle(0, footer_y, GetScreenWidth(), footer_height, Fade(BLACK, 0.55f));

        DrawText(hud_line, 10, footer_y, 12, WHITE);
    }
}

static void debug_draw_light_gizmos(void) {
    if (!runtime_state.debug_show_light_gizmos)
        return;

    usize visible_index = 0;
    usize light_count = 0;
    (void)debug_get_visible_light_at(0, Null, Null, &light_count);

    for (usize actor_index = 0; actor_index < runtime_state.actor_registry.actor_count; ++actor_index) {
        Actor *actor = &runtime_state.actor_registry.actors[actor_index];
        if (!actor->enabled)
            continue;

        PointLightComponentData *point_light = ActorFindBuiltinComponentDataAs(actor, "PointLight", PointLightComponentData);
        if (!point_light)
            continue;

        const float world_x = actor->transform.position.x + point_light->position.x;
        const float world_y = actor->transform.position.y + point_light->position.y;

        float draw_x = world_x;
        float draw_y = world_y;
        float draw_radius = point_light->range;
        if (draw_radius <= 0.0f)
            draw_radius = 1.0f;

        if (runtime_state.active_camera && runtime_state.active_camera_actor) {
            float zoom = 1.0f;
            if (runtime_state.active_camera->camera.fovy > 0.001f)
                zoom = 60.0f / runtime_state.active_camera->camera.fovy;

            const float camera_x = runtime_state.active_camera_actor->transform.position.x;
            const float camera_y = runtime_state.active_camera_actor->transform.position.y;
            draw_x = (world_x - camera_x) * zoom + ((float)GetScreenWidth() * 0.5f);
            draw_y = (world_y - camera_y) * zoom + ((float)GetScreenHeight() * 0.5f);
            draw_radius *= zoom;
        }

        if (draw_radius < 2.0f)
            draw_radius = 2.0f;

        const boolean selected = (visible_index == runtime_state.debug_selected_light_index);
        const Color ring_color = selected ? YELLOW : (point_light->enabled ? SKYBLUE : GRAY);
        DrawCircleLines((int)draw_x, (int)draw_y, draw_radius, ring_color);
        DrawCircleLines((int)draw_x, (int)draw_y, selected ? 5.0f : 3.0f, ring_color);

        if (actor->id && actor->id[0] != '\0')
            DrawText(actor->id, (int)draw_x + 8, (int)draw_y - 16, 10, ring_color);

        ++visible_index;
    }

    if (light_count > 0) {
        char hud_line[512] = {0};
        (void)snprintf(
            hud_line,
            sizeof(hud_line),
            "Light Debug: J/K select (%zu/%zu) | Tab mode=%s | Arrows edit | Z/X: Z-pos | B/N: Blue | E: enabled | C: copy TOML | Ctrl: larger step",
            runtime_state.debug_selected_light_index + 1,
            light_count,
            debug_light_mode_name(runtime_state.debug_light_edit_mode)
        );

        const int footer_height = 18;
        int footer_y = GetScreenHeight() - footer_height;
        if (footer_y < 0)
            footer_y = 0;

        DrawRectangle(0, footer_y, GetScreenWidth(), footer_height, Fade(BLACK, 0.55f));
        DrawText(hud_line, 10, footer_y, 12, WHITE);
    }
}
#endif

static void draw_point_light_overlays(CameraComponentData *active_camera, Actor *active_camera_actor) {
    BeginBlendMode(BLEND_ADDITIVE);

    for (usize actor_index = 0; actor_index < runtime_state.actor_registry.actor_count; ++actor_index) {
        Actor *actor = &runtime_state.actor_registry.actors[actor_index];
        if (!actor->enabled)
            continue;

        PointLightComponentData *point_light = ActorFindBuiltinComponentDataAs(actor, "PointLight", PointLightComponentData);
        if (!point_light || !point_light->enabled)
            continue;

        const float world_x = actor->transform.position.x + point_light->position.x;
        const float world_y = actor->transform.position.y + point_light->position.y;

        float draw_x = world_x;
        float draw_y = world_y;
        float draw_radius = point_light->range;
        if (draw_radius <= 0.0f)
            draw_radius = 1.0f;

        if (active_camera && active_camera_actor) {
            float zoom = 1.0f;
            if (active_camera->camera.fovy > 0.001f)
                zoom = 60.0f / active_camera->camera.fovy;

            const float camera_x = active_camera_actor->transform.position.x;
            const float camera_y = active_camera_actor->transform.position.y;
            draw_x = (world_x - camera_x) * zoom + ((float)GetScreenWidth() * 0.5f);
            draw_y = (world_y - camera_y) * zoom + ((float)GetScreenHeight() * 0.5f);
            draw_radius *= zoom;
        }

        if (draw_radius <= 0.5f)
            continue;

        float intensity = point_light->intensity;
        if (intensity < 0.0f)
            intensity = 0.0f;
        if (intensity > 8.0f)
            intensity = 8.0f;

        const Color lit_color = apply_global_light_to_color(point_light->color);
        int alpha = (int)(intensity * 70.0f);
        if (alpha < 0)
            alpha = 0;
        if (alpha > 255)
            alpha = 255;

        const Color center = (Color){lit_color.r, lit_color.g, lit_color.b, (unsigned char)alpha};
        const Color outer = (Color){0, 0, 0, 0};
        DrawCircleGradient((int)draw_x, (int)draw_y, draw_radius, center, outer);
    }

    EndBlendMode();
}

static void frame_update(void) {
    if (!runtime_state.active)
        return;

#ifdef Debug
    if (debug_collider_toggle_pressed()) {
        runtime_state.debug_show_collider_borders = !runtime_state.debug_show_collider_borders;
        if (runtime_state.debug_show_collider_borders)
            runtime_state.debug_show_light_gizmos = False;
        log_msg("Debug collider borders: %s", runtime_state.debug_show_collider_borders ? "ON" : "OFF");
    }

    if (debug_light_toggle_pressed()) {
        runtime_state.debug_show_light_gizmos = !runtime_state.debug_show_light_gizmos;
        if (runtime_state.debug_show_light_gizmos) {
            runtime_state.debug_show_collider_borders = False;
            if (runtime_state.debug_light_edit_mode >= DebugLightEditModeCount)
                runtime_state.debug_light_edit_mode = DebugLightEditPosition;
        }
        log_msg("Debug light gizmos: %s", runtime_state.debug_show_light_gizmos ? "ON" : "OFF");
    }

    if (runtime_state.debug_show_collider_borders)
        debug_update_collider_editor();

    if (runtime_state.debug_show_light_gizmos)
        debug_update_light_editor();
#endif

    process_pending_scene_load();
    (void)process_pending_prefab_instantiations();
    process_pending_actor_destroys();

    script_runtime_begin_frame(&runtime_state.script_runtime);
    const float delta_time = runtime_state.script_runtime.time_delta_time;

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

            if (component->descriptor.kind == ComponentBuiltin && component->descriptor.name && strcmp(component->descriptor.name, "Rigidbody") == 0)
                rigidbody_component_integrate(actor, (RigidbodyComponentData *)component->data, delta_time);

            if (component->descriptor.kind == ComponentBuiltin && component->descriptor.name && strcmp(component->descriptor.name, "AnimatedSprite") == 0)
                animated_sprite_component_update((AnimatedSpriteState *)component->data, delta_time);

            if (actor->pending_destroy)
                break;
        }
    }

    rigidbody_component_resolve_collisions();
    apply_parent_transform_deltas();
    sync_parented_actor_positions();

    process_pending_actor_destroys();

    runtime_state.active_camera = Null;
    runtime_state.active_camera_actor = Null;
    for (usize actor_index = 0; actor_index < runtime_state.actor_registry.actor_count; ++actor_index) {
        Actor *actor = &runtime_state.actor_registry.actors[actor_index];
        if (!actor->enabled)
            continue;

        CameraComponentData *camera_state = ActorFindBuiltinComponentDataAs(actor, "Camera", CameraComponentData);
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

    if (enabled_actor_count == 0) {
        capture_actor_previous_transforms();
        return;
    }

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

    const int screen_width = GetScreenWidth();
    const int screen_height = GetScreenHeight();
    boolean use_postfx = runtime_postfx_stack.enabled && runtime_postfx_stack.pass_count > 0;
    if (use_postfx && postfx_ensure_targets(screen_width, screen_height) != Ok)
        use_postfx = False;

    if (use_postfx) {
        BeginTextureMode(runtime_postfx_scene_target);
        ClearBackground(BLANK);
    }

    for (usize index = 0; index < draw_order_index; ++index) {
        Actor *actor = draw_order[index];
        for (usize component_index = 0; component_index < actor->component_count; ++component_index) {
            ActorComponent *component = &actor->components[component_index];

            if (component->descriptor.kind == ComponentBuiltin && component->descriptor.name && strcmp(component->descriptor.name, "StaticSprite") == 0)
                static_sprite_component_draw((StaticSpriteState *)component->data, runtime_state.active_camera, runtime_state.active_camera_actor);

            if (component->descriptor.kind == ComponentBuiltin && component->descriptor.name && strcmp(component->descriptor.name, "StaticColor") == 0)
                static_color_component_draw((StaticColorState *)component->data, runtime_state.active_camera, runtime_state.active_camera_actor);

            if (component->descriptor.kind == ComponentBuiltin && component->descriptor.name && strcmp(component->descriptor.name, "AnimatedSprite") == 0)
                animated_sprite_component_draw((AnimatedSpriteState *)component->data, runtime_state.active_camera, runtime_state.active_camera_actor);
        }
    }
    draw_point_light_overlays(runtime_state.active_camera, runtime_state.active_camera_actor);

#ifdef Debug
    if (runtime_state.debug_show_collider_borders)
        debug_draw_collider_borders();

    if (runtime_state.debug_show_light_gizmos)
        debug_draw_light_gizmos();
#endif

    if (use_postfx)
        EndTextureMode();

    if (use_postfx) {
        RenderTexture2D *input_target = &runtime_postfx_scene_target;

        for (usize pass_index = 0; pass_index < runtime_postfx_stack.pass_count; ++pass_index) {
            const PostFxPassConfig *pass = &runtime_postfx_stack.passes[pass_index];
            const boolean last_pass = (pass_index + 1) >= runtime_postfx_stack.pass_count;

            SharedShaderEntry *shader_entry = Null;
            if (shader_cache_acquire_by_id(pass->shader_id, &shader_entry) != Ok || !shader_entry) {
                if (last_pass) {
                    draw_texture_fullscreen(input_target->texture, screen_width, screen_height);
                } else {
                    RenderTexture2D *output_target = (input_target == &runtime_postfx_scene_target)
                        ? &runtime_postfx_ping_target
                        : &runtime_postfx_scene_target;

                    BeginTextureMode(*output_target);
                    ClearBackground(BLANK);
                    draw_texture_fullscreen(input_target->texture, screen_width, screen_height);
                    EndTextureMode();
                    input_target = output_target;
                }

                continue;
            }

            const float runtime_time = (float)GetTime();
            const float screen_size[2] = {(float)screen_width, (float)screen_height};
            float pixel_size = pass->pixel_size;
            float vignette_inner = pass->vignette_inner;
            float vignette_outer = pass->vignette_outer;
            float edge_glow = pass->edge_glow;
            float pulse_speed = pass->pulse_speed;
            float intensity = pass->intensity;
            float override_value = 0.0f;

            if (material_override_get_float(pass->material_alias, "pixel_size", &override_value))
                pixel_size = override_value;
            if (material_override_get_float(pass->material_alias, "vignette_inner", &override_value))
                vignette_inner = override_value;
            if (material_override_get_float(pass->material_alias, "vignette_outer", &override_value))
                vignette_outer = override_value;
            if (material_override_get_float(pass->material_alias, "edge_glow", &override_value))
                edge_glow = override_value;
            if (material_override_get_float(pass->material_alias, "pulse_speed", &override_value))
                pulse_speed = override_value;
            if (material_override_get_float(pass->material_alias, "intensity", &override_value))
                intensity = override_value;

            if (intensity < 0.0f)
                intensity = 0.0f;

            edge_glow *= intensity;

            if (last_pass) {
                if (shader_entry->time_location >= 0)
                    SetShaderValue(shader_entry->shader, shader_entry->time_location, &runtime_time, SHADER_UNIFORM_FLOAT);
                if (shader_entry->screen_size_location >= 0)
                    SetShaderValue(shader_entry->shader, shader_entry->screen_size_location, screen_size, SHADER_UNIFORM_VEC2);
                if (shader_entry->pixel_size_location >= 0)
                    SetShaderValue(shader_entry->shader, shader_entry->pixel_size_location, &pixel_size, SHADER_UNIFORM_FLOAT);
                if (shader_entry->vignette_inner_location >= 0)
                    SetShaderValue(shader_entry->shader, shader_entry->vignette_inner_location, &vignette_inner, SHADER_UNIFORM_FLOAT);
                if (shader_entry->vignette_outer_location >= 0)
                    SetShaderValue(shader_entry->shader, shader_entry->vignette_outer_location, &vignette_outer, SHADER_UNIFORM_FLOAT);
                if (shader_entry->edge_glow_location >= 0)
                    SetShaderValue(shader_entry->shader, shader_entry->edge_glow_location, &edge_glow, SHADER_UNIFORM_FLOAT);
                if (shader_entry->pulse_speed_location >= 0)
                    SetShaderValue(shader_entry->shader, shader_entry->pulse_speed_location, &pulse_speed, SHADER_UNIFORM_FLOAT);
                if (shader_entry->intensity_location >= 0)
                    SetShaderValue(shader_entry->shader, shader_entry->intensity_location, &intensity, SHADER_UNIFORM_FLOAT);

                BeginShaderMode(shader_entry->shader);
                draw_texture_fullscreen(input_target->texture, screen_width, screen_height);
                EndShaderMode();
            } else {
                RenderTexture2D *output_target = (input_target == &runtime_postfx_scene_target)
                    ? &runtime_postfx_ping_target
                    : &runtime_postfx_scene_target;

                BeginTextureMode(*output_target);
                ClearBackground(BLANK);

                if (shader_entry->time_location >= 0)
                    SetShaderValue(shader_entry->shader, shader_entry->time_location, &runtime_time, SHADER_UNIFORM_FLOAT);
                if (shader_entry->screen_size_location >= 0)
                    SetShaderValue(shader_entry->shader, shader_entry->screen_size_location, screen_size, SHADER_UNIFORM_VEC2);
                if (shader_entry->pixel_size_location >= 0)
                    SetShaderValue(shader_entry->shader, shader_entry->pixel_size_location, &pixel_size, SHADER_UNIFORM_FLOAT);
                if (shader_entry->vignette_inner_location >= 0)
                    SetShaderValue(shader_entry->shader, shader_entry->vignette_inner_location, &vignette_inner, SHADER_UNIFORM_FLOAT);
                if (shader_entry->vignette_outer_location >= 0)
                    SetShaderValue(shader_entry->shader, shader_entry->vignette_outer_location, &vignette_outer, SHADER_UNIFORM_FLOAT);
                if (shader_entry->edge_glow_location >= 0)
                    SetShaderValue(shader_entry->shader, shader_entry->edge_glow_location, &edge_glow, SHADER_UNIFORM_FLOAT);
                if (shader_entry->pulse_speed_location >= 0)
                    SetShaderValue(shader_entry->shader, shader_entry->pulse_speed_location, &pulse_speed, SHADER_UNIFORM_FLOAT);
                if (shader_entry->intensity_location >= 0)
                    SetShaderValue(shader_entry->shader, shader_entry->intensity_location, &intensity, SHADER_UNIFORM_FLOAT);

                BeginShaderMode(shader_entry->shader);
                draw_texture_fullscreen(input_target->texture, screen_width, screen_height);
                EndShaderMode();

                EndTextureMode();
                input_target = output_target;
            }
        }
    }

    if (runtime_state.ui_runtime)
        ui_runtime_draw(runtime_state.ui_runtime);

    deallocate(draw_order_heap);
    capture_actor_previous_transforms();
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

            if (component->descriptor.kind == ComponentBuiltin && component->descriptor.name && strcmp(component->descriptor.name, "StaticColor") == 0)
                static_color_component_dispose((StaticColorState *)component->data);

            if (component->descriptor.kind == ComponentBuiltin && component->descriptor.name && strcmp(component->descriptor.name, "AnimatedSprite") == 0)
                animated_sprite_component_dispose((AnimatedSpriteState *)component->data);

            if (component->descriptor.kind == ComponentBuiltin && component->descriptor.name && strcmp(component->descriptor.name, "Camera") == 0)
                camera_component_dispose((CameraComponentData *)component->data);

            if (component->descriptor.kind == ComponentBuiltin && component->descriptor.name && strcmp(component->descriptor.name, "Collider") == 0)
                collider_component_dispose((ColliderComponentData *)component->data);

            if (component->descriptor.kind == ComponentBuiltin && component->descriptor.name && strcmp(component->descriptor.name, "Rigidbody") == 0)
                rigidbody_component_dispose((RigidbodyComponentData *)component->data);

            if (component->descriptor.kind == ComponentBuiltin && component->descriptor.name && strcmp(component->descriptor.name, "PointLight") == 0)
                point_light_component_dispose((PointLightComponentData *)component->data);

            if (component->descriptor.kind == ComponentBuiltin && component->descriptor.name && strcmp(component->descriptor.name, "SpotLight") == 0)
                spot_light_component_dispose((SpotLightComponentData *)component->data);

            if (component->descriptor.kind == ComponentBuiltin && component->descriptor.name && strcmp(component->descriptor.name, "DirectionLight") == 0)
                direction_light_component_dispose((DirectionLightComponentData *)component->data);
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

static result read_color_rgb_or_rgba(toml_datum_t table, const char *key, Color *out_color) {
    if (!key || key[0] == '\0' || !out_color)
        return Err;

    toml_datum_t color = toml_get(table, key);
    if (color.type != TOML_ARRAY)
        return Err;

    if (color.u.arr.size < 3 || color.u.arr.size > 4)
        return Err;

    for (int channel_index = 0; channel_index < color.u.arr.size; ++channel_index) {
        if (color.u.arr.elem[channel_index].type != TOML_INT64)
            return Err;
    }

    out_color->r = (unsigned char)color.u.arr.elem[0].u.int64;
    out_color->g = (unsigned char)color.u.arr.elem[1].u.int64;
    out_color->b = (unsigned char)color.u.arr.elem[2].u.int64;
    out_color->a = (unsigned char)(color.u.arr.size == 4 ? color.u.arr.elem[3].u.int64 : 255);
    return Ok;
}

static const toml_datum_t *find_point_light_component_table(toml_datum_t actor_table, toml_datum_t *out_point_light_table) {
    if (out_point_light_table)
        *out_point_light_table = (toml_datum_t){0};

    if (actor_table.type != TOML_TABLE || !out_point_light_table)
        return Null;

    toml_datum_t overrides = toml_get(actor_table, "Overrides");
    if (overrides.type == TOML_TABLE) {
        toml_datum_t components = toml_get(overrides, "Components");
        if (components.type == TOML_TABLE) {
            toml_datum_t point_light = toml_get(components, "PointLight");
            if (point_light.type == TOML_TABLE) {
                *out_point_light_table = point_light;
                return out_point_light_table;
            }
        }
    }

    toml_datum_t components = toml_get(actor_table, "Components");
    if (components.type == TOML_TABLE) {
        toml_datum_t point_light = toml_get(components, "PointLight");
        if (point_light.type == TOML_TABLE) {
            *out_point_light_table = point_light;
            return out_point_light_table;
        }
    }

    return Null;
}

static const toml_datum_t *find_spot_light_component_table(toml_datum_t actor_table, toml_datum_t *out_spot_light_table) {
    if (out_spot_light_table)
        *out_spot_light_table = (toml_datum_t){0};

    if (actor_table.type != TOML_TABLE || !out_spot_light_table)
        return Null;

    toml_datum_t overrides = toml_get(actor_table, "Overrides");
    if (overrides.type == TOML_TABLE) {
        toml_datum_t components = toml_get(overrides, "Components");
        if (components.type == TOML_TABLE) {
            toml_datum_t spot_light = toml_get(components, "SpotLight");
            if (spot_light.type == TOML_TABLE) {
                *out_spot_light_table = spot_light;
                return out_spot_light_table;
            }
        }
    }

    toml_datum_t components = toml_get(actor_table, "Components");
    if (components.type == TOML_TABLE) {
        toml_datum_t spot_light = toml_get(components, "SpotLight");
        if (spot_light.type == TOML_TABLE) {
            *out_spot_light_table = spot_light;
            return out_spot_light_table;
        }
    }

    return Null;
}

static const toml_datum_t *find_direction_light_component_table(toml_datum_t actor_table, toml_datum_t *out_direction_light_table) {
    if (out_direction_light_table)
        *out_direction_light_table = (toml_datum_t){0};

    if (actor_table.type != TOML_TABLE || !out_direction_light_table)
        return Null;

    toml_datum_t overrides = toml_get(actor_table, "Overrides");
    if (overrides.type == TOML_TABLE) {
        toml_datum_t components = toml_get(overrides, "Components");
        if (components.type == TOML_TABLE) {
            toml_datum_t direction_light = toml_get(components, "DirectionLight");
            if (direction_light.type != TOML_TABLE)
                direction_light = toml_get(components, "DirectionalLight");

            if (direction_light.type == TOML_TABLE) {
                *out_direction_light_table = direction_light;
                return out_direction_light_table;
            }
        }
    }

    toml_datum_t components = toml_get(actor_table, "Components");
    if (components.type == TOML_TABLE) {
        toml_datum_t direction_light = toml_get(components, "DirectionLight");
        if (direction_light.type != TOML_TABLE)
            direction_light = toml_get(components, "DirectionalLight");

        if (direction_light.type == TOML_TABLE) {
            *out_direction_light_table = direction_light;
            return out_direction_light_table;
        }
    }

    return Null;
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

static const toml_datum_t *find_collider_component_table(toml_datum_t actor_table, toml_datum_t *out_collider_table) {
    if (out_collider_table)
        *out_collider_table = (toml_datum_t){0};

    if (actor_table.type != TOML_TABLE || !out_collider_table)
        return Null;

    toml_datum_t overrides = toml_get(actor_table, "Overrides");
    if (overrides.type == TOML_TABLE) {
        toml_datum_t components = toml_get(overrides, "Components");
        if (components.type == TOML_TABLE) {
            toml_datum_t collider = toml_get(components, "Collider");
            if (collider.type == TOML_TABLE) {
                *out_collider_table = collider;
                return out_collider_table;
            }
        }
    }

    toml_datum_t components = toml_get(actor_table, "Components");
    if (components.type == TOML_TABLE) {
        toml_datum_t collider = toml_get(components, "Collider");
        if (collider.type == TOML_TABLE) {
            *out_collider_table = collider;
            return out_collider_table;
        }
    }

    return Null;
}

static const toml_datum_t *find_rigidbody_component_table(toml_datum_t actor_table, toml_datum_t *out_rigidbody_table) {
    if (out_rigidbody_table)
        *out_rigidbody_table = (toml_datum_t){0};

    if (actor_table.type != TOML_TABLE || !out_rigidbody_table)
        return Null;

    toml_datum_t overrides = toml_get(actor_table, "Overrides");
    if (overrides.type == TOML_TABLE) {
        toml_datum_t components = toml_get(overrides, "Components");
        if (components.type == TOML_TABLE) {
            toml_datum_t rigidbody = toml_get(components, "Rigidbody");
            if (rigidbody.type == TOML_TABLE) {
                *out_rigidbody_table = rigidbody;
                return out_rigidbody_table;
            }
        }
    }

    toml_datum_t components = toml_get(actor_table, "Components");
    if (components.type == TOML_TABLE) {
        toml_datum_t rigidbody = toml_get(components, "Rigidbody");
        if (rigidbody.type == TOML_TABLE) {
            *out_rigidbody_table = rigidbody;
            return out_rigidbody_table;
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

static boolean script_module_exists(const ScriptModuleBinding *modules, usize module_count, const char *candidate) {
    if (!modules || !candidate || candidate[0] == '\0')
        return False;

    for (usize index = 0; index < module_count; ++index) {
        const char *existing = modules[index].module_path;
        if (existing && strcmp(existing, candidate) == 0)
            return True;
    }

    return False;
}

static result parse_script_module_entry(toml_datum_t entry, const char **out_module, const char **out_alias) {
    if (!out_module || !out_alias)
        return Err;

    *out_module = Null;
    *out_alias = Null;

    if (entry.type == TOML_STRING) {
        if (!entry.u.s || entry.u.s[0] == '\0')
            return Err;

        *out_module = entry.u.s;
        return Ok;
    }

    if (entry.type == TOML_ARRAY) {
        if (entry.u.arr.size < 1 || entry.u.arr.size > 2)
            return Err;

        toml_datum_t module = entry.u.arr.elem[0];
        if (module.type != TOML_STRING || !module.u.s || module.u.s[0] == '\0')
            return Err;

        *out_module = module.u.s;

        if (entry.u.arr.size == 2) {
            toml_datum_t alias = entry.u.arr.elem[1];
            if (alias.type != TOML_STRING || !alias.u.s || alias.u.s[0] == '\0')
                return Err;

            *out_alias = alias.u.s;
        }

        return Ok;
    }

    return Err;
}

static result collect_script_modules_from_table(toml_datum_t script_table, ScriptModuleBinding *out_modules, usize max_modules, usize *out_module_count) {
    if (!out_modules || !out_module_count || max_modules == 0)
        return Err;

    *out_module_count = 0;
    if (script_table.type != TOML_TABLE)
        return Ok;

    toml_datum_t modules = toml_get(script_table, "modules");
    if (modules.type == TOML_ARRAY) {
        for (int index = 0; index < modules.u.arr.size; ++index) {
            const char *module_path = Null;
            const char *module_alias = Null;
            if (parse_script_module_entry(modules.u.arr.elem[index], &module_path, &module_alias) != Ok)
                return Err;

            if (script_module_exists(out_modules, *out_module_count, module_path))
                continue;

            if (*out_module_count >= max_modules)
                return Err;

            out_modules[*out_module_count].module_path = module_path;
            out_modules[*out_module_count].module_alias = module_alias;
            (*out_module_count)++;
        }
    } else if (modules.type != TOML_UNKNOWN) {
        return Err;
    }

    toml_datum_t module = toml_get(script_table, "module");
    if (module.type == TOML_STRING || module.type == TOML_ARRAY) {
        const char *module_path = Null;
        const char *module_alias = Null;
        if (parse_script_module_entry(module, &module_path, &module_alias) != Ok)
            return Err;

        if (!script_module_exists(out_modules, *out_module_count, module_path)) {
            if (*out_module_count >= max_modules)
                return Err;

            out_modules[*out_module_count].module_path = module_path;
            out_modules[*out_module_count].module_alias = module_alias;
            (*out_module_count)++;
        }
    } else if (module.type != TOML_UNKNOWN) {
        return Err;
    }

    return Ok;
}

static result find_script_modules(toml_datum_t actor_table, ScriptModuleBinding *out_modules, usize max_modules, usize *out_module_count) {
    if (!out_modules || !out_module_count || max_modules == 0)
        return Err;

    *out_module_count = 0;
    if (actor_table.type != TOML_TABLE)
        return Ok;

    toml_datum_t overrides = toml_get(actor_table, "Overrides");
    if (overrides.type == TOML_TABLE) {
        toml_datum_t components = toml_get(overrides, "Components");
        if (components.type == TOML_TABLE) {
            toml_datum_t script = toml_get(components, "Script");
            if (script.type == TOML_TABLE) {
                if (collect_script_modules_from_table(script, out_modules, max_modules, out_module_count) != Ok)
                    return Err;

                if (*out_module_count > 0)
                    return Ok;
            }
        }
    }

    toml_datum_t components = toml_get(actor_table, "Components");
    if (components.type == TOML_TABLE) {
        toml_datum_t script = toml_get(components, "Script");
        if (script.type == TOML_TABLE) {
            if (collect_script_modules_from_table(script, out_modules, max_modules, out_module_count) != Ok)
                return Err;
        }
    }

    return Ok;
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

static result assign_actor_tags_from_array(Actor *actor, toml_datum_t tags_array) {
    if (!actor)
        return Err;

    actor->tag_count = 0;
    if (tags_array.type == TOML_UNKNOWN)
        return Ok;

    if (tags_array.type != TOML_ARRAY)
        return Err;

    for (int index = 0; index < tags_array.u.arr.size; ++index) {
        toml_datum_t tag_value = tags_array.u.arr.elem[index];
        if (tag_value.type != TOML_STRING || !tag_value.u.s || tag_value.u.s[0] == '\0')
            return Err;

        boolean duplicate = False;
        for (usize tag_index = 0; tag_index < actor->tag_count; ++tag_index) {
            if (strcmp(actor->tags[tag_index], tag_value.u.s) == 0) {
                duplicate = True;
                break;
            }
        }

        if (duplicate)
            continue;

        if (actor->tag_count >= ActorMaxTags)
            return Err;

        if (snprintf(actor->tags[actor->tag_count], ActorMaxTagLength, "%s", tag_value.u.s) >= ActorMaxTagLength)
            return Err;

        actor->tag_count++;
    }

    return Ok;
}

static result read_actor_tags(toml_datum_t actor_table, Actor *actor) {
    if (!actor || actor_table.type != TOML_TABLE)
        return Err;

    toml_datum_t tags = toml_get(actor_table, "tags");
    if (tags.type != TOML_UNKNOWN)
        return assign_actor_tags_from_array(actor, tags);

    toml_datum_t defaults = toml_get(actor_table, "Defaults");
    if (defaults.type == TOML_TABLE) {
        toml_datum_t default_tags = toml_get(defaults, "tags");
        return assign_actor_tags_from_array(actor, default_tags);
    }

    actor->tag_count = 0;
    return Ok;
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

    ScriptModuleBinding script_modules[RuntimeMaxActorScriptComponents] = {0};
    usize script_module_count = 0;
    if (find_script_modules(actor_table, script_modules, RuntimeMaxActorScriptComponents, &script_module_count) != Ok) {
        log_err("Actor '%s' has invalid Script component; expected module=<string|[module,alias]> or modules=[<string|[module,alias]>, ...]", actor_id);
        return Err;
    }

    if (read_actor_tags(actor_table, actor) != Ok) {
        log_err("Actor '%s' has invalid tags; expected tags=[\"tag\", ...]", actor_id);
        return Err;
    }

    toml_datum_t parent_id = toml_get(actor_table, "parent");
    if (parent_id.type != TOML_UNKNOWN) {
        if (parent_id.type != TOML_STRING || !parent_id.u.s || parent_id.u.s[0] == '\0') {
            log_err("Actor '%s' has invalid parent; expected non-empty string actor id", actor_id);
            return Err;
        }

        if (actor_set_parent(actor, parent_id.u.s) != Ok) {
            log_err("Actor '%s' has parent id that is too long", actor_id);
            return Err;
        }
    } else {
        (void)actor_set_parent(actor, Null);
    }

    for (usize script_index = 0; script_index < script_module_count; ++script_index) {
        const char *module = script_modules[script_index].module_path;
        const char *module_alias = script_modules[script_index].module_alias;
        if (!module || module[0] == '\0')
            continue;

        void *script_state = script_component_state_create(module, module_alias);
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

    toml_datum_t collider_component_table = {0};
    if (find_collider_component_table(actor_table, &collider_component_table)) {
        Heap collider_heap = allocate(1, sizeof(ColliderComponentData));
        ColliderComponentData *collider_state = (ColliderComponentData *)collider_heap.pointer;
        if (!collider_state) {
            log_err("Failed to allocate collider state for actor '%s'", actor_id);
            return Err;
        }

        memset(collider_state, 0, sizeof(*collider_state));
        collider_state->heap = collider_heap;
        collider_state->offset = (Vector2){0.0f, 0.0f};
        collider_state->size = (Vector2){16.0f, 16.0f};
        collider_state->enabled = True;
        collider_state->is_trigger = False;

        toml_datum_t offset = toml_get(collider_component_table, "offset");
        if (offset.type != TOML_UNKNOWN && read_xy_array(collider_component_table, "offset", &collider_state->offset) != Ok) {
            collider_component_dispose(collider_state);
            log_err("Actor '%s' has invalid Collider.offset; expected [x, y]", actor_id);
            return Err;
        }

        toml_datum_t size = toml_get(collider_component_table, "size");
        if (size.type != TOML_UNKNOWN && read_xy_array(collider_component_table, "size", &collider_state->size) != Ok) {
            collider_component_dispose(collider_state);
            log_err("Actor '%s' has invalid Collider.size; expected [width, height]", actor_id);
            return Err;
        }

        if (collider_state->size.x <= 0.0f || collider_state->size.y <= 0.0f) {
            collider_component_dispose(collider_state);
            log_err("Actor '%s' has invalid Collider.size; width and height must be > 0", actor_id);
            return Err;
        }

        toml_datum_t enabled = toml_get(collider_component_table, "enabled");
        if (enabled.type == TOML_BOOLEAN)
            collider_state->enabled = enabled.u.boolean ? True : False;

        toml_datum_t is_trigger = toml_get(collider_component_table, "is_trigger");
        if (is_trigger.type == TOML_BOOLEAN)
            collider_state->is_trigger = is_trigger.u.boolean ? True : False;

        toml_datum_t trigger = toml_get(collider_component_table, "trigger");
        if (trigger.type == TOML_BOOLEAN)
            collider_state->is_trigger = trigger.u.boolean ? True : False;

        ComponentDescriptor descriptor = {
            .name = "Collider",
            .kind = ComponentBuiltin,
            .initialize = collider_component_initialize,
            .context = Null,
        };

        if (actor_add_component(actor, descriptor, collider_state) != Ok) {
            collider_component_dispose(collider_state);
            log_err("Failed to add collider component to actor '%s'", actor_id);
            return Err;
        }
    }

    toml_datum_t rigidbody_component_table = {0};
    if (find_rigidbody_component_table(actor_table, &rigidbody_component_table)) {
        Heap rigidbody_heap = allocate(1, sizeof(RigidbodyComponentData));
        RigidbodyComponentData *rigidbody_state = (RigidbodyComponentData *)rigidbody_heap.pointer;
        if (!rigidbody_state) {
            log_err("Failed to allocate rigidbody state for actor '%s'", actor_id);
            return Err;
        }

        memset(rigidbody_state, 0, sizeof(*rigidbody_state));
        rigidbody_state->heap = rigidbody_heap;
        rigidbody_state->body_type = RigidbodyBodyTypeDynamic;
        rigidbody_state->simulated = True;
        rigidbody_state->use_gravity = True;
        rigidbody_state->mass = 1.0f;
        rigidbody_state->inverse_mass = 1.0f;
        rigidbody_state->gravity_scale = 1.0f;
        rigidbody_state->linear_drag = 0.0f;
        rigidbody_state->angular_drag = 0.05f;
        rigidbody_state->velocity_x = 0.0f;
        rigidbody_state->velocity_y = 0.0f;
        rigidbody_state->angular_velocity = 0.0f;
        rigidbody_state->force_x = 0.0f;
        rigidbody_state->force_y = 0.0f;
        rigidbody_state->torque = 0.0f;
        rigidbody_state->freeze_position_x = False;
        rigidbody_state->freeze_position_y = False;
        rigidbody_state->freeze_rotation = True;
        rigidbody_state->is_grounded = False;

        toml_datum_t body_type = toml_get(rigidbody_component_table, "body_type");
        if (body_type.type == TOML_STRING && body_type.u.s && body_type.u.s[0] != '\0') {
            if (rigidbody_body_type_from_string(body_type.u.s, &rigidbody_state->body_type) != Ok) {
                rigidbody_component_dispose(rigidbody_state);
                log_err("Actor '%s' has invalid Rigidbody.body_type; expected dynamic|kinematic|static", actor_id);
                return Err;
            }
        }

        toml_datum_t is_kinematic = toml_get(rigidbody_component_table, "is_kinematic");
        if (is_kinematic.type == TOML_BOOLEAN && is_kinematic.u.boolean)
            rigidbody_state->body_type = RigidbodyBodyTypeKinematic;

        toml_datum_t simulated = toml_get(rigidbody_component_table, "simulated");
        if (simulated.type == TOML_BOOLEAN)
            rigidbody_state->simulated = simulated.u.boolean ? True : False;

        toml_datum_t use_gravity = toml_get(rigidbody_component_table, "use_gravity");
        if (use_gravity.type == TOML_BOOLEAN)
            rigidbody_state->use_gravity = use_gravity.u.boolean ? True : False;

        float number_value = 0.0f;
        toml_datum_t mass = toml_get(rigidbody_component_table, "mass");
        if (toml_number_to_float(mass, &number_value) && number_value > 0.0f)
            rigidbody_state->mass = number_value;

        toml_datum_t gravity_scale = toml_get(rigidbody_component_table, "gravity_scale");
        if (toml_number_to_float(gravity_scale, &number_value))
            rigidbody_state->gravity_scale = number_value;

        toml_datum_t linear_drag = toml_get(rigidbody_component_table, "linear_drag");
        if (toml_number_to_float(linear_drag, &number_value) && number_value >= 0.0f)
            rigidbody_state->linear_drag = number_value;

        toml_datum_t drag = toml_get(rigidbody_component_table, "drag");
        if (toml_number_to_float(drag, &number_value) && number_value >= 0.0f)
            rigidbody_state->linear_drag = number_value;

        toml_datum_t angular_drag = toml_get(rigidbody_component_table, "angular_drag");
        if (toml_number_to_float(angular_drag, &number_value) && number_value >= 0.0f)
            rigidbody_state->angular_drag = number_value;

        toml_datum_t velocity = toml_get(rigidbody_component_table, "velocity");
        Vector2 parsed_velocity = {0.0f, 0.0f};
        if (velocity.type != TOML_UNKNOWN && read_xy_array(rigidbody_component_table, "velocity", &parsed_velocity) != Ok) {
            rigidbody_component_dispose(rigidbody_state);
            log_err("Actor '%s' has invalid Rigidbody.velocity; expected [x, y]", actor_id);
            return Err;
        }
        rigidbody_state->velocity_x = parsed_velocity.x;
        rigidbody_state->velocity_y = parsed_velocity.y;

        toml_datum_t angular_velocity = toml_get(rigidbody_component_table, "angular_velocity");
        if (toml_number_to_float(angular_velocity, &number_value))
            rigidbody_state->angular_velocity = number_value;

        toml_datum_t freeze_position = toml_get(rigidbody_component_table, "freeze_position");
        if (freeze_position.type == TOML_ARRAY && freeze_position.u.arr.size >= 2) {
            if (freeze_position.u.arr.elem[0].type == TOML_BOOLEAN)
                rigidbody_state->freeze_position_x = freeze_position.u.arr.elem[0].u.boolean ? True : False;
            if (freeze_position.u.arr.elem[1].type == TOML_BOOLEAN)
                rigidbody_state->freeze_position_y = freeze_position.u.arr.elem[1].u.boolean ? True : False;
        }

        toml_datum_t freeze_rotation = toml_get(rigidbody_component_table, "freeze_rotation");
        if (freeze_rotation.type == TOML_BOOLEAN)
            rigidbody_state->freeze_rotation = freeze_rotation.u.boolean ? True : False;

        ComponentDescriptor descriptor = {
            .name = "Rigidbody",
            .kind = ComponentBuiltin,
            .initialize = rigidbody_component_initialize,
            .context = Null,
        };

        if (actor_add_component(actor, descriptor, rigidbody_state) != Ok) {
            rigidbody_component_dispose(rigidbody_state);
            log_err("Failed to add rigidbody component to actor '%s'", actor_id);
            return Err;
        }
    }

    toml_datum_t point_light_component_table = {0};
    if (find_point_light_component_table(actor_table, &point_light_component_table)) {
        Heap point_light_heap = allocate(1, sizeof(PointLightComponentData));
        PointLightComponentData *point_light_state = (PointLightComponentData *)point_light_heap.pointer;
        if (!point_light_state) {
            log_err("Failed to allocate point light state for actor '%s'", actor_id);
            return Err;
        }

        memset(point_light_state, 0, sizeof(*point_light_state));
        point_light_state->heap = point_light_heap;
        point_light_state->actor = actor;
        point_light_state->position = (Vector3){0.0f, 0.0f, 0.0f};
        point_light_state->color = WHITE;
        point_light_state->intensity = 1.0f;
        point_light_state->range = 8.0f;
        point_light_state->enabled = True;

        toml_datum_t position = toml_get(point_light_component_table, "position");
        if (position.type != TOML_UNKNOWN && read_vector3_from_table(point_light_component_table, "position", &point_light_state->position) != Ok) {
            point_light_component_dispose(point_light_state);
            log_err("Actor '%s' has invalid PointLight.position; expected [x, y, z]", actor_id);
            return Err;
        }

        toml_datum_t color = toml_get(point_light_component_table, "color");
        if (color.type != TOML_UNKNOWN && read_color_rgb_or_rgba(point_light_component_table, "color", &point_light_state->color) != Ok) {
            point_light_component_dispose(point_light_state);
            log_err("Actor '%s' has invalid PointLight.color; expected [r, g, b] or [r, g, b, a]", actor_id);
            return Err;
        }

        float number_value = 0.0f;
        toml_datum_t intensity = toml_get(point_light_component_table, "intensity");
        if (toml_number_to_float(intensity, &number_value) && number_value >= 0.0f)
            point_light_state->intensity = number_value;

        toml_datum_t range = toml_get(point_light_component_table, "range");
        if (toml_number_to_float(range, &number_value) && number_value > 0.0f)
            point_light_state->range = number_value;

        toml_datum_t enabled = toml_get(point_light_component_table, "enabled");
        if (enabled.type == TOML_BOOLEAN)
            point_light_state->enabled = enabled.u.boolean ? True : False;

        ComponentDescriptor descriptor = {
            .name = "PointLight",
            .kind = ComponentBuiltin,
            .initialize = point_light_component_initialize,
            .context = Null,
        };

        if (actor_add_component(actor, descriptor, point_light_state) != Ok) {
            point_light_component_dispose(point_light_state);
            log_err("Failed to add point light component to actor '%s'", actor_id);
            return Err;
        }
    }

    toml_datum_t spot_light_component_table = {0};
    if (find_spot_light_component_table(actor_table, &spot_light_component_table)) {
        Heap spot_light_heap = allocate(1, sizeof(SpotLightComponentData));
        SpotLightComponentData *spot_light_state = (SpotLightComponentData *)spot_light_heap.pointer;
        if (!spot_light_state) {
            log_err("Failed to allocate spot light state for actor '%s'", actor_id);
            return Err;
        }

        memset(spot_light_state, 0, sizeof(*spot_light_state));
        spot_light_state->heap = spot_light_heap;
        spot_light_state->actor = actor;
        spot_light_state->position = (Vector3){0.0f, 0.0f, 0.0f};
        spot_light_state->direction = (Vector3){0.0f, -1.0f, 0.0f};
        spot_light_state->color = WHITE;
        spot_light_state->intensity = 1.0f;
        spot_light_state->range = 12.0f;
        spot_light_state->angle = 35.0f;
        spot_light_state->enabled = True;

        toml_datum_t position = toml_get(spot_light_component_table, "position");
        if (position.type != TOML_UNKNOWN && read_vector3_from_table(spot_light_component_table, "position", &spot_light_state->position) != Ok) {
            spot_light_component_dispose(spot_light_state);
            log_err("Actor '%s' has invalid SpotLight.position; expected [x, y, z]", actor_id);
            return Err;
        }

        toml_datum_t direction = toml_get(spot_light_component_table, "direction");
        if (direction.type != TOML_UNKNOWN && read_vector3_from_table(spot_light_component_table, "direction", &spot_light_state->direction) != Ok) {
            spot_light_component_dispose(spot_light_state);
            log_err("Actor '%s' has invalid SpotLight.direction; expected [x, y, z]", actor_id);
            return Err;
        }

        if (fabsf(spot_light_state->direction.x) < 0.0001f && fabsf(spot_light_state->direction.y) < 0.0001f && fabsf(spot_light_state->direction.z) < 0.0001f) {
            spot_light_component_dispose(spot_light_state);
            log_err("Actor '%s' has invalid SpotLight.direction; zero vector is not allowed", actor_id);
            return Err;
        }

        toml_datum_t color = toml_get(spot_light_component_table, "color");
        if (color.type != TOML_UNKNOWN && read_color_rgb_or_rgba(spot_light_component_table, "color", &spot_light_state->color) != Ok) {
            spot_light_component_dispose(spot_light_state);
            log_err("Actor '%s' has invalid SpotLight.color; expected [r, g, b] or [r, g, b, a]", actor_id);
            return Err;
        }

        float number_value = 0.0f;
        toml_datum_t intensity = toml_get(spot_light_component_table, "intensity");
        if (toml_number_to_float(intensity, &number_value) && number_value >= 0.0f)
            spot_light_state->intensity = number_value;

        toml_datum_t range = toml_get(spot_light_component_table, "range");
        if (toml_number_to_float(range, &number_value) && number_value > 0.0f)
            spot_light_state->range = number_value;

        toml_datum_t angle = toml_get(spot_light_component_table, "angle");
        if (!toml_number_to_float(angle, &number_value)) {
            toml_datum_t spot_angle = toml_get(spot_light_component_table, "spot_angle");
            (void)toml_number_to_float(spot_angle, &number_value);
        }
        if (number_value > 0.0f)
            spot_light_state->angle = number_value;

        toml_datum_t enabled = toml_get(spot_light_component_table, "enabled");
        if (enabled.type == TOML_BOOLEAN)
            spot_light_state->enabled = enabled.u.boolean ? True : False;

        ComponentDescriptor descriptor = {
            .name = "SpotLight",
            .kind = ComponentBuiltin,
            .initialize = spot_light_component_initialize,
            .context = Null,
        };

        if (actor_add_component(actor, descriptor, spot_light_state) != Ok) {
            spot_light_component_dispose(spot_light_state);
            log_err("Failed to add spot light component to actor '%s'", actor_id);
            return Err;
        }
    }

    toml_datum_t direction_light_component_table = {0};
    if (find_direction_light_component_table(actor_table, &direction_light_component_table)) {
        Heap direction_light_heap = allocate(1, sizeof(DirectionLightComponentData));
        DirectionLightComponentData *direction_light_state = (DirectionLightComponentData *)direction_light_heap.pointer;
        if (!direction_light_state) {
            log_err("Failed to allocate direction light state for actor '%s'", actor_id);
            return Err;
        }

        memset(direction_light_state, 0, sizeof(*direction_light_state));
        direction_light_state->heap = direction_light_heap;
        direction_light_state->actor = actor;
        direction_light_state->direction = (Vector3){0.0f, -1.0f, 0.0f};
        direction_light_state->color = WHITE;
        direction_light_state->intensity = 1.0f;
        direction_light_state->enabled = True;

        toml_datum_t direction = toml_get(direction_light_component_table, "direction");
        if (direction.type != TOML_UNKNOWN && read_vector3_from_table(direction_light_component_table, "direction", &direction_light_state->direction) != Ok) {
            direction_light_component_dispose(direction_light_state);
            log_err("Actor '%s' has invalid DirectionLight.direction; expected [x, y, z]", actor_id);
            return Err;
        }

        if (fabsf(direction_light_state->direction.x) < 0.0001f && fabsf(direction_light_state->direction.y) < 0.0001f && fabsf(direction_light_state->direction.z) < 0.0001f) {
            direction_light_component_dispose(direction_light_state);
            log_err("Actor '%s' has invalid DirectionLight.direction; zero vector is not allowed", actor_id);
            return Err;
        }

        toml_datum_t color = toml_get(direction_light_component_table, "color");
        if (color.type != TOML_UNKNOWN && read_color_rgb_or_rgba(direction_light_component_table, "color", &direction_light_state->color) != Ok) {
            direction_light_component_dispose(direction_light_state);
            log_err("Actor '%s' has invalid DirectionLight.color; expected [r, g, b] or [r, g, b, a]", actor_id);
            return Err;
        }

        float number_value = 0.0f;
        toml_datum_t intensity = toml_get(direction_light_component_table, "intensity");
        if (toml_number_to_float(intensity, &number_value) && number_value >= 0.0f)
            direction_light_state->intensity = number_value;

        toml_datum_t enabled = toml_get(direction_light_component_table, "enabled");
        if (enabled.type == TOML_BOOLEAN)
            direction_light_state->enabled = enabled.u.boolean ? True : False;

        ComponentDescriptor descriptor = {
            .name = "DirectionLight",
            .kind = ComponentBuiltin,
            .initialize = direction_light_component_initialize,
            .context = Null,
        };

        if (actor_add_component(actor, descriptor, direction_light_state) != Ok) {
            direction_light_component_dispose(direction_light_state);
            log_err("Failed to add direction light component to actor '%s'", actor_id);
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

        toml_datum_t animated_material = toml_get(animated_sprite_table, "material");
        if (animated_material.type == TOML_STRING && animated_material.u.s && animated_material.u.s[0] != '\0') {
            if (snprintf(animated_sprite_state->material_id, sizeof(animated_sprite_state->material_id), "%s", animated_material.u.s) >= (int)sizeof(animated_sprite_state->material_id)) {
                animated_sprite_component_dispose(animated_sprite_state);
                log_err("Animated sprite material id is too long for actor '%s'", actor_id);
                return Err;
            }
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

        toml_datum_t static_material = toml_get(static_sprite_table, "material");
        if (static_material.type == TOML_STRING && static_material.u.s && static_material.u.s[0] != '\0') {
            if (snprintf(sprite_state->material_id, sizeof(sprite_state->material_id), "%s", static_material.u.s) >= (int)sizeof(sprite_state->material_id)) {
                static_sprite_component_dispose(sprite_state);
                log_err("Static sprite material id is too long for actor '%s'", actor_id);
                return Err;
            }
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

    toml_datum_t static_color_table = {0};
    if (find_static_color_component_table(actor_table, &static_color_table)) {
        Heap color_heap = allocate(1, sizeof(StaticColorState));
        StaticColorState *color_state = (StaticColorState *)color_heap.pointer;
        if (!color_state) {
            log_err("Failed to allocate static color state for actor '%s'", actor_id);
            return Err;
        }

        memset(color_state, 0, sizeof(*color_state));
        color_state->heap = color_heap;
        color_state->actor = actor;
        color_state->position = (Vector2){0.0f, 0.0f};
        color_state->anchor = (SpriteAnchor){
            .center = False,
            .offset = (Vector2){0.0f, 0.0f},
        };
        color_state->size = (Vector2){64.0f, 64.0f};
        color_state->rotation = 0.0f;
        color_state->color = WHITE;

        toml_datum_t transform = toml_get(actor_table, "Transform");
        (void)read_xy_array(transform, "position", &color_state->position);
        if (read_actor_transform_anchor(actor_table, prefab_refs, prefabs_root, &color_state->anchor) != Ok) {
            static_color_component_dispose(color_state);
            log_err("Actor '%s' has invalid Transform.anchor; expected [x, y] or \"center\" (actor or prefab Transform)", actor_id);
            return Err;
        }

        (void)read_xy_array(static_color_table, "position", &color_state->position);

        toml_datum_t size = toml_get(static_color_table, "size");
        if (size.type != TOML_UNKNOWN)
            (void)read_xy_array(static_color_table, "size", &color_state->size);
        if (color_state->size.x <= 0.0f || color_state->size.y <= 0.0f) {
            static_color_component_dispose(color_state);
            log_err("Actor '%s' has invalid StaticColor.size; both dimensions must be > 0", actor_id);
            return Err;
        }

        toml_datum_t rotation = toml_get(static_color_table, "rotation");
        float rotation_value = 0.0f;
        if (toml_number_to_float(rotation, &rotation_value))
            color_state->rotation = rotation_value;

        toml_datum_t anchor = toml_get(static_color_table, "anchor");
        if (anchor.type != TOML_UNKNOWN && read_transform_anchor(static_color_table, &color_state->anchor) != Ok) {
            static_color_component_dispose(color_state);
            log_err("Actor '%s' has invalid StaticColor.anchor; expected [x, y] or \"center\"", actor_id);
            return Err;
        }

        toml_datum_t color = toml_get(static_color_table, "color");
        if (color.type == TOML_ARRAY && color.u.arr.size >= 4 &&
            color.u.arr.elem[0].type == TOML_INT64 && color.u.arr.elem[1].type == TOML_INT64 &&
            color.u.arr.elem[2].type == TOML_INT64 && color.u.arr.elem[3].type == TOML_INT64) {
            color_state->color = (Color){
                (unsigned char)color.u.arr.elem[0].u.int64,
                (unsigned char)color.u.arr.elem[1].u.int64,
                (unsigned char)color.u.arr.elem[2].u.int64,
                (unsigned char)color.u.arr.elem[3].u.int64,
            };
        }

        ComponentDescriptor descriptor = {
            .name = "StaticColor",
            .kind = ComponentBuiltin,
            .initialize = static_color_component_initialize,
            .context = Null,
        };

        if (actor_add_component(actor, descriptor, color_state) != Ok) {
            static_color_component_dispose(color_state);
            log_err("Failed to add static color component to actor '%s'", actor_id);
            return Err;
        }
    }

    if (actor_initialize_components(actor) != Ok)
        return Err;

    actor_capture_previous_transform(actor);

    if (actor->has_parent)
        log_msg("%s '%s' (parent='%s')", success_log_label, actor_id, actor->parent_id);
    else
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
    LightingSceneSelection resolved_lighting = {0};

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

    lighting_scene_selection_reset(&resolved_lighting);
    if (lighting_resolve_scene_selection(&runtime_state.lighting_global_config, scene_toml.toptab, &resolved_lighting) != Ok)
        goto fail;

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

    if (validate_actor_parent_links() != Ok)
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

    runtime_state.lighting_selection = resolved_lighting;
    apply_resolved_scene_lighting(&runtime_state.lighting_selection);

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

static void apply_resolved_scene_lighting(const LightingSceneSelection *selection) {
    if (!selection || !selection->has_lighting)
        return;

    runtime_state.scene_light_multiplier = selection->global_light_multiplier;
    if (runtime_state.scene_light_multiplier < 0.0f)
        runtime_state.scene_light_multiplier = 0.0f;

    if (selection->has_clear_color) {
        Color clear_color = {
            selection->clear_r,
            selection->clear_g,
            selection->clear_b,
            selection->clear_a,
        };
        set_window_clear_color(clear_color);
    }

    log_msg(
        "Applied lighting schema '%s' from '%s'%s (global_multiplier=%.2f)",
        selection->schema_name,
        selection->file_ref,
        selection->has_blend ? " (blend requested)" : "",
        runtime_state.scene_light_multiplier);
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

    if (!project_path || project_path[0] == '\0') {
        log_err("Project path is missing");
        return Err;
    }

    if (vfs_mount_project(project_path) != Ok) {
        log_err("Project path '%s' is not a valid directory or .targame archive", project_path ? project_path : "<null>");
        return Err;
    }

    usize conf_count = 0;
    if (validate_conf_files(project_path, &conf_count) != Ok) {
        vfs_unmount();
        return Err;
    }

    if (conf_count == 0) {
        log_err("No .conf files found under '%s'", project_path);
        vfs_unmount();
        return Err;
    }

    if (version_check_project_requirement(project_path, Version) != Ok) {
        vfs_unmount();
        return Err;
    }

    memset(&runtime_state, 0, sizeof(runtime_state));

    char springengine_config_path[PATH_MAX] = {0};
    if (join_path(project_path, "springengine.conf", springengine_config_path, sizeof(springengine_config_path)) != Ok) {
        log_err("Failed to resolve springengine.conf path for '%s'", project_path);
        vfs_unmount();
        return Err;
    }

    if (vfs_file_exists(springengine_config_path) != True) {
        log_err("Required config missing: '%s'", springengine_config_path);
        vfs_unmount();
        return Err;
    }

    WindowConfig window_config = DefaultWindowConfig;
    if (load_project_window_config(springengine_config_path, &window_config) != Ok) {
        log_err("Failed to load required window settings from '%s'", springengine_config_path);
        vfs_unmount();
        return Err;
    }

    if (parse_toml_file(springengine_config_path, &project_toml) != Ok) {
        vfs_unmount();
        return Err;
    }
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
    runtime_state.scene_light_multiplier = 1.0f;
    runtime_state.ui_runtime = Null;
    lighting_global_config_reset(&runtime_state.lighting_global_config);
    lighting_scene_selection_reset(&runtime_state.lighting_selection);
    shader_global_config_reset(&runtime_state.shader_global_config);
    shader_library_reset(&runtime_state.shader_library);
    material_library_reset(&runtime_state.material_library);
    postfx_reset();

    if (cache_autoload_actor_ids(project_toml.toptab) != Ok)
        goto fail;

    if (lighting_load_global_config(project_path, &runtime_state.lighting_global_config) != Ok)
        goto fail;

    if (shader_load_global_config(project_path, &runtime_state.shader_global_config) != Ok)
        goto fail;

    if (shader_library_load(&runtime_state.shader_global_config, &runtime_state.shader_library) != Ok)
        goto fail;

    if (material_library_load(&runtime_state.shader_global_config, &runtime_state.shader_library, &runtime_state.material_library) != Ok)
        goto fail;

    if (postfx_load_stack_config(&runtime_state.shader_global_config, &runtime_state.shader_library) != Ok)
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

    texture_cache_reset();
    shader_cache_reset();

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
    runtime_state.scene_light_multiplier = 1.0f;
    lighting_global_config_reset(&runtime_state.lighting_global_config);
    lighting_scene_selection_reset(&runtime_state.lighting_selection);
    shader_global_config_reset(&runtime_state.shader_global_config);
    shader_library_reset(&runtime_state.shader_library);
    material_library_reset(&runtime_state.material_library);
    postfx_reset();

    if (project_ok)
        toml_free(project_toml);

    vfs_unmount();
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

        texture_cache_reset();
        shader_cache_reset();

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
        runtime_state.scene_light_multiplier = 1.0f;
        lighting_global_config_reset(&runtime_state.lighting_global_config);
        lighting_scene_selection_reset(&runtime_state.lighting_selection);
        shader_global_config_reset(&runtime_state.shader_global_config);
        shader_library_reset(&runtime_state.shader_library);
        material_library_reset(&runtime_state.material_library);
        postfx_reset();
    }

    if (project_ok)
        toml_free(project_toml);

    vfs_unmount();
    return Err;
}

result validate_project_configs(const char *project_path) {
    toml_result_t project_toml = {0};
    boolean project_ok = False;
    Heap lighting_heap = NullHeap;
    Heap shader_library_heap = NullHeap;
    Heap material_library_heap = NullHeap;

    LightingGlobalConfig *lighting_global_config = Null;
    ShaderLibrary *shader_library = Null;
    MaterialLibrary *material_library = Null;

    if (!project_path || project_path[0] == '\0') {
        log_err("Project path is missing");
        return Err;
    }

    if (vfs_mount_project(project_path) != Ok) {
        log_err("Project path '%s' is not a valid directory or .targame archive", project_path);
        return Err;
    }

    usize conf_count = 0;
    if (validate_conf_files(project_path, &conf_count) != Ok)
        goto fail;

    if (conf_count == 0) {
        log_err("No .conf files found under '%s'", project_path);
        goto fail;
    }

    if (version_check_project_requirement(project_path, Version) != Ok)
        goto fail;

    char springengine_config_path[PATH_MAX] = {0};
    if (join_path(project_path, "springengine.conf", springengine_config_path, sizeof(springengine_config_path)) != Ok) {
        log_err("Failed to resolve springengine.conf path for '%s'", project_path);
        goto fail;
    }

    if (vfs_file_exists(springengine_config_path) != True) {
        log_err("Required config missing: '%s'", springengine_config_path);
        goto fail;
    }

    if (parse_toml_file(springengine_config_path, &project_toml) != Ok)
        goto fail;
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

    lighting_heap = allocate(1, sizeof(LightingGlobalConfig));
    lighting_global_config = (LightingGlobalConfig *)lighting_heap.pointer;
    if (!lighting_global_config)
        goto fail;
    memset(lighting_global_config, 0, sizeof(*lighting_global_config));

    if (lighting_load_global_config(project_path, lighting_global_config) != Ok)
        goto fail;

    ShaderGlobalConfig shader_global_config = {0};
    if (shader_load_global_config(project_path, &shader_global_config) != Ok)
        goto fail;

    shader_library_heap = allocate(1, sizeof(ShaderLibrary));
    shader_library = (ShaderLibrary *)shader_library_heap.pointer;
    if (!shader_library)
        goto fail;
    memset(shader_library, 0, sizeof(*shader_library));

    if (shader_library_load(&shader_global_config, shader_library) != Ok)
        goto fail;

    material_library_heap = allocate(1, sizeof(MaterialLibrary));
    material_library = (MaterialLibrary *)material_library_heap.pointer;
    if (!material_library)
        goto fail;
    memset(material_library, 0, sizeof(*material_library));

    if (material_library_load(&shader_global_config, shader_library, material_library) != Ok)
        goto fail;

    if (postfx_load_stack_config(&shader_global_config, shader_library) != Ok)
        goto fail;

    if (lighting_heap.pointer)
        deallocate(lighting_heap);
    if (shader_library_heap.pointer)
        deallocate(shader_library_heap);
    if (material_library_heap.pointer)
        deallocate(material_library_heap);
    postfx_reset();

    if (project_ok)
        toml_free(project_toml);
    vfs_unmount();

    log_msg("Validation passed for project '%s' (%lu .conf files)", project_path, conf_count);
    return Ok;

fail:
    if (lighting_heap.pointer)
        deallocate(lighting_heap);
    if (shader_library_heap.pointer)
        deallocate(shader_library_heap);
    if (material_library_heap.pointer)
        deallocate(material_library_heap);
    postfx_reset();
    if (project_ok)
        toml_free(project_toml);
    vfs_unmount();
    return Err;
}

result validate_project_version_requirement(const char *project_path) {
    if (!project_path || project_path[0] == '\0') {
        log_err("Project path is missing");
        return Err;
    }

    if (vfs_mount_project(project_path) != Ok) {
        log_err("Project path '%s' is not a valid directory or .targame archive", project_path);
        return Err;
    }

    if (version_check_project_requirement(project_path, Version) != Ok) {
        vfs_unmount();
        return Err;
    }

    log_msg("Version requirement validation passed for '%s'", project_path);
    vfs_unmount();
    return Ok;
}

boolean runtime_material_alias_exists(const char *material_alias) {
    if (!runtime_state.active)
        return False;

    return material_alias_exists_internal(material_alias);
}

result runtime_material_set_property(const char *material_alias, const char *property_name, float value) {
    RuntimeGuardActiveErr();

    if (!material_alias || material_alias[0] == '\0' || !property_name || property_name[0] == '\0')
        return Err;

    if (!material_alias_exists_internal(material_alias))
        return Err;

    const float normalized_value = normalize_material_property_value(property_name, value);
    const int existing_index = find_material_override_index(material_alias, property_name);
    if (existing_index >= 0) {
        runtime_material_overrides[existing_index].value = normalized_value;
        return Ok;
    }

    if (runtime_material_override_count >= RuntimeMaxMaterialOverrides) {
        log_err("Material override table is full; cannot set '%s.%s'", material_alias, property_name);
        return Err;
    }

    MaterialPropertyOverride *entry = &runtime_material_overrides[runtime_material_override_count++];
    memset(entry, 0, sizeof(*entry));

    if (snprintf(entry->material_alias, sizeof(entry->material_alias), "%s", material_alias) >= (int)sizeof(entry->material_alias)) {
        runtime_material_override_count--;
        return Err;
    }

    if (snprintf(entry->property_name, sizeof(entry->property_name), "%s", property_name) >= (int)sizeof(entry->property_name)) {
        runtime_material_override_count--;
        return Err;
    }

    entry->value = normalized_value;
    return Ok;
}

result runtime_material_get_property(const char *material_alias, const char *property_name, float *out_value) {
    RuntimeGuardActiveErr();

    if (!material_alias || material_alias[0] == '\0' || !property_name || property_name[0] == '\0' || !out_value)
        return Err;

    if (!material_alias_exists_internal(material_alias))
        return Err;

    if (material_override_get_float(material_alias, property_name, out_value))
        return Ok;

    const int pass_index = find_postfx_pass_index_by_alias(material_alias);
    if (pass_index >= 0) {
        const PostFxPassConfig *pass = &runtime_postfx_stack.passes[pass_index];
        if (postfx_pass_get_default_property(pass, property_name, out_value))
            return Ok;
    }

    return Err;
}
