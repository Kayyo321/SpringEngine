#ifndef SCRIPT_RUNTIME_H
#define SCRIPT_RUNTIME_H

#include "actor/actor.h"

#include "lua.h"

typedef struct {
    lua_State *lua_state;
    char project_root[4096];
} ScriptRuntime;

result script_runtime_init(ScriptRuntime *runtime, const char *project_root);
void script_runtime_dispose(ScriptRuntime *runtime);

void *script_component_state_create(const char *module_path);
void script_component_state_dispose(void *state);

result script_component_initialize(Actor *actor, ActorComponent *component, void *context);
void script_component_update(Actor *actor, ActorComponent *component, ScriptRuntime *runtime);
void script_component_destroy(Actor *actor, ActorComponent *component, ScriptRuntime *runtime);

#endif // SCRIPT_RUNTIME_H
