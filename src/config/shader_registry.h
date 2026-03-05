#ifndef ShaderRegistryH
#define ShaderRegistryH

#include "common.h"

#include "shader_config.h"

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

enum {
    ShaderRegistryMaxShaders = 128,
    ShaderRegistryMaxKeywords = 32,
    ShaderRegistryMaxUniforms = 64,
    ShaderRegistryMaxIdLength = 64,
    ShaderRegistryMaxUniformTypeLength = 24,
};

typedef struct {
    char name[ShaderRegistryMaxIdLength];
} ShaderKeywordDescriptor;

typedef struct {
    char name[ShaderRegistryMaxIdLength];
    char type[ShaderRegistryMaxUniformTypeLength];
    boolean has_source;
    char source[PATH_MAX];
} ShaderUniformDescriptor;

typedef struct {
    char source_path[PATH_MAX];
    int schema;
    char id[ShaderRegistryMaxIdLength];
    char vertex[PATH_MAX];
    char fragment[PATH_MAX];
    boolean depth_test;
    boolean depth_write;
    char blend[ShaderRegistryMaxIdLength];
    char cull[ShaderRegistryMaxIdLength];
    usize keyword_count;
    ShaderKeywordDescriptor keywords[ShaderRegistryMaxKeywords];
    usize uniform_count;
    ShaderUniformDescriptor uniforms[ShaderRegistryMaxUniforms];
} ShaderDescriptor;

typedef struct {
    boolean loaded;
    usize shader_count;
    ShaderDescriptor shaders[ShaderRegistryMaxShaders];
} ShaderLibrary;

void shader_library_reset(ShaderLibrary *library);
result shader_library_load(const ShaderGlobalConfig *global_config, ShaderLibrary *out_library);

#endif // ShaderRegistryH
