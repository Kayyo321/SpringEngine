local UI = require("Engine.UI")
local Time = require("Engine.Time")

local Hud = {}
local state = {
    hud_icon_visible = true,
    lab_layer = 20,
}

local function now_text(prefix)
    return string.format("%s %.2fs", prefix, Time.elapsed_time())
end

local function update_label(document_id, node_id, text)
    local handle = UI.find(document_id, node_id)
    if handle then
        UI.set_text(handle, text)
    end
end

local function has_document(target_id)
    local documents = UI.current_documents()
    for _, document_id in ipairs(documents) do
        if document_id == target_id then
            return true
        end
    end

    return false
end

local function documents_summary()
    local documents = UI.current_documents()
    if #documents == 0 then
        return "Docs: <none>"
    end

    return "Docs: " .. table.concat(documents, ", ")
end

local function refresh_doc_labels()
    local summary = documents_summary()
    update_label("hud", "hud_label", summary)
    update_label("ui_lab", "lab_docs", summary)
end

local function ensure_toast(message)
    if not has_document("toast") then
        UI.push_document("toast.ui.conf")
    end

    UI.bring_to_front("toast")
    update_label("toast", "toast_label", message)
end

function Hud.on_open_menu_click(self, document_id, node_id)
    if not has_document("pause_menu") then
        UI.push_document("pause_menu.ui.conf")
        UI.bring_to_front("pause_menu")
    end

    refresh_doc_labels()

    update_label("hud", "hud_label", now_text("Menu opened at"))
end

function Hud.on_toggle_hud_icon_click(self, document_id, node_id)
    local icon = UI.find("hud", "hud_icon")
    if not icon then
        return
    end

    state.hud_icon_visible = not state.hud_icon_visible
    UI.set_visible(icon, state.hud_icon_visible)

    local visibility = state.hud_icon_visible and "visible" or "hidden"
    ensure_toast(string.format("HUD icon is now %s", visibility))
    refresh_doc_labels()
end

function Hud.on_refresh_docs_click(self, document_id, node_id)
    refresh_doc_labels()
    ensure_toast("Refreshed active document list")
end

function Hud.on_show_toast_click(self, document_id, node_id)
    ensure_toast(now_text("Toast raised at"))
    refresh_doc_labels()
end

function Hud.on_raise_hud_click(self, document_id, node_id)
    UI.bring_to_front("hud")
    refresh_doc_labels()
    ensure_toast("HUD moved to front")
end

function Hud.on_lower_hud_click(self, document_id, node_id)
    UI.send_to_back("hud")
    refresh_doc_labels()
    ensure_toast("HUD moved to back")
end

function Hud.on_bring_lab_front_click(self, document_id, node_id)
    UI.bring_to_front("ui_lab")
    refresh_doc_labels()
end

function Hud.on_send_lab_back_click(self, document_id, node_id)
    UI.send_to_back("ui_lab")
    refresh_doc_labels()
end

function Hud.on_lab_layer_up_click(self, document_id, node_id)
    state.lab_layer = state.lab_layer + 5
    UI.set_document_layer("ui_lab", state.lab_layer)
    ensure_toast(string.format("ui_lab layer set to %d", state.lab_layer))
    refresh_doc_labels()
end

function Hud.on_lab_layer_down_click(self, document_id, node_id)
    state.lab_layer = state.lab_layer - 5
    UI.set_document_layer("ui_lab", state.lab_layer)
    ensure_toast(string.format("ui_lab layer set to %d", state.lab_layer))
    refresh_doc_labels()
end

function Hud.on_close_toast_click(self, document_id, node_id)
    UI.pop_document("toast")
    refresh_doc_labels()
end

function Hud.on_close_menu_click(self, document_id, node_id)
    UI.pop_document(document_id)
    update_label("hud", "hud_label", now_text("Menu closed at"))
    refresh_doc_labels()
end

return Hud
