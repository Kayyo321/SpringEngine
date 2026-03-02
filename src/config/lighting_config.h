#ifndef LIGHTING_CONFIG_H
#define LIGHTING_CONFIG_H

#include "common.h"

#include "tomlc17.h"

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

enum {
    LightingMaxNameLength = 64,
    LightingMaxSceneMapEntries = 128,
};

typedef struct {
    char scene_id[LightingMaxNameLength];
    char file[PATH_MAX];
    char default_schema[LightingMaxNameLength];
} LightingSceneMapEntry;

typedef struct {
    boolean loaded;
    boolean allow_missing_scene_lighting;
    char lighting_root[PATH_MAX];
    char default_file[PATH_MAX];
    char default_schema[LightingMaxNameLength];
    usize scene_map_count;
    LightingSceneMapEntry scene_map[LightingMaxSceneMapEntries];
} LightingGlobalConfig;

typedef struct {
    boolean has_lighting;
    boolean has_blend;
    float blend_in_seconds;
    float global_light_multiplier;
    boolean has_clear_color;
    unsigned char clear_r;
    unsigned char clear_g;
    unsigned char clear_b;
    unsigned char clear_a;
    char file_ref[PATH_MAX];
    char file_path[PATH_MAX];
    char schema_name[LightingMaxNameLength];
} LightingSceneSelection;

void lighting_global_config_reset(LightingGlobalConfig *config);
void lighting_scene_selection_reset(LightingSceneSelection *selection);
result lighting_load_global_config(const char *project_root, LightingGlobalConfig *out_config);
result lighting_resolve_scene_selection(
    const LightingGlobalConfig *global_config,
    toml_datum_t scene_toptab,
    LightingSceneSelection *out_selection);

#endif // LIGHTING_CONFIG_H
