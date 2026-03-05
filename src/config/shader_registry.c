#include "shader_registry.h"

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

static boolean text_equals(const char *left, const char *right) {
    if (!left || !right)
        return False;

    return strcmp(left, right) == 0 ? True : False;
}

static boolean is_supported_blend(const char *blend) {
    return text_equals(blend, "alpha") || text_equals(blend, "additive") || text_equals(blend, "none");
}

static boolean is_supported_cull(const char *cull) {
    return text_equals(cull, "none") || text_equals(cull, "back") || text_equals(cull, "front");
}

static boolean is_supported_uniform_type(const char *type) {
    return text_equals(type, "float") ||
        text_equals(type, "vec2") ||
        text_equals(type, "vec3") ||
        text_equals(type, "vec4") ||
        text_equals(type, "color") ||
        text_equals(type, "texture2D") ||
        text_equals(type, "int") ||
        text_equals(type, "bool");
}

void shader_library_reset(ShaderLibrary *library) {
    if (!library)
        return;

    memset(library, 0, sizeof(*library));
}

static boolean shader_id_exists(const ShaderLibrary *library, const char *id) {
    if (!library || !id || id[0] == '\0')
        return False;

    for (usize index = 0; index < library->shader_count; ++index) {
        if (strcmp(library->shaders[index].id, id) == 0)
            return True;
    }

    return False;
}

static boolean keyword_exists(const ShaderDescriptor *descriptor, const char *name) {
    if (!descriptor || !name || name[0] == '\0')
        return False;

    for (usize index = 0; index < descriptor->keyword_count; ++index) {
        if (strcmp(descriptor->keywords[index].name, name) == 0)
            return True;
    }

    return False;
}

static boolean uniform_exists(const ShaderDescriptor *descriptor, const char *name) {
    if (!descriptor || !name || name[0] == '\0')
        return False;

    for (usize index = 0; index < descriptor->uniform_count; ++index) {
        if (strcmp(descriptor->uniforms[index].name, name) == 0)
            return True;
    }

    return False;
}

static result parse_shader_keywords(toml_datum_t shader_table, const char *path, ShaderDescriptor *out_descriptor) {
    toml_datum_t keywords = toml_get(shader_table, "Keywords");
    if (keywords.type != TOML_ARRAY)
        return Ok;

    for (int index = 0; index < keywords.u.arr.size; ++index) {
        if (out_descriptor->keyword_count >= ShaderRegistryMaxKeywords) {
            log_err("Shader '%s' exceeds max keyword count (%d) in '%s'", out_descriptor->id, ShaderRegistryMaxKeywords, path);
            return Err;
        }

        toml_datum_t keyword_table = keywords.u.arr.elem[index];
        if (keyword_table.type != TOML_TABLE) {
            log_err("Shader.Keywords[%d] must be a table in '%s'", index, path);
            return Err;
        }

        toml_datum_t name = toml_get(keyword_table, "name");
        if (name.type != TOML_STRING || !name.u.s || name.u.s[0] == '\0') {
            log_err("Shader.Keywords[%d].name must be a non-empty string in '%s'", index, path);
            return Err;
        }

        if (keyword_exists(out_descriptor, name.u.s)) {
            log_err("Shader keyword '%s' is duplicated in '%s'", name.u.s, path);
            return Err;
        }

        ShaderKeywordDescriptor *descriptor = &out_descriptor->keywords[out_descriptor->keyword_count];
        if (copy_text(descriptor->name, sizeof(descriptor->name), name.u.s) != Ok) {
            log_err("Shader keyword name is too long in '%s'", path);
            return Err;
        }

        out_descriptor->keyword_count++;
    }

    return Ok;
}

