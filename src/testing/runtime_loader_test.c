#include "testing.h"

#include "config/runtime_loader.h"

#include "common.h"

void run_runtime_loader_tests(void) {
#ifdef TESTING
    usize failed = 0;

    log_msg("Running runtime loader tests...");

    if (runtime_current_scene_path() != Null) {
        log_err("runtime_current_scene_path should return null when runtime is not active");
        ++failed;
    }

    if (runtime_request_scene_load("starting_scene.scene.conf") != Err) {
        log_err("runtime_request_scene_load should reject requests while runtime is inactive");
        ++failed;
    }

    if (runtime_request_scene_load(Null) != Err) {
        log_err("runtime_request_scene_load should reject null scene paths");
        ++failed;
    }

    if (runtime_request_scene_load("") != Err) {
        log_err("runtime_request_scene_load should reject empty scene paths");
        ++failed;
    }

    if (runtime_ui_node_exists("hud", "hud_label") != Err) {
        log_err("runtime_ui_node_exists should reject calls while runtime is inactive");
        ++failed;
    }

    if (runtime_ui_set_visible("hud", "hud_label", True) != Err) {
        log_err("runtime_ui_set_visible should reject calls while runtime is inactive");
        ++failed;
    }

    if (runtime_ui_set_text("hud", "hud_label", "value") != Err) {
        log_err("runtime_ui_set_text should reject calls while runtime is inactive");
        ++failed;
    }

    if (runtime_ui_push_document("hud.ui.conf") != Err) {
        log_err("runtime_ui_push_document should reject calls while runtime is inactive");
        ++failed;
    }

    if (runtime_ui_pop_document("hud") != Err) {
        log_err("runtime_ui_pop_document should reject calls while runtime is inactive");
        ++failed;
    }

    if (runtime_ui_set_document_layer("hud", 100) != Err) {
        log_err("runtime_ui_set_document_layer should reject calls while runtime is inactive");
        ++failed;
    }

    if (runtime_ui_bring_document_to_front("hud") != Err) {
        log_err("runtime_ui_bring_document_to_front should reject calls while runtime is inactive");
        ++failed;
    }

    if (runtime_ui_send_document_to_back("hud") != Err) {
        log_err("runtime_ui_send_document_to_back should reject calls while runtime is inactive");
        ++failed;
    }

    usize document_count = 999;
    if (runtime_ui_get_document_count(&document_count) != Err) {
        log_err("runtime_ui_get_document_count should reject calls while runtime is inactive");
        ++failed;
    }

    if (runtime_ui_get_document_id_at(0) != Null) {
        log_err("runtime_ui_get_document_id_at should return null while runtime is inactive");
        ++failed;
    }

    record_test_result("Runtime loader tests", failed);
#endif // TESTING
}
