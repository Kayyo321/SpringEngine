local UI = require("Engine.UI")
local Time = require("Engine.Time")

local Hud = {}

function Hud.on_hud_button_click(self, document_id, node_id)
    local label = UI.find(document_id, "hud_label")
    if not label then
        return
    end

    local elapsed = Time.elapsed_time()
    UI.set_text(label, string.format("Button clicked at %.2fs", elapsed))
end

return Hud
