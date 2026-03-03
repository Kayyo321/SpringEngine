#include "testing.h"

#include "config/runtime_loader.h"

#include "common.h"

void run_runtime_loader_tests(void) {
#ifdef Testing
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

    if (runtime_instantiate_prefab("crate", True, 1.0f, 2.0f, 3.0f, Null, 0) != Err) {
        log_err("runtime_instantiate_prefab should reject calls while runtime is inactive");
        ++failed;
    }

    if (runtime_destroy_actor("player") != Err) {
        log_err("runtime_destroy_actor should reject calls while runtime is inactive");
        ++failed;
    }

    if (runtime_set_actor_destroy_on_load("player", True) != Err) {
        log_err("runtime_set_actor_destroy_on_load should reject calls while runtime is inactive");
        ++failed;
    }

    boolean destroy_on_load = False;
    if (runtime_get_actor_destroy_on_load("player", &destroy_on_load) != Err) {
        log_err("runtime_get_actor_destroy_on_load should reject calls while runtime is inactive");
        ++failed;
    }

    if (runtime_get_actor_destroy_on_load("player", Null) != Err) {
        log_err("runtime_get_actor_destroy_on_load should reject null output pointers");
        ++failed;
    }

    if (runtime_ui_node_exists("hud", "hud_label") != Err) {
        log_err("runtime_ui_node_exists should reject calls while runtime is inactive");
        ++failed;
    }

    if (runtime_ui_node_exists(Null, "hud_label") != Err) {
        log_err("runtime_ui_node_exists should reject null document ids");
        ++failed;
    }

    if (runtime_ui_node_exists("hud", Null) != Err) {
        log_err("runtime_ui_node_exists should reject null node ids");
        ++failed;
    }

    if (runtime_ui_set_visible("hud", "hud_label", True) != Err) {
        log_err("runtime_ui_set_visible should reject calls while runtime is inactive");
        ++failed;
    }

    if (runtime_ui_set_visible(Null, "hud_label", True) != Err) {
        log_err("runtime_ui_set_visible should reject null document ids");
        ++failed;
    }

    if (runtime_ui_set_visible("hud", Null, True) != Err) {
        log_err("runtime_ui_set_visible should reject null node ids");
        ++failed;
    }

    if (runtime_ui_set_text("hud", "hud_label", "value") != Err) {
        log_err("runtime_ui_set_text should reject calls while runtime is inactive");
        ++failed;
    }

    if (runtime_ui_set_text(Null, "hud_label", "value") != Err) {
        log_err("runtime_ui_set_text should reject null document ids");
        ++failed;
    }

    if (runtime_ui_set_text("hud", Null, "value") != Err) {
        log_err("runtime_ui_set_text should reject null node ids");
        ++failed;
    }

    if (runtime_ui_set_text("hud", "hud_label", Null) != Err) {
        log_err("runtime_ui_set_text should reject null text");
        ++failed;
    }

    if (runtime_ui_push_document("hud.ui.conf") != Err) {
        log_err("runtime_ui_push_document should reject calls while runtime is inactive");
        ++failed;
    }

    if (runtime_ui_push_document(Null) != Err) {
        log_err("runtime_ui_push_document should reject null document paths");
        ++failed;
    }

    if (runtime_ui_pop_document("hud") != Err) {
        log_err("runtime_ui_pop_document should reject calls while runtime is inactive");
        ++failed;
    }

    if (runtime_ui_pop_document(Null) != Err) {
        log_err("runtime_ui_pop_document should reject null document ids");
        ++failed;
    }

    if (runtime_ui_set_document_layer("hud", 100) != Err) {
        log_err("runtime_ui_set_document_layer should reject calls while runtime is inactive");
        ++failed;
    }

    if (runtime_ui_set_document_layer(Null, 100) != Err) {
        log_err("runtime_ui_set_document_layer should reject null document ids");
        ++failed;
    }

    if (runtime_ui_bring_document_to_front("hud") != Err) {
        log_err("runtime_ui_bring_document_to_front should reject calls while runtime is inactive");
        ++failed;
    }

    if (runtime_ui_bring_document_to_front(Null) != Err) {
        log_err("runtime_ui_bring_document_to_front should reject null document ids");
        ++failed;
    }

    if (runtime_ui_send_document_to_back("hud") != Err) {
        log_err("runtime_ui_send_document_to_back should reject calls while runtime is inactive");
        ++failed;
    }

    if (runtime_ui_send_document_to_back(Null) != Err) {
        log_err("runtime_ui_send_document_to_back should reject null document ids");
        ++failed;
    }

    usize document_count = 999;
    if (runtime_ui_get_document_count(&document_count) != Err) {
        log_err("runtime_ui_get_document_count should reject calls while runtime is inactive");
        ++failed;
    }

    if (runtime_ui_get_document_count(Null) != Err) {
        log_err("runtime_ui_get_document_count should reject null count outputs");
        ++failed;
    }

    if (runtime_ui_get_document_id_at(0) != Null) {
        log_err("runtime_ui_get_document_id_at should return null while runtime is inactive");
        ++failed;
    }

    record_test_result("Runtime loader tests", failed);
#endif // Testing
}
