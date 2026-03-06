#include "maker.h"

#include <errno.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef _WIN32
#include <direct.h>
#endif

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

static boolean is_absolute_path(const char *path) {
	return path && path[0] == '/';
}

static result join_path(const char *base, const char *path, char *out_path, usize out_size) {
	if (!base || !path || !out_path || out_size == 0)
		return Err;

	if (is_absolute_path(path)) {
		if (snprintf(out_path, out_size, "%s", path) >= (int)out_size)
			return Err;
		return Ok;
	}

	if (snprintf(out_path, out_size, "%s/%s", base, path) >= (int)out_size)
		return Err;

	return Ok;
}

static result ensure_directory(const char *path) {
	if (!path || path[0] == '\0')
		return Err;

	struct stat path_stat = {0};
	if (stat(path, &path_stat) == 0) {
		if (S_ISDIR(path_stat.st_mode))
			return Ok;

		log_err("Path exists but is not a directory: '%s'", path);
		return Err;
	}

	int mkdir_result = 0;
#ifdef _WIN32
	mkdir_result = _mkdir(path);
#else
	mkdir_result = mkdir(path, 0755);
#endif
	if (mkdir_result != 0) {
		log_err("Failed to create directory '%s': %s", path, strerror(errno));
		return Err;
	}

	return Ok;
}

static result ensure_directory_recursive(const char *path) {
	if (!path || path[0] == '\0')
		return Err;

	char work[PATH_MAX] = {0};
	if (snprintf(work, sizeof(work), "%s", path) >= (int)sizeof(work)) {
		log_err("Directory path too long: '%s'", path);
		return Err;
	}

	const usize length = strlen(work);
	if (length == 0)
		return Err;

	if (length > 1 && work[length - 1] == '/')
		work[length - 1] = '\0';

	for (char *cursor = work + 1; *cursor != '\0'; ++cursor) {
		if (*cursor != '/')
			continue;

		*cursor = '\0';
		if (ensure_directory(work) != Ok)
			return Err;
		*cursor = '/';
	}

	return ensure_directory(work);
}

static result write_new_file(const char *path, const char *content) {
	if (!path || !content)
		return Err;

	if (access(path, F_OK) == 0) {
		log_err("Refusing to overwrite existing file: '%s'", path);
		return Err;
	}

	FILE *file = fopen(path, "w");
	if (!file) {
		log_err("Failed to open '%s' for writing", path);
		return Err;
	}

	if (fputs(content, file) == EOF) {
		fclose(file);
		log_err("Failed to write file '%s'", path);
		return Err;
	}

	if (fclose(file) != 0) {
		log_err("Failed to close file '%s'", path);
		return Err;
	}

	return Ok;
}

static result append_extension_if_missing(const char *name, const char *extension, char *out_name, usize out_size) {
	if (!name || !extension || !out_name || out_size == 0)
		return Err;

	const usize name_len = strlen(name);
	const usize extension_len = strlen(extension);

	if (name_len >= extension_len && strcmp(name + (name_len - extension_len), extension) == 0) {
		if (snprintf(out_name, out_size, "%s", name) >= (int)out_size)
			return Err;
		return Ok;
	}

	if (snprintf(out_name, out_size, "%s%s", name, extension) >= (int)out_size)
		return Err;

	return Ok;
}

static result make_lua_symbol_from_script_name(const char *script_name, char *out_symbol, usize out_size) {
	if (!script_name || !out_symbol || out_size == 0)
		return Err;

	const char *base_name = strrchr(script_name, '/');
	base_name = base_name ? base_name + 1 : script_name;

	char work[PATH_MAX] = {0};
	if (snprintf(work, sizeof(work), "%s", base_name) >= (int)sizeof(work))
		return Err;

	const usize length = strlen(work);
	if (length >= 4 && strcmp(work + length - 4, ".lua") == 0)
		work[length - 4] = '\0';

	usize out_index = 0;
	for (usize index = 0; work[index] != '\0'; ++index) {
		const unsigned char ch = (unsigned char)work[index];
		const boolean valid = (boolean)(isalnum(ch) || ch == '_');
		const char normalized = valid ? (char)ch : '_';

		if (out_index + 1 >= out_size)
			return Err;

		out_symbol[out_index++] = normalized;
	}

	if (out_index == 0) {
		if (out_size < 2)
			return Err;
		out_symbol[0] = 's';
		out_index = 1;
	}

	if ((out_symbol[0] >= '0' && out_symbol[0] <= '9') || out_symbol[0] == '\0') {
		if (out_index + 2 >= out_size)
			return Err;

		for (usize move = out_index; move > 0; --move)
			out_symbol[move] = out_symbol[move - 1];

		out_symbol[0] = '_';
		out_index++;
	}

	out_symbol[out_index] = '\0';
	return Ok;
}

static result make_default_project_files(const char *project_root, const char *project_name) {
	if (!project_root || !project_name)
		return Err;

	char springengine_conf_path[PATH_MAX] = {0};
	char input_conf_path[PATH_MAX] = {0};
	char autoload_path[PATH_MAX] = {0};
	char scene_path[PATH_MAX] = {0};
	char scene_data_path[PATH_MAX] = {0};
	char global_lighting_path[PATH_MAX] = {0};
	char default_lighting_path[PATH_MAX] = {0};

	if (join_path(project_root, "springengine.conf", springengine_conf_path, sizeof(springengine_conf_path)) != Ok)
		return Err;
	if (join_path(project_root, "input.conf", input_conf_path, sizeof(input_conf_path)) != Ok)
		return Err;
	if (join_path(project_root, "autoload.dat.conf", autoload_path, sizeof(autoload_path)) != Ok)
		return Err;
	if (join_path(project_root, "starting_scene.scene.conf", scene_path, sizeof(scene_path)) != Ok)
		return Err;
	if (join_path(project_root, "starting_scene.dat.conf", scene_data_path, sizeof(scene_data_path)) != Ok)
		return Err;
	if (join_path(project_root, "global.lighting.conf", global_lighting_path, sizeof(global_lighting_path)) != Ok)
		return Err;
	if (join_path(project_root, "lighting/default.lighting.conf", default_lighting_path, sizeof(default_lighting_path)) != Ok)
		return Err;

	char springengine_conf[4096] = {0};
	if (snprintf(
			springengine_conf,
			sizeof(springengine_conf),
			"[Engine]\n"
			"config_schema = 1\n"
			"project_id = \"%s\"\n"
			"project_title = \"%s\"\n"
			"engine_api_version = \"0.1\"\n"
			"\n"
			"[Paths]\n"
			"scenes_dir = \"./\"\n"
			"prefabs_dir = \"./prefabs\"\n"
			"assets_dir = \"./assets\"\n"
			"scripts_dir = \"./scripts\"\n"
			"ui_dir = \"./ui\"\n"
			"\n"
			"[Boot]\n"
			"first_scene = \"starting_scene.scene.conf\"\n"
			"autoload_data = \"autoload.dat.conf\"\n"
			"\n"
			"[Window]\n"
			"title = \"%s\"\n"
			"width = 900\n"
			"height = 600\n"
			"target_fps = 60\n"
			"resizable = true\n",
			project_name,
			project_name,
			project_name) >= (int)sizeof(springengine_conf)) {
		return Err;
	}

	const char *input_conf =
		"[Input]\n"
		"active_schema = \"Normal\"\n"
		"\n"
		"[Schema.Normal]\n"
		"MoveUp = { kind = \"key_down\", keys = [\"w\"] }\n"
		"MoveDown = { kind = \"key_down\", keys = [\"s\"] }\n"
		"MoveLeft = { kind = \"key_down\", keys = [\"a\"] }\n"
		"MoveRight = { kind = \"key_down\", keys = [\"d\"] }\n"
		"CloseGame = { kind = \"key_pressed\", keys = [\"escape\"] }\n";

	const char *autoload_conf =
		"[Data]\n"
		"schema = 1\n"
		"\n"
		"Actors = []\n";

	const char *scene_conf =
		"[Scene]\n"
		"schema = 1\n"
		"id = \"starting_scene\"\n"
		"title = \"Starting Scene\"\n"
		"data_file = \"starting_scene.dat.conf\"\n"
		"\n"
		"[Scene.Lighting]\n"
		"file = \"default.lighting.conf\"\n"
		"schema = \"GameplayDay\"\n"
		"\n"
		"[Scene.Load]\n"
		"actors = [\"main_camera\"]\n";

	const char *global_lighting_conf =
		"[LightingGlobal]\n"
		"schema = 1\n"
		"active_profile = \"default\"\n"
		"allow_missing_scene_lighting = false\n"
		"\n"
		"[LightingGlobal.Paths]\n"
		"lighting_dir = \"./lighting\"\n"
		"\n"
		"[LightingGlobal.Defaults]\n"
		"file = \"default.lighting.conf\"\n"
		"schema = \"GameplayDay\"\n"
		"\n"
		"[[LightingGlobal.SceneMap]]\n"
		"scene_id = \"starting_scene\"\n"
		"file = \"default.lighting.conf\"\n"
		"default_schema = \"GameplayDay\"\n";

	const char *default_lighting_conf =
		"[Lighting]\n"
		"schema = 1\n"
		"id = \"default\"\n"
		"default_schema = \"GameplayDay\"\n"
		"\n"
		"[Schema.GameplayDay.Ambient]\n"
		"mode = \"flat\"\n"
		"color = [90, 110, 140]\n"
		"intensity = 0.75\n"
		"\n"
		"[Schema.GameplayNight.Ambient]\n"
		"mode = \"flat\"\n"
		"color = [12, 16, 28]\n"
		"intensity = 0.20\n";

	const char *scene_data_conf =
		"[Data]\n"
		"schema = 1\n"
		"\n"
		"[[Actors]]\n"
		"id = \"main_camera\"\n"
		"enabled = true\n"
		"lifetime = \"scene\"\n"
		"\n"
		"[Actors.Transform]\n"
		"position = [0.0, 2.0, 10.0]\n"
		"rotation_euler = [10.0, 180.0, 0.0]\n"
		"scale = [1.0, 1.0, 1.0]\n"
		"\n"
		"[Actors.Components.Camera]\n"
		"projection = \"perspective\"\n"
		"fov = 60.0\n"
		"near_clip = 0.1\n"
		"far_clip = 1000.0\n";

	if (write_new_file(springengine_conf_path, springengine_conf) != Ok)
		return Err;
	if (write_new_file(input_conf_path, input_conf) != Ok)
		return Err;
	if (write_new_file(autoload_path, autoload_conf) != Ok)
		return Err;
	if (write_new_file(scene_path, scene_conf) != Ok)
		return Err;
	if (write_new_file(scene_data_path, scene_data_conf) != Ok)
		return Err;
	if (write_new_file(global_lighting_path, global_lighting_conf) != Ok)
		return Err;
	if (write_new_file(default_lighting_path, default_lighting_conf) != Ok)
		return Err;

	return Ok;
}

result make_project(const char *path_to_put_it, const char *project_name) {
	if (!path_to_put_it || path_to_put_it[0] == '\0' || !project_name || project_name[0] == '\0')
		return Err;

	char project_root[PATH_MAX] = {0};
	if (join_path(path_to_put_it, project_name, project_root, sizeof(project_root)) != Ok) {
		log_err("Project path is too long: '%s/%s'", path_to_put_it, project_name);
		return Err;
	}

	char scripts_dir[PATH_MAX] = {0};
	char prefabs_dir[PATH_MAX] = {0};
	char assets_dir[PATH_MAX] = {0};
	char sounds_dir[PATH_MAX] = {0};
	char sprites_dir[PATH_MAX] = {0};
	char ui_dir[PATH_MAX] = {0};
	char lighting_dir[PATH_MAX] = {0};

	if (join_path(project_root, "scripts", scripts_dir, sizeof(scripts_dir)) != Ok)
		return Err;
	if (join_path(project_root, "prefabs", prefabs_dir, sizeof(prefabs_dir)) != Ok)
		return Err;
	if (join_path(project_root, "assets", assets_dir, sizeof(assets_dir)) != Ok)
		return Err;
	if (join_path(assets_dir, "sounds", sounds_dir, sizeof(sounds_dir)) != Ok)
		return Err;
	if (join_path(assets_dir, "sprites", sprites_dir, sizeof(sprites_dir)) != Ok)
		return Err;
	if (join_path(project_root, "ui", ui_dir, sizeof(ui_dir)) != Ok)
		return Err;
	if (join_path(project_root, "lighting", lighting_dir, sizeof(lighting_dir)) != Ok)
		return Err;

	if (ensure_directory_recursive(project_root) != Ok)
		return Err;
	if (ensure_directory_recursive(scripts_dir) != Ok)
		return Err;
	if (ensure_directory_recursive(prefabs_dir) != Ok)
		return Err;
	if (ensure_directory_recursive(assets_dir) != Ok)
		return Err;
	if (ensure_directory_recursive(sounds_dir) != Ok)
		return Err;
	if (ensure_directory_recursive(sprites_dir) != Ok)
		return Err;
	if (ensure_directory_recursive(ui_dir) != Ok)
		return Err;
	if (ensure_directory_recursive(lighting_dir) != Ok)
		return Err;

	if (make_default_project_files(project_root, project_name) != Ok)
		return Err;

	log_msg("Created project at '%s'", project_root);
	return Ok;

}

result make_script(const char *project_root, const char *script_name) {
	if (!project_root || project_root[0] == '\0' || !script_name || script_name[0] == '\0')
		return Err;

	char file_name[PATH_MAX] = {0};
	if (append_extension_if_missing(script_name, ".lua", file_name, sizeof(file_name)) != Ok)
		return Err;

	char scripts_dir[PATH_MAX] = {0};
	char script_path[PATH_MAX] = {0};
	if (join_path(project_root, "scripts", scripts_dir, sizeof(scripts_dir)) != Ok)
		return Err;
	if (join_path(scripts_dir, file_name, script_path, sizeof(script_path)) != Ok)
		return Err;

	if (ensure_directory_recursive(scripts_dir) != Ok)
		return Err;

	char script_symbol[PATH_MAX] = {0};
	if (make_lua_symbol_from_script_name(script_name, script_symbol, sizeof(script_symbol)) != Ok)
		return Err;

	char default_script[2048] = {0};
	if (snprintf(
			default_script,
			sizeof(default_script),
			"local %s = {}\n"
			"\n"
			"function %s:awake()\n"
			"end\n"
			"\n"
			"function %s:start()\n"
			"end\n"
			"\n"
			"function %s:update()\n"
			"end\n"
			"\n"
			"function %s:on_destroy()\n"
			"end\n"
			"\n"
			"return %s\n",
			script_symbol,
			script_symbol,
			script_symbol,
			script_symbol,
			script_symbol,
			script_symbol) >= (int)sizeof(default_script)) {
		return Err;
	}

	if (write_new_file(script_path, default_script) != Ok)
		return Err;

	log_msg("Created script '%s'", script_path);
	return Ok;

}

result make_scene(const char *project_root, const char *scene_name) {
	if (!project_root || project_root[0] == '\0' || !scene_name || scene_name[0] == '\0')
		return Err;

	char manifest_name[PATH_MAX] = {0};
	char data_name[PATH_MAX] = {0};
	if (append_extension_if_missing(scene_name, ".scene.conf", manifest_name, sizeof(manifest_name)) != Ok)
		return Err;

	char base_name[PATH_MAX] = {0};
	if (snprintf(base_name, sizeof(base_name), "%s", scene_name) >= (int)sizeof(base_name))
		return Err;

	const char *scene_suffix = ".scene.conf";
	const usize base_len = strlen(base_name);
	const usize scene_suffix_len = strlen(scene_suffix);
	if (base_len >= scene_suffix_len && strcmp(base_name + (base_len - scene_suffix_len), scene_suffix) == 0)
		base_name[base_len - scene_suffix_len] = '\0';

	if (snprintf(data_name, sizeof(data_name), "%s.dat.conf", base_name) >= (int)sizeof(data_name))
		return Err;

	char scene_path[PATH_MAX] = {0};
	char scene_data_path[PATH_MAX] = {0};
	if (join_path(project_root, manifest_name, scene_path, sizeof(scene_path)) != Ok)
		return Err;
	if (join_path(project_root, data_name, scene_data_path, sizeof(scene_data_path)) != Ok)
		return Err;

	char scene_manifest[2048] = {0};
	if (snprintf(
			scene_manifest,
			sizeof(scene_manifest),
			"[Scene]\n"
			"schema = 1\n"
			"id = \"%s\"\n"
			"title = \"%s\"\n"
			"data_file = \"%s\"\n"
			"\n"
			"[Scene.Load]\n"
			"actors = []\n",
			base_name,
			base_name,
			data_name) >= (int)sizeof(scene_manifest)) {
		return Err;
	}

	const char *scene_data =
		"[Data]\n"
		"schema = 1\n"
		"\n"
		"Actors = []\n";

	if (write_new_file(scene_path, scene_manifest) != Ok)
		return Err;
	if (write_new_file(scene_data_path, scene_data) != Ok)
		return Err;

	log_msg("Created scene '%s' and data '%s'", scene_path, scene_data_path);
	return Ok;

}

