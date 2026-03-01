local Engine = require("Engine")
local Actor = require("Engine.Actor")
local Camera = require("Engine.Camera")
local Time = require("Engine.Time")

local camera_follow = {
    target_actor_id = "crate_001",
    follow_speed = 0.03,
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

local function get_delta_time()
    return Time.delta_time()
end

local function smoothing_alpha(speed, delta_time)
    if speed <= 0.0 then
        return 1.0
    end

    local alpha = 1.0 - math.exp(-speed * delta_time)
    return clamp(alpha, 0.0, 1.0)
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

    local desired_x = x + self.follow_offset.x
    local desired_y = y + self.follow_offset.y
    local desired_z = z + self.follow_offset.z

    local current_x, current_y, current_z = Camera.get_position()
    local alpha = smoothing_alpha(self.follow_speed, get_delta_time())

    Camera.set_position(
        current_x + (desired_x - current_x) * alpha,
        current_y + (desired_y - current_y) * alpha,
        current_z + (desired_z - current_z) * alpha
    )

    Camera.set_target(x, y, z)
end

return camera_follow
