#ifndef MakerH
#define MakerH

#include "common.h"

result make_project(const char *path_to_put_it, const char *project_name);
result make_script(const char *project_root, const char *script_name);
result make_scene(const char *project_root, const char *scene_name);
result make_ui_document(const char *project_root, const char *document_name);
result make_shader(const char *project_root, const char *shader_name);
result make_material(const char *project_root, const char *material_name);
result make_prefab(const char *project_root, const char *prefab_name);

#endif // MAKER_H
