local UI = require("Engine.UI")
local Scene = require("Engine.Scene")
local Time = require("Engine.Time")

local Hud = {}

local function get_stats()
    if type(_G.__springengine_hud_stats) ~= "table" then
        _G.__springengine_hud_stats = {
            started_at = Time.elapsed_time(),
            player_health = 100.0,
            player_max_health = 100.0,
            mages_spawned = 0,
            mages_cleared = 0,
        }
    end

    return _G.__springengine_hud_stats
end

local function set_label(node_id, text)
    local handle = UI.find("hud", node_id)
    if handle then
        UI.set_text(handle, text)
    end
end

local function format_time(seconds)
    local safe_seconds = math.max(0, math.floor(seconds))
    local minutes = math.floor(safe_seconds / 60)
    local remainder = safe_seconds % 60
    return string.format("%02d:%02d", minutes, remainder)
end

local function format_health_bar(current, max_value)
    local total_slots = 12
    local safe_max = math.max(1, max_value)
    local clamped_current = math.max(0, math.min(current, safe_max))
    local ratio = clamped_current / safe_max
    local filled = math.floor((ratio * total_slots) + 0.5)
    local empty = total_slots - filled
    return "[" .. string.rep("#", filled) .. string.rep("-", empty) .. "]"
end

local function current_active_mages(stats)
    local active_from_totals = math.max(0, stats.mages_spawned - stats.mages_cleared)
    if not Scene.find_all_by_tag then
        return active_from_totals
    end

    local enemies = Scene.find_all_by_tag("enemy", false, false)
    if type(enemies) ~= "table" then
        return active_from_totals
    end

    return #enemies
end

local function refresh_hud()
    local stats = get_stats()
    local elapsed = Time.elapsed_time() - (stats.started_at or 0.0)
    local health = stats.player_health or 0.0
    local max_health = stats.player_max_health or 100.0
    local mages_cleared = stats.mages_cleared or 0
    local mages_active = current_active_mages(stats)

    set_label("hud_health_value", string.format("%d / %d", math.floor(health + 0.5), math.floor(max_health + 0.5)))
    set_label("hud_health_bar", format_health_bar(health, max_health))
    set_label("hud_time_survived_value", format_time(elapsed))
    set_label("hud_mages_cleared_value", tostring(mages_cleared))
    set_label("hud_mages_active_value", tostring(mages_active))
end

function Hud:start()
    local stats = get_stats()
    if not stats.started_at then
        stats.started_at = Time.elapsed_time()
    end

    refresh_hud()
end

function Hud:update()
    refresh_hud()
end

function Hud.on_close_menu_click(self, document_id, node_id)
    UI.pop_document(document_id)
end

return Hud