result make_ui_document(const char *project_root, const char *document_name) {
    const char *ui_root = "ui";

	if (!project_root || project_root[0] == '\0' || !ui_root || ui_root[0] == '\0' || !document_name || document_name[0] == '\0')
		return Err;

	char resolved_ui_root[PATH_MAX] = {0};
	if (is_absolute_path(ui_root)) {
		if (snprintf(resolved_ui_root, sizeof(resolved_ui_root), "%s", ui_root) >= (int)sizeof(resolved_ui_root))
			return Err;
	} else {
		if (join_path(project_root, ui_root, resolved_ui_root, sizeof(resolved_ui_root)) != Ok)
			return Err;
	}

	if (ensure_directory_recursive(resolved_ui_root) != Ok)
		return Err;

	char ui_file_name[PATH_MAX] = {0};
	if (append_extension_if_missing(document_name, ".ui.conf", ui_file_name, sizeof(ui_file_name)) != Ok)
		return Err;

	char ui_path[PATH_MAX] = {0};
	if (join_path(resolved_ui_root, ui_file_name, ui_path, sizeof(ui_path)) != Ok)
		return Err;

	char doc_id[PATH_MAX] = {0};
	if (snprintf(doc_id, sizeof(doc_id), "%s", document_name) >= (int)sizeof(doc_id))
		return Err;

	const char *ui_suffix = ".ui.conf";
	const usize id_len = strlen(doc_id);
	const usize ui_suffix_len = strlen(ui_suffix);
	if (id_len >= ui_suffix_len && strcmp(doc_id + (id_len - ui_suffix_len), ui_suffix) == 0)
		doc_id[id_len - ui_suffix_len] = '\0';

	char ui_doc[2048] = {0};
	if (snprintf(
			ui_doc,
			sizeof(ui_doc),
			"[UI]\n"
			"schema = 1\n"
			"id = \"%s\"\n"
			"title = \"%s\"\n"
			"lifetime = \"scene\"\n"
			"layer = 0\n"
			"\n"
			"[[Widgets]]\n"
			"id = \"root_canvas\"\n"
			"type = \"Canvas\"\n"
			"\n"
			"[Widgets.Layout]\n"
			"anchor_min = [0.0, 0.0]\n"
			"anchor_max = [1.0, 1.0]\n"
			"offset_min = [0.0, 0.0]\n"
			"offset_max = [0.0, 0.0]\n",
			doc_id,
			doc_id) >= (int)sizeof(ui_doc)) {
		return Err;
	}

	if (write_new_file(ui_path, ui_doc) != Ok)
		return Err;

	log_msg("Created UI document '%s'", ui_path);
	return Ok;
}

