#ifndef RUNTIME_LOADER_H
#define RUNTIME_LOADER_H

#include "common.h"

result run_project_runtime(const char *project_path);
result runtime_request_scene_load(const char *scene_path);
const char *runtime_current_scene_path(void);
result runtime_ui_node_exists(const char *document_id, const char *node_id);
result runtime_ui_set_visible(const char *document_id, const char *node_id, boolean visible);
result runtime_ui_set_text(const char *document_id, const char *node_id, const char *text);

#endif // RUNTIME_LOADER_H
