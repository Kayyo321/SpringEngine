#include "material_registry.h"

#include "tomlc17.h"
#include "vfs.h"

#include <stdio.h>
#include <string.h>

static result copy_text(char *out_text, usize out_size, const char *text) {
    if (!out_text || out_size == 0 || !text || text[0] == '\0')
        return Err;

    if (snprintf(out_text, out_size, "%s", text) >= (int)out_size)
        return Err;

    return Ok;
}

void material_library_reset(MaterialLibrary *library) {
    if (!library)
        return;

    memset(library, 0, sizeof(*library));
}

static boolean shader_id_exists(const ShaderLibrary *library, const char *shader_id) {
    if (!library || !shader_id || shader_id[0] == '\0')
        return False;

    for (usize index = 0; index < library->shader_count; ++index) {
        if (strcmp(library->shaders[index].id, shader_id) == 0)
            return True;
    }

    return False;
}

const MaterialDescriptor *material_library_find_by_id(const MaterialLibrary *library, const char *material_id) {
    if (!library || !material_id || material_id[0] == '\0')
        return Null;

    for (usize index = 0; index < library->material_count; ++index) {
        const MaterialDescriptor *descriptor = &library->materials[index];
        if (strcmp(descriptor->id, material_id) == 0)
            return descriptor;
    }

    return Null;
}

const char *material_descriptor_find_texture_slot(const MaterialDescriptor *descriptor, const char *slot) {
    if (!descriptor || !slot || slot[0] == '\0')
        return Null;

    for (usize index = 0; index < descriptor->texture_slot_count; ++index) {
        const MaterialTextureSlot *texture_slot = &descriptor->texture_slots[index];
        if (strcmp(texture_slot->slot, slot) == 0)
            return texture_slot->path;
    }

    return Null;
}

static result parse_material_textures(toml_datum_t material_table, const char *path, MaterialDescriptor *out_descriptor) {
    toml_datum_t textures = toml_get(material_table, "Textures");
    if (textures.type != TOML_TABLE)
        return Ok;

    for (int index = 0; index < textures.u.tab.size; ++index) {
        if (out_descriptor->texture_slot_count >= MaterialRegistryMaxTextureSlots) {
            log_err("Material '%s' exceeded max texture slots (%d) in '%s'", out_descriptor->id, MaterialRegistryMaxTextureSlots, path);
            return Err;
        }

        toml_datum_t texture_path = textures.u.tab.value[index];
        if (texture_path.type != TOML_STRING || !texture_path.u.s || texture_path.u.s[0] == '\0') {
            log_err("Material.Textures entries must be non-empty strings in '%s'", path);
            return Err;
        }

        MaterialTextureSlot *slot = &out_descriptor->texture_slots[out_descriptor->texture_slot_count];
        memset(slot, 0, sizeof(*slot));

        const int key_length = textures.u.tab.len[index];
        if (key_length <= 0 || (usize)(key_length + 1) > sizeof(slot->slot)) {
            log_err("Material texture slot key is invalid/too long in '%s'", path);
            return Err;
        }

        memcpy(slot->slot, textures.u.tab.key[index], (usize)key_length);
        slot->slot[key_length] = '\0';

        if (copy_text(slot->path, sizeof(slot->path), texture_path.u.s) != Ok) {
            log_err("Material texture path is too long in '%s'", path);
            return Err;
        }

        out_descriptor->texture_slot_count++;
    }

    return Ok;
}

static result parse_material_file(
    const char *path,
    const ShaderLibrary *shader_library,
    MaterialLibrary *library,
    usize *in_out_loaded_count) {
    if (!path || !shader_library || !library || !in_out_loaded_count)
        return Err;

    if (library->material_count >= MaterialRegistryMaxMaterials) {
        log_err("Material registry exceeded max materials (%d)", MaterialRegistryMaxMaterials);
        return Err;
    }

    toml_result_t parsed = {0};
    if (vfs_parse_toml_file(path, &parsed) != Ok) {
        log_err("Failed to parse material file '%s'", path);
        return Err;
    }

    toml_datum_t material_table = toml_get(parsed.toptab, "Material");
    if (material_table.type != TOML_TABLE) {
        log_err("Material file '%s' is missing [Material] table", path);
        toml_free(parsed);
        return Err;
    }

    MaterialDescriptor descriptor = {0};
    descriptor.schema = 1;

    toml_datum_t schema = toml_get(material_table, "schema");
    if (schema.type == TOML_INT64) {
        if (schema.u.int64 <= 0 || schema.u.int64 > 2147483647) {
            log_err("Material.schema must be a positive integer in '%s'", path);
            toml_free(parsed);
            return Err;
        }

        descriptor.schema = (int)schema.u.int64;
    }

    toml_datum_t id = toml_get(material_table, "id");
    toml_datum_t shader = toml_get(material_table, "shader");
    if (id.type != TOML_STRING || !id.u.s || id.u.s[0] == '\0' ||
        shader.type != TOML_STRING || !shader.u.s || shader.u.s[0] == '\0') {
        log_err("Material file '%s' requires Material.id and Material.shader", path);
        toml_free(parsed);
        return Err;
    }

    if (material_library_find_by_id(library, id.u.s)) {
        log_err("Material id '%s' is duplicated (file '%s')", id.u.s, path);
        toml_free(parsed);
        return Err;
    }

    if (shader_id_exists(shader_library, shader.u.s) != True) {
        log_err("Material '%s' references unknown shader '%s' in '%s'", id.u.s, shader.u.s, path);
        toml_free(parsed);
        return Err;
    }

    if (copy_text(descriptor.id, sizeof(descriptor.id), id.u.s) != Ok ||
        copy_text(descriptor.shader_id, sizeof(descriptor.shader_id), shader.u.s) != Ok ||
        copy_text(descriptor.source_path, sizeof(descriptor.source_path), path) != Ok) {
        log_err("Material id/shader/path text is too long in '%s'", path);
        toml_free(parsed);
        return Err;
    }

    if (parse_material_textures(material_table, path, &descriptor) != Ok) {
        toml_free(parsed);
        return Err;
    }

    library->materials[library->material_count++] = descriptor;
    *in_out_loaded_count = *in_out_loaded_count + 1;

    toml_free(parsed);
    return Ok;
}

typedef struct {
    const ShaderLibrary *shader_library;
    MaterialLibrary *material_library;
    usize loaded_count;
} MaterialLoadContext;

static result load_material_file_callback(const char *path, void *user_data) {
    MaterialLoadContext *context = (MaterialLoadContext *)user_data;
    if (!context || !context->shader_library || !context->material_library)
        return Err;

    return parse_material_file(path, context->shader_library, context->material_library, &context->loaded_count);
}

result material_library_load(const ShaderGlobalConfig *global_config, const ShaderLibrary *shader_library, MaterialLibrary *out_library) {
    if (!global_config || !shader_library || !out_library)
        return Err;

    material_library_reset(out_library);

    MaterialLoadContext context = {
        .shader_library = shader_library,
        .material_library = out_library,
        .loaded_count = 0,
    };

    if (vfs_file_exists(global_config->material_root) != True) {
        out_library->loaded = True;
        log_msg("Material directory '%s' not found; continuing with empty registry", global_config->material_root);
        return Ok;
    }

    if (vfs_for_each_file_with_suffix(global_config->material_root, ".mat.conf", load_material_file_callback, &context) != Ok) {
        log_err("Failed while scanning material files under '%s'", global_config->material_root);
        return Err;
    }

    out_library->loaded = True;
    log_msg("Loaded %lu material descriptor(s) from '%s'", context.loaded_count, global_config->material_root);
    return Ok;
}
