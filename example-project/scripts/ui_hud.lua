local UI = require("Engine.UI")
local Time = require("Engine.Time")

local Hud = {}

local function has_document(target_id)
    local documents = UI.current_documents()
    for _, document_id in ipairs(documents) do
        if document_id == target_id then
            return true
        end
    end

    return false
end

function Hud.on_open_menu_click(self, document_id, node_id)
    if not has_document("pause_menu") then
        UI.push_document("pause_menu.ui.conf")
        UI.set_document_layer("pause_menu", 100)
    end

    local label = UI.find(document_id, "hud_label")
    if not label then
        return
    end

    local elapsed = Time.elapsed_time()
    UI.set_text(label, string.format("Menu opened at %.2fs", elapsed))
end

function Hud.on_close_menu_click(self, document_id, node_id)
    UI.pop_document(document_id)

    local hud_label = UI.find("hud", "hud_label")
    if not hud_label then
        return
    end

    local elapsed = Time.elapsed_time()
    UI.set_text(hud_label, string.format("Menu closed at %.2fs", elapsed))
end

return Hud
