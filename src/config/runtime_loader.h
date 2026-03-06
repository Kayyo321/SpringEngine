#ifndef RuntimeLoaderH
#define RuntimeLoaderH

#include "common.h"

result run_project_runtime(const char *project_path);
result validate_project_configs(const char *project_path);
result validate_project_version_requirement(const char *project_path);
result runtime_request_scene_load(const char *scene_path);
const char *runtime_current_scene_path(void);
result runtime_instantiate_prefab(const char *prefab_ref_id, boolean has_position, float x, float y, float z, char *out_actor_id, usize out_actor_id_size);
result runtime_destroy_actor(const char *actor_id);
result runtime_set_actor_destroy_on_load(const char *actor_id, boolean destroy_on_load);
result runtime_get_actor_destroy_on_load(const char *actor_id, boolean *out_destroy_on_load);
result runtime_ui_node_exists(const char *document_id, const char *node_id);
result runtime_ui_set_visible(const char *document_id, const char *node_id, boolean visible);
result runtime_ui_set_text(const char *document_id, const char *node_id, const char *text);
result runtime_ui_push_document(const char *document_path);
result runtime_ui_pop_document(const char *document_id);
result runtime_ui_set_document_layer(const char *document_id, int layer);
result runtime_ui_bring_document_to_front(const char *document_id);
result runtime_ui_send_document_to_back(const char *document_id);
result runtime_ui_get_document_count(usize *out_count);
const char *runtime_ui_get_document_id_at(usize index);
boolean runtime_material_alias_exists(const char *material_alias);
result runtime_material_set_property(const char *material_alias, const char *property_name, float value);
result runtime_material_get_property(const char *material_alias, const char *property_name, float *out_value);
result runtime_camera_set_main(const char *actor_id);

#endif // RUNTIME_LOADER_H
