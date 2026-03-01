#include "testing.h"

#include "ui/ui_runtime.h"

#include "common.h"

#include "tomlc17.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static result write_text_file(const char *path, const char *contents) {
    if (!path || !contents)
        return Err;

    FILE *file = fopen(path, "w");
    if (!file)
        return Err;

    fputs(contents, file);
    fclose(file);
    return Ok;
}

void run_ui_runtime_tests(void) {
#ifdef TESTING
    usize failed = 0;

    log_msg("Running UI runtime tests...");

    char temp_dir_template[] = "/tmp/springengine-ui-runtime-XXXXXX";
    char *temp_dir = mkdtemp(temp_dir_template);
    if (!temp_dir) {
        log_err("mkdtemp failed for ui runtime tests");
        ++failed;
        record_test_result("UI runtime tests", failed);
        return;
    }

    char scene_path[512] = {0};
    char hud_path[512] = {0};
    char lab_path[512] = {0};
    char overlay_path[512] = {0};

    if (snprintf(scene_path, sizeof(scene_path), "%s/scene.scene.conf", temp_dir) >= (int)sizeof(scene_path)
        || snprintf(hud_path, sizeof(hud_path), "%s/hud.ui.conf", temp_dir) >= (int)sizeof(hud_path)
        || snprintf(lab_path, sizeof(lab_path), "%s/lab.ui.conf", temp_dir) >= (int)sizeof(lab_path)
        || snprintf(overlay_path, sizeof(overlay_path), "%s/overlay.ui.conf", temp_dir) >= (int)sizeof(overlay_path)) {
        log_err("failed composing ui runtime test paths");
        ++failed;
        goto cleanup;
    }

    if (write_text_file(scene_path,
            "[Scene]\n"
            "schema = 1\n"
            "id = \"test_scene\"\n"
            "data_file = \"ignored.dat.conf\"\n"
            "\n"
            "[Scene.UI]\n"
            "documents = [\"hud.ui.conf\", \"lab.ui.conf\"]\n")
        != Ok) {
        log_err("failed writing scene ui test file");
        ++failed;
        goto cleanup;
    }

    if (write_text_file(hud_path,
            "[UI]\n"
            "schema = 1\n"
            "id = \"hud\"\n"
            "lifetime = \"scene\"\n"
            "layer = 10\n"
            "\n"
            "[[Widgets]]\n"
            "id = \"root\"\n"
            "type = \"Canvas\"\n"
            "\n"
            "[Widgets.Layout]\n"
            "anchor_min = [0.0, 0.0]\n"
            "anchor_max = [1.0, 1.0]\n"
            "offset_min = [0.0, 0.0]\n"
            "offset_max = [0.0, 0.0]\n"
            "\n"
            "[[Widgets]]\n"
            "id = \"hud_label\"\n"
            "type = \"Label\"\n"
            "parent = \"root\"\n"
            "text = \"HUD\"\n"
            "font_size = 18\n"
            "\n"
            "[Widgets.Layout]\n"
            "anchor_min = [0.1, 0.1]\n"
            "anchor_max = [0.4, 0.2]\n"
            "offset_min = [0.0, 0.0]\n"
            "offset_max = [0.0, 0.0]\n")
        != Ok) {
        log_err("failed writing hud ui test file");
        ++failed;
        goto cleanup;
    }

    if (write_text_file(lab_path,
            "[UI]\n"
            "schema = 1\n"
            "id = \"ui_lab\"\n"
            "lifetime = \"scene\"\n"
            "layer = 20\n"
            "\n"
            "[[Widgets]]\n"
            "id = \"lab_root\"\n"
            "type = \"Canvas\"\n"
            "\n"
            "[Widgets.Layout]\n"
            "anchor_min = [0.0, 0.0]\n"
            "anchor_max = [1.0, 1.0]\n"
            "offset_min = [0.0, 0.0]\n"
            "offset_max = [0.0, 0.0]\n")
        != Ok) {
        log_err("failed writing lab ui test file");
        ++failed;
        goto cleanup;
    }

    if (write_text_file(overlay_path,
            "[UI]\n"
            "schema = 1\n"
            "id = \"overlay\"\n"
            "lifetime = \"scene\"\n"
            "layer = 30\n"
            "\n"
            "[[Widgets]]\n"
            "id = \"overlay_root\"\n"
            "type = \"Canvas\"\n"
            "\n"
            "[Widgets.Layout]\n"
            "anchor_min = [0.0, 0.0]\n"
            "anchor_max = [1.0, 1.0]\n"
            "offset_min = [0.0, 0.0]\n"
            "offset_max = [0.0, 0.0]\n")
        != Ok) {
        log_err("failed writing overlay ui test file");
        ++failed;
        goto cleanup;
    }

    UiRuntime *runtime = Null;
    toml_result_t scene_parsed = {0};
    boolean scene_ok = False;

    if (ui_runtime_create(&runtime, temp_dir, temp_dir, Null) != Ok || !runtime) {
        log_err("ui_runtime_create should succeed for valid temp project paths");
        ++failed;
        goto cleanup;
    }

    scene_parsed = toml_parse_file_ex(scene_path);
    if (!scene_parsed.ok) {
        log_err("failed parsing ui runtime test scene: %s", scene_parsed.errmsg);
        ++failed;
        ui_runtime_destroy(runtime);
        runtime = Null;
        goto cleanup;
    }
    scene_ok = True;

    if (ui_runtime_load_scene_documents(runtime, scene_parsed.toptab) != Ok) {
        log_err("ui_runtime_load_scene_documents should load scene UI documents");
        ++failed;
    }

    usize document_count = 0;
    if (ui_runtime_get_document_count(runtime, &document_count) != Ok || document_count != 2) {
        log_err("ui runtime should have 2 documents after scene load");
        ++failed;
    }

    const char *ordered_document = Null;
    if (ui_runtime_get_document_id_at(runtime, 0, &ordered_document) != Ok || !ordered_document || strcmp(ordered_document, "hud") != 0) {
        log_err("ui document order index 0 should be 'hud' based on layer");
        ++failed;
    }

    if (ui_runtime_get_document_id_at(runtime, 1, &ordered_document) != Ok || !ordered_document || strcmp(ordered_document, "ui_lab") != 0) {
        log_err("ui document order index 1 should be 'ui_lab' based on layer");
        ++failed;
    }

    if (ui_runtime_node_exists(runtime, "hud", "hud_label") != Ok) {
        log_err("ui_runtime_node_exists should resolve existing node");
        ++failed;
    }

    if (ui_runtime_set_node_text(runtime, "hud", "hud_label", "Updated") != Ok) {
        log_err("ui_runtime_set_node_text should update label nodes");
        ++failed;
    }

    if (ui_runtime_push_document(runtime, "overlay.ui.conf") != Ok) {
        log_err("ui_runtime_push_document should load overlay document");
        ++failed;
    }

    if (ui_runtime_get_document_count(runtime, &document_count) != Ok || document_count != 3) {
        log_err("ui runtime should have 3 documents after push_document");
        ++failed;
    }

    if (ui_runtime_get_document_id_at(runtime, 2, &ordered_document) != Ok || !ordered_document || strcmp(ordered_document, "overlay") != 0) {
        log_err("overlay should be top document after push_document");
        ++failed;
    }

    if (ui_runtime_send_document_to_back(runtime, "overlay") != Ok) {
        log_err("ui_runtime_send_document_to_back should succeed");
        ++failed;
    }

    if (ui_runtime_get_document_id_at(runtime, 0, &ordered_document) != Ok || !ordered_document || strcmp(ordered_document, "overlay") != 0) {
        log_err("overlay should become bottom document after send_to_back");
        ++failed;
    }

    if (ui_runtime_bring_document_to_front(runtime, "hud") != Ok) {
        log_err("ui_runtime_bring_document_to_front should succeed");
        ++failed;
    }

    if (ui_runtime_get_document_id_at(runtime, 2, &ordered_document) != Ok || !ordered_document || strcmp(ordered_document, "hud") != 0) {
        log_err("hud should become top document after bring_to_front");
        ++failed;
    }

    if (ui_runtime_set_document_layer(runtime, "ui_lab", 250) != Ok) {
        log_err("ui_runtime_set_document_layer should succeed for existing document");
        ++failed;
    }

    if (ui_runtime_get_document_id_at(runtime, 2, &ordered_document) != Ok || !ordered_document || strcmp(ordered_document, "ui_lab") != 0) {
        log_err("ui_lab should become top document after layer increase");
        ++failed;
    }

    if (ui_runtime_pop_document(runtime, "ui_lab") != Ok) {
        log_err("ui_runtime_pop_document should remove the requested document");
        ++failed;
    }

    if (ui_runtime_get_document_count(runtime, &document_count) != Ok || document_count != 2) {
        log_err("ui runtime should have 2 documents after pop_document");
        ++failed;
    }

    if (ui_runtime_unload_scene_documents(runtime) != Ok) {
        log_err("ui_runtime_unload_scene_documents should succeed");
        ++failed;
    }

    if (ui_runtime_get_document_count(runtime, &document_count) != Ok || document_count != 0) {
        log_err("ui runtime should have 0 documents after unloading scene docs");
        ++failed;
    }

    if (scene_ok)
        toml_free(scene_parsed);

    ui_runtime_destroy(runtime);
    runtime = Null;

cleanup:
    (void)unlink(scene_path);
    (void)unlink(hud_path);
    (void)unlink(lab_path);
    (void)unlink(overlay_path);
    (void)rmdir(temp_dir);

    record_test_result("UI runtime tests", failed);
#endif // TESTING
}
