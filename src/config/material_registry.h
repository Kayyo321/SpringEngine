#ifndef MaterialRegistryH
#define MaterialRegistryH

#include "common.h"

#include "shader_registry.h"

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

enum {
    MaterialRegistryMaxMaterials = 256,
    MaterialRegistryMaxIdLength = 64,
    MaterialRegistryMaxTextureSlots = 16,
};

typedef struct {
    char slot[MaterialRegistryMaxIdLength];
    char path[PATH_MAX];
} MaterialTextureSlot;

typedef struct {
    char source_path[PATH_MAX];
    int schema;
    char id[MaterialRegistryMaxIdLength];
    char shader_id[MaterialRegistryMaxIdLength];
    usize texture_slot_count;
    MaterialTextureSlot texture_slots[MaterialRegistryMaxTextureSlots];
} MaterialDescriptor;

typedef struct {
    boolean loaded;
    usize material_count;
    MaterialDescriptor materials[MaterialRegistryMaxMaterials];
} MaterialLibrary;

void material_library_reset(MaterialLibrary *library);
result material_library_load(const ShaderGlobalConfig *global_config, const ShaderLibrary *shader_library, MaterialLibrary *out_library);
const MaterialDescriptor *material_library_find_by_id(const MaterialLibrary *library, const char *material_id);
const char *material_descriptor_find_texture_slot(const MaterialDescriptor *descriptor, const char *slot);

#endif // MaterialRegistryH
