#ifndef UI_RUNTIME_H
#define UI_RUNTIME_H

#include "common.h"
#include "script/script_runtime.h"

#include "tomlc17.h"

typedef struct UiRuntime UiRuntime;

result ui_runtime_create(UiRuntime **out_runtime, const char *project_root, const char *ui_root, ScriptRuntime *script_runtime);
void ui_runtime_destroy(UiRuntime *runtime);

result ui_runtime_unload_scene_documents(UiRuntime *runtime);
result ui_runtime_load_scene_documents(UiRuntime *runtime, toml_datum_t scene_toptab);

result ui_runtime_node_exists(UiRuntime *runtime, const char *document_id, const char *node_id);
result ui_runtime_set_node_visible(UiRuntime *runtime, const char *document_id, const char *node_id, boolean visible);
result ui_runtime_set_node_text(UiRuntime *runtime, const char *document_id, const char *node_id, const char *text);
result ui_runtime_push_document(UiRuntime *runtime, const char *document_path);
result ui_runtime_pop_document(UiRuntime *runtime, const char *document_id);
result ui_runtime_set_document_layer(UiRuntime *runtime, const char *document_id, int layer);
result ui_runtime_get_document_count(UiRuntime *runtime, usize *out_count);
result ui_runtime_get_document_id_at(UiRuntime *runtime, usize index, const char **out_document_id);

void ui_runtime_draw(UiRuntime *runtime);

#endif // UI_RUNTIME_H