#include "runtime_loader_internal.h"

result runtime_ui_node_exists(const char *document_id, const char *node_id) {
    RUNTIME_GUARD_UI_ERR();
    if (!document_id || !node_id)
        return Err;

    return ui_runtime_node_exists(runtime_state.ui_runtime, document_id, node_id);
}

result runtime_ui_set_visible(const char *document_id, const char *node_id, boolean visible) {
    RUNTIME_GUARD_UI_ERR();
    if (!document_id || !node_id)
        return Err;

    return ui_runtime_set_node_visible(runtime_state.ui_runtime, document_id, node_id, visible);
}

result runtime_ui_set_text(const char *document_id, const char *node_id, const char *text) {
    RUNTIME_GUARD_UI_ERR();
    if (!document_id || !node_id || !text)
        return Err;

    return ui_runtime_set_node_text(runtime_state.ui_runtime, document_id, node_id, text);
}

result runtime_ui_push_document(const char *document_path) {
    RUNTIME_GUARD_UI_ERR();
    if (!document_path)
        return Err;

    return ui_runtime_push_document(runtime_state.ui_runtime, document_path);
}

result runtime_ui_pop_document(const char *document_id) {
    RUNTIME_GUARD_UI_ERR();
    if (!document_id)
        return Err;

    return ui_runtime_pop_document(runtime_state.ui_runtime, document_id);
}

result runtime_ui_set_document_layer(const char *document_id, int layer) {
    RUNTIME_GUARD_UI_ERR();
    if (!document_id)
        return Err;

    return ui_runtime_set_document_layer(runtime_state.ui_runtime, document_id, layer);
}

result runtime_ui_bring_document_to_front(const char *document_id) {
    RUNTIME_GUARD_UI_ERR();
    if (!document_id)
        return Err;

    return ui_runtime_bring_document_to_front(runtime_state.ui_runtime, document_id);
}

result runtime_ui_send_document_to_back(const char *document_id) {
    RUNTIME_GUARD_UI_ERR();
    if (!document_id)
        return Err;

    return ui_runtime_send_document_to_back(runtime_state.ui_runtime, document_id);
}

result runtime_ui_get_document_count(usize *out_count) {
    RUNTIME_GUARD_UI_ERR();
    if (!out_count)
        return Err;

    return ui_runtime_get_document_count(runtime_state.ui_runtime, out_count);
}

const char *runtime_ui_get_document_id_at(usize index) {
    RUNTIME_GUARD_UI_NULL();

    const char *document_id = Null;
    if (ui_runtime_get_document_id_at(runtime_state.ui_runtime, index, &document_id) != Ok)
        return Null;

    return document_id;
}
