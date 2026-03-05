#ifndef ShaderConfigH
#define ShaderConfigH

#include "common.h"

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

enum {
    ShaderConfigMaxNameLength = 64,
};

typedef struct {
    boolean loaded;
    boolean config_file_present;
    boolean allow_compile_fallback;
    boolean allow_expensive_post;
    int max_variants_per_shader;
    char config_file_path[PATH_MAX];
    char shader_root[PATH_MAX];
    char material_root[PATH_MAX];
    char active_profile[ShaderConfigMaxNameLength];
    char default_sprite_material[PATH_MAX];
    char default_ui_material[PATH_MAX];
    char default_post_stack[PATH_MAX];
} ShaderGlobalConfig;

void shader_global_config_reset(ShaderGlobalConfig *config);
result shader_load_global_config(const char *project_root, ShaderGlobalConfig *out_config);

#endif // ShaderConfigH
