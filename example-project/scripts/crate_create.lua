local Engine = require("Engine")
local Scene = require("Engine.Scene")
local Transform = require("Engine.Transform")
local Actor = require("Engine.Actor")

local crate_create = {
	target_actor_id = "player_spawn",
    crate_count = 6,
    spacing = 96.0 * 2.0,
    y_offset = 140.0,
    spawned = false,
}

local function log_message(message)
    if Engine.log then
        Engine.log(message)
    elseif print then
        print(message)
    end
end

function crate_create:start()
    if self.spawned then
        return
    end

    local player_x, player_y, player_z = nil, nil, nil

    if Actor.get_position and self.target_actor_id and self.target_actor_id ~= "" then
		player_x, player_y, player_z = Actor.get_position(self.target_actor_id)
	end

	if player_x == nil or player_y == nil or player_z == nil then
		player_x, player_y, player_z = Transform.get_position()
	end

    player_x = player_x or 0.0
    player_y = player_y or 0.0
    player_z = player_z or 0.0

    local floor_y = player_y + self.y_offset
    local start_x = player_x - ((self.crate_count - 1) * self.spacing * 0.5)

    for index = 0, self.crate_count - 1 do
        local spawn_x = start_x + (index * self.spacing)
        Scene.instantiate_prefab("Crate", spawn_x, floor_y, player_z)
    end

    self.spawned = true
    log_message("crate_create spawned a crate floor beneath player")
end

return crate_create
