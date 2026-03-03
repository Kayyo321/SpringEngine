#ifndef RUNTIME_LOADER_INTERNAL_H
#define RUNTIME_LOADER_INTERNAL_H

#include "runtime_loader.h"

#include "lighting_config.h"

#include "actor/actor.h"
#include "actor/camera_component.h"
#include "dj/dj.h"
#include "script/script_runtime.h"
#include "ui/ui_runtime.h"

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
#ifdef SPRINGENGINE_DEBUG
    boolean debug_show_collider_borders;
    usize debug_selected_collider_index;
#endif
    boolean has_pending_scene_load;
    usize autoload_actor_count;
    usize pending_prefab_instantiation_count;
    usize prefab_copy_counter;
    char project_root[PATH_MAX];
    float scene_light_multiplier;
    char scenes_root[PATH_MAX];
    char prefabs_root[PATH_MAX];
    char ui_root[PATH_MAX];
    char autoload_data_path[PATH_MAX];
    char current_scene_path[PATH_MAX];
    char pending_scene_path[PATH_MAX];
    LightingGlobalConfig lighting_global_config;
    LightingSceneSelection lighting_selection;
    char autoload_actor_ids[RuntimeMaxAutoloadActors][RuntimeMaxAutoloadActorIdLength];
    PendingPrefabInstantiation pending_prefab_instantiations[RuntimeMaxPendingPrefabInstantiations];
    UiRuntime *ui_runtime;
} RuntimeState;

extern RuntimeState runtime_state;

#define RUNTIME_GUARD_ACTIVE_ERR() do { \
    if (!runtime_state.active) \
        return Err; \
} while (False)

#define RUNTIME_GUARD_ACTIVE_NULL() do { \
    if (!runtime_state.active) \
        return Null; \
} while (False)

#define RUNTIME_GUARD_UI_ERR() do { \
    if (!runtime_state.active || !runtime_state.ui_runtime) \
        return Err; \
} while (False)

#define RUNTIME_GUARD_UI_NULL() do { \
    if (!runtime_state.active || !runtime_state.ui_runtime) \
        return Null; \
} while (False)

Actor *find_actor_by_id(const char *actor_id);
result enqueue_prefab_instantiation(const char *prefab_ref_id, boolean has_position, float x, float y, float z, char *out_actor_id, usize out_actor_id_size);

#endif // RUNTIME_LOADER_INTERNAL_H