result make_shader(const char *project_root, const char *shader_name) {
	if (!project_root || project_root[0] == '\0' || !shader_name || shader_name[0] == '\0')
		return Err;

	char base_name[PATH_MAX] = {0};
	if (snprintf(base_name, sizeof(base_name), "%s", shader_name) >= (int)sizeof(base_name))
		return Err;

	/* Strip .shader.conf suffix if the user included it. */
	const char *shader_suffix = ".shader.conf";
	const usize base_len = strlen(base_name);
	const usize shader_suffix_len = strlen(shader_suffix);
	if (base_len >= shader_suffix_len && strcmp(base_name + (base_len - shader_suffix_len), shader_suffix) == 0)
		base_name[base_len - shader_suffix_len] = '\0';

	char shaders_dir[PATH_MAX] = {0};
	if (join_path(project_root, "shaders", shaders_dir, sizeof(shaders_dir)) != Ok)
		return Err;

	if (ensure_directory_recursive(shaders_dir) != Ok)
		return Err;

	char conf_name[PATH_MAX] = {0};
	char vert_name[PATH_MAX] = {0};
	char frag_name[PATH_MAX] = {0};
	if (snprintf(conf_name, sizeof(conf_name), "%s.shader.conf", base_name) >= (int)sizeof(conf_name))
		return Err;
	if (snprintf(vert_name, sizeof(vert_name), "%s.vert.glsl", base_name) >= (int)sizeof(vert_name))
		return Err;
	if (snprintf(frag_name, sizeof(frag_name), "%s.frag.glsl", base_name) >= (int)sizeof(frag_name))
		return Err;

	char conf_path[PATH_MAX] = {0};
	char vert_path[PATH_MAX] = {0};
	char frag_path[PATH_MAX] = {0};
	if (join_path(shaders_dir, conf_name, conf_path, sizeof(conf_path)) != Ok)
		return Err;
	if (join_path(shaders_dir, vert_name, vert_path, sizeof(vert_path)) != Ok)
		return Err;
	if (join_path(shaders_dir, frag_name, frag_path, sizeof(frag_path)) != Ok)
		return Err;

	char shader_conf[1024] = {0};
	if (snprintf(
			shader_conf,
			sizeof(shader_conf),
			"[Shader]\n"
			"schema = 1\n"
			"id = \"%s\"\n"
			"vertex = \"%s\"\n"
			"fragment = \"%s\"\n"
			"\n"
			"[Shader.States]\n"
			"blend = \"alpha\"\n"
			"depth_test = false\n"
			"depth_write = false\n"
			"cull = \"none\"\n",
			base_name,
			vert_name,
			frag_name) >= (int)sizeof(shader_conf)) {
		return Err;
	}

	const char *vert_src =
		"#version 330\n"
		"\n"
		"in vec3 vertexPosition;\n"
		"in vec2 vertexTexCoord;\n"
		"in vec4 vertexColor;\n"
		"\n"
		"out vec2 fragTexCoord;\n"
		"out vec4 fragColor;\n"
		"\n"
		"uniform mat4 mvp;\n"
		"\n"
		"void main() {\n"
		"    fragTexCoord = vertexTexCoord;\n"
		"    fragColor = vertexColor;\n"
		"    gl_Position = mvp * vec4(vertexPosition, 1.0);\n"
		"}\n";

	const char *frag_src =
		"#version 330\n"
		"\n"
		"in vec2 fragTexCoord;\n"
		"in vec4 fragColor;\n"
		"\n"
		"out vec4 finalColor;\n"
		"\n"
		"uniform sampler2D texture0;\n"
		"uniform vec4 colDiffuse;\n"
		"\n"
		"void main() {\n"
		"    vec4 texel = texture(texture0, fragTexCoord);\n"
		"    finalColor = texel * colDiffuse * fragColor;\n"
		"}\n";

	if (write_new_file(conf_path, shader_conf) != Ok)
		return Err;
	if (write_new_file(vert_path, vert_src) != Ok)
		return Err;
	if (write_new_file(frag_path, frag_src) != Ok)
		return Err;

	log_msg("Created shader '%s' (%s, %s, %s)", base_name, conf_path, vert_path, frag_path);
	return Ok;
}

