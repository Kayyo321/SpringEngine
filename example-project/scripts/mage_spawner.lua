local Engine = require("Engine")
local Scene = require("Engine.Scene")
local Time = require("Engine.Time")

local mage_spawner = {
    prefab_id = "Mage",
    initial_delay = 1.0,
    spawn_interval = 4.5 * 1.5,
    spawn_timer = 0.0,
}

local function log_message(message)
    if Engine.log then
        Engine.log(message)
    elseif print then
        print(message)
    end
end

local function spawn_mage(self)
    local spawned_actor = Scene.instantiate_prefab(self.prefab_id)
    if not spawned_actor then
        log_message("mage_spawner: failed to instantiate Mage prefab")
        return
    end

    local spawned_id = spawned_actor.id or "<unknown>"
    log_message("mage_spawner: spawned " .. spawned_id)
end

function mage_spawner:start()
    self.spawn_timer = self.initial_delay
    log_message("mage_spawner.lua start")
end

function mage_spawner:update()
    local delta_time = Time.delta_time()
    if not delta_time or delta_time <= 0.0 then
        return
    end

    self.spawn_timer = self.spawn_timer - delta_time
    if self.spawn_timer > 0.0 then
        return
    end

    spawn_mage(self)
    self.spawn_timer = self.spawn_interval
end

return mage_spawner