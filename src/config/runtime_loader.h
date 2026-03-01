#ifndef RUNTIME_LOADER_H
#define RUNTIME_LOADER_H

#include "common.h"

result run_project_runtime(const char *project_path);
result runtime_request_scene_load(const char *scene_path);
const char *runtime_current_scene_path(void);

#endif // RUNTIME_LOADER_H