static result parse_shader_uniforms(toml_datum_t shader_table, const char *path, ShaderDescriptor *out_descriptor) {
    toml_datum_t uniforms = toml_get(shader_table, "Uniforms");
    if (uniforms.type != TOML_ARRAY)
        return Ok;

    for (int index = 0; index < uniforms.u.arr.size; ++index) {
        if (out_descriptor->uniform_count >= ShaderRegistryMaxUniforms) {
            log_err("Shader '%s' exceeds max uniform count (%d) in '%s'", out_descriptor->id, ShaderRegistryMaxUniforms, path);
            return Err;
        }

        toml_datum_t uniform_table = uniforms.u.arr.elem[index];
        if (uniform_table.type != TOML_TABLE) {
            log_err("Shader.Uniforms[%d] must be a table in '%s'", index, path);
            return Err;
        }

        toml_datum_t name = toml_get(uniform_table, "name");
        toml_datum_t type = toml_get(uniform_table, "type");
        if (name.type != TOML_STRING || !name.u.s || name.u.s[0] == '\0' ||
            type.type != TOML_STRING || !type.u.s || type.u.s[0] == '\0') {
            log_err("Shader.Uniforms[%d] requires name and type in '%s'", index, path);
            return Err;
        }

        if (is_supported_uniform_type(type.u.s) != True) {
            log_err("Shader uniform type '%s' is not supported in '%s'", type.u.s, path);
            return Err;
        }

        if (uniform_exists(out_descriptor, name.u.s)) {
            log_err("Shader uniform '%s' is duplicated in '%s'", name.u.s, path);
            return Err;
        }

        ShaderUniformDescriptor *descriptor = &out_descriptor->uniforms[out_descriptor->uniform_count];
        if (copy_text(descriptor->name, sizeof(descriptor->name), name.u.s) != Ok ||
            copy_text(descriptor->type, sizeof(descriptor->type), type.u.s) != Ok) {
            log_err("Shader uniform name/type is too long in '%s'", path);
            return Err;
        }

        toml_datum_t source = toml_get(uniform_table, "source");
        if (source.type == TOML_STRING && source.u.s && source.u.s[0] != '\0') {
            if (copy_text(descriptor->source, sizeof(descriptor->source), source.u.s) != Ok) {
                log_err("Shader uniform source is too long in '%s'", path);
                return Err;
            }
            descriptor->has_source = True;
        }

        out_descriptor->uniform_count++;
    }

    return Ok;
}

static result parse_shader_file(const char *path, ShaderLibrary *library, usize *in_out_loaded_count) {
    if (!path || !library || !in_out_loaded_count)
        return Err;

    if (library->shader_count >= ShaderRegistryMaxShaders) {
        log_err("Shader registry exceeded max shaders (%d)", ShaderRegistryMaxShaders);
        return Err;
    }

    toml_result_t parsed = {0};
    if (vfs_parse_toml_file(path, &parsed) != Ok) {
        log_err("Failed to parse shader file '%s'", path);
        return Err;
    }

    toml_datum_t shader_table = toml_get(parsed.toptab, "Shader");
    if (shader_table.type != TOML_TABLE) {
        log_err("Shader file '%s' is missing [Shader] table", path);
        toml_free(parsed);
        return Err;
    }

    ShaderDescriptor descriptor = {0};
    descriptor.schema = 1;
    descriptor.depth_test = False;
    descriptor.depth_write = False;

    if (copy_text(descriptor.blend, sizeof(descriptor.blend), "alpha") != Ok ||
        copy_text(descriptor.cull, sizeof(descriptor.cull), "none") != Ok) {
        toml_free(parsed);
        return Err;
    }

    toml_datum_t schema = toml_get(shader_table, "schema");
    if (schema.type == TOML_INT64) {
        if (schema.u.int64 <= 0 || schema.u.int64 > 2147483647) {
            log_err("Shader.schema must be a positive integer in '%s'", path);
            toml_free(parsed);
            return Err;
        }
        descriptor.schema = (int)schema.u.int64;
    }

    toml_datum_t id = toml_get(shader_table, "id");
    toml_datum_t vertex = toml_get(shader_table, "vertex");
    toml_datum_t fragment = toml_get(shader_table, "fragment");
    if (id.type != TOML_STRING || !id.u.s || id.u.s[0] == '\0' ||
        vertex.type != TOML_STRING || !vertex.u.s || vertex.u.s[0] == '\0' ||
        fragment.type != TOML_STRING || !fragment.u.s || fragment.u.s[0] == '\0') {
        log_err("Shader file '%s' requires Shader.id, Shader.vertex, and Shader.fragment", path);
        toml_free(parsed);
        return Err;
    }

    if (shader_id_exists(library, id.u.s)) {
        log_err("Shader id '%s' is duplicated (file '%s')", id.u.s, path);
        toml_free(parsed);
        return Err;
    }

    if (copy_text(descriptor.id, sizeof(descriptor.id), id.u.s) != Ok ||
        copy_text(descriptor.vertex, sizeof(descriptor.vertex), vertex.u.s) != Ok ||
        copy_text(descriptor.fragment, sizeof(descriptor.fragment), fragment.u.s) != Ok ||
        copy_text(descriptor.source_path, sizeof(descriptor.source_path), path) != Ok) {
        log_err("Shader id/path text is too long in '%s'", path);
        toml_free(parsed);
        return Err;
    }

    toml_datum_t states = toml_get(shader_table, "States");
    if (states.type == TOML_TABLE) {
        toml_datum_t blend = toml_get(states, "blend");
        if (blend.type == TOML_STRING && blend.u.s && blend.u.s[0] != '\0') {
            if (is_supported_blend(blend.u.s) != True || copy_text(descriptor.blend, sizeof(descriptor.blend), blend.u.s) != Ok) {
                log_err("Shader.States.blend has unsupported value '%s' in '%s'", blend.u.s, path);
                toml_free(parsed);
                return Err;
            }
        }

        toml_datum_t cull = toml_get(states, "cull");
        if (cull.type == TOML_STRING && cull.u.s && cull.u.s[0] != '\0') {
            if (is_supported_cull(cull.u.s) != True || copy_text(descriptor.cull, sizeof(descriptor.cull), cull.u.s) != Ok) {
                log_err("Shader.States.cull has unsupported value '%s' in '%s'", cull.u.s, path);
                toml_free(parsed);
                return Err;
            }
        }

        toml_datum_t depth_test = toml_get(states, "depth_test");
        if (depth_test.type == TOML_BOOLEAN)
            descriptor.depth_test = depth_test.u.boolean ? True : False;

        toml_datum_t depth_write = toml_get(states, "depth_write");
        if (depth_write.type == TOML_BOOLEAN)
            descriptor.depth_write = depth_write.u.boolean ? True : False;
    }

    if (parse_shader_keywords(shader_table, path, &descriptor) != Ok ||
        parse_shader_uniforms(shader_table, path, &descriptor) != Ok) {
        toml_free(parsed);
        return Err;
    }

    library->shaders[library->shader_count++] = descriptor;
    *in_out_loaded_count = *in_out_loaded_count + 1;

    toml_free(parsed);
    return Ok;
}

typedef struct {
    ShaderLibrary *library;
    usize loaded_count;
} ShaderLoadContext;

static result load_shader_file_callback(const char *path, void *user_data) {
    ShaderLoadContext *context = (ShaderLoadContext *)user_data;
    if (!context || !context->library)
        return Err;

    return parse_shader_file(path, context->library, &context->loaded_count);
}

result shader_library_load(const ShaderGlobalConfig *global_config, ShaderLibrary *out_library) {
    if (!global_config || !out_library)
        return Err;

    shader_library_reset(out_library);

    ShaderLoadContext context = {
        .library = out_library,
        .loaded_count = 0,
    };

    if (vfs_file_exists(global_config->shader_root) != True) {
        out_library->loaded = True;
        log_msg("Shader directory '%s' not found; continuing with empty registry", global_config->shader_root);
        return Ok;
    }

    if (vfs_for_each_file_with_suffix(global_config->shader_root, ".shader.conf", load_shader_file_callback, &context) != Ok) {
        log_err("Failed while scanning shader files under '%s'", global_config->shader_root);
        return Err;
    }

    out_library->loaded = True;
    log_msg("Loaded %lu shader descriptor(s) from '%s'", context.loaded_count, global_config->shader_root);
    return Ok;
}
