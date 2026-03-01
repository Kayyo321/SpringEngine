local Engine = require("Engine")
local Actor = require("Engine.Actor")
local Camera = require("Engine.Camera")

local camera_follow = {
    target_actor_id = "crate_001",
    follow_lerp = 0.013,
    follow_offset = { x = 0.0, y = 5.0, z = 12.0 },
}

local function log_message(message)
    if Engine.log then
        Engine.log(message)
    elseif print then
        print(message)
    end
end

local function clamp(value, min_value, max_value)
    if value < min_value then
        return min_value
    end

    if value > max_value then
        return max_value
    end

    return value
end

function camera_follow:start()
    local x, y, z = Actor.get_position(self.target_actor_id)
    if x and y and z then
        Camera.set_target(x, y, z)
    end

    log_message("camera_follow.lua start")
end

function camera_follow:update()
    local x, y, z = Actor.get_position(self.target_actor_id)
    if not (x and y and z) then
        return
    end

    Camera.lerp_towards_actor(
        self.target_actor_id,
        clamp(self.follow_lerp, 0.0, 1.0),
        self.follow_offset.x,
        self.follow_offset.y,
        self.follow_offset.z
    )

    Camera.set_target(x, y, z)
end

return camera_follow
