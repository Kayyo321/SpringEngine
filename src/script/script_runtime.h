#ifndef ScriptRuntimeH
#define ScriptRuntimeH

#include "actor/actor.h"
#include "dj/dj.h"

#include "lua.h"

typedef struct {
    lua_State *lua_state;
    DJ *dj;
    ActorRegistry *actor_registry;
    Actor *current_actor;
    char project_root[4096];
    float time_raw_delta_time;
    float time_delta_time;
    float time_unscaled_elapsed_time;
    float time_elapsed_time;
    float time_scale;
    float time_max_delta_time;
    float time_fixed_delta_time;
    usize time_frame_count;
    boolean time_paused;
} ScriptRuntime;

result script_runtime_init(ScriptRuntime *runtime, const char *project_root, DJ *dj);
void script_runtime_begin_frame(ScriptRuntime *runtime);
void script_async_tick(ScriptRuntime *runtime);
void script_runtime_bind_registry(ScriptRuntime *runtime, ActorRegistry *actor_registry);
void script_runtime_dispose(ScriptRuntime *runtime);

void *script_component_state_create(const char *module_path, const char *module_alias);
void script_component_state_dispose(void *state);

result script_component_initialize(Actor *actor, ActorComponent *component, void *context);
void script_component_update(Actor *actor, ActorComponent *component, ScriptRuntime *runtime);
void script_component_destroy(Actor *actor, ActorComponent *component, ScriptRuntime *runtime);

result script_runtime_invoke_ui_callback(ScriptRuntime *runtime, const char *callback_ref, const char *document_id, const char *node_id);

#endif // SCRIPT_RUNTIME_H