result make_material(const char *project_root, const char *material_name) {
	if (!project_root || project_root[0] == '\0' || !material_name || material_name[0] == '\0')
		return Err;

	char base_name[PATH_MAX] = {0};
	if (snprintf(base_name, sizeof(base_name), "%s", material_name) >= (int)sizeof(base_name))
		return Err;

	/* Strip .mat.conf suffix if the user included it. */
	const char *mat_suffix = ".mat.conf";
	const usize base_len = strlen(base_name);
	const usize mat_suffix_len = strlen(mat_suffix);
	if (base_len >= mat_suffix_len && strcmp(base_name + (base_len - mat_suffix_len), mat_suffix) == 0)
		base_name[base_len - mat_suffix_len] = '\0';

	char materials_dir[PATH_MAX] = {0};
	if (join_path(project_root, "materials", materials_dir, sizeof(materials_dir)) != Ok)
		return Err;

	if (ensure_directory_recursive(materials_dir) != Ok)
		return Err;

	char mat_file_name[PATH_MAX] = {0};
	if (snprintf(mat_file_name, sizeof(mat_file_name), "%s.mat.conf", base_name) >= (int)sizeof(mat_file_name))
		return Err;

	char mat_path[PATH_MAX] = {0};
	if (join_path(materials_dir, mat_file_name, mat_path, sizeof(mat_path)) != Ok)
		return Err;

	char mat_conf[512] = {0};
	if (snprintf(
			mat_conf,
			sizeof(mat_conf),
			"[Material]\n"
			"schema = 1\n"
			"id = \"%s\"\n"
			"shader = \"%s\"\n",
			base_name,
			base_name) >= (int)sizeof(mat_conf)) {
		return Err;
	}

	if (write_new_file(mat_path, mat_conf) != Ok)
		return Err;

	log_msg("Created material '%s'", mat_path);
	return Ok;
}

result make_prefab(const char *project_root, const char *prefab_name) {
	if (!project_root || project_root[0] == '\0' || !prefab_name || prefab_name[0] == '\0')
		return Err;

	char base_name[PATH_MAX] = {0};
	if (snprintf(base_name, sizeof(base_name), "%s", prefab_name) >= (int)sizeof(base_name))
		return Err;

	/* Strip .prefab.conf suffix if the user included it. */
	const char *prefab_suffix = ".prefab.conf";
	const usize base_len = strlen(base_name);
	const usize prefab_suffix_len = strlen(prefab_suffix);
	if (base_len >= prefab_suffix_len && strcmp(base_name + (base_len - prefab_suffix_len), prefab_suffix) == 0)
		base_name[base_len - prefab_suffix_len] = '\0';

	char prefabs_dir[PATH_MAX] = {0};
	if (join_path(project_root, "prefabs", prefabs_dir, sizeof(prefabs_dir)) != Ok)
		return Err;

	if (ensure_directory_recursive(prefabs_dir) != Ok)
		return Err;

	char prefab_file_name[PATH_MAX] = {0};
	if (snprintf(prefab_file_name, sizeof(prefab_file_name), "%s.prefab.conf", base_name) >= (int)sizeof(prefab_file_name))
		return Err;

	char prefab_path[PATH_MAX] = {0};
	if (join_path(prefabs_dir, prefab_file_name, prefab_path, sizeof(prefab_path)) != Ok)
		return Err;

	char prefab_conf[1024] = {0};
	if (snprintf(
			prefab_conf,
			sizeof(prefab_conf),
			"[Prefab]\n"
			"schema = 1\n"
			"id = \"%s\"\n"
			"\n"
			"[Prefab.Defaults]\n"
			"enabled = true\n"
			"lifetime = \"scene\"\n"
			"tags = []\n"
			"\n"
			"[Prefab.Transform]\n"
			"position = [0.0, 0.0, 0.0]\n"
			"rotation_euler = [0.0, 0.0, 0.0]\n"
			"scale = [1.0, 1.0, 1.0]\n"
			"anchor = \"center\"\n"
			"\n"
			"[Prefab.Components.Script]\n"
			"module = \"scripts/%s.lua\"\n",
			base_name,
			base_name) >= (int)sizeof(prefab_conf)) {
		return Err;
	}

	if (write_new_file(prefab_path, prefab_conf) != Ok)
		return Err;

	log_msg("Created prefab '%s'", prefab_path);
	return Ok;
}
