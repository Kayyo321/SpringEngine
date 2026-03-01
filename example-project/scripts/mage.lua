local Engine = require("Engine")
local Actor = require("Engine.Actor")
local Time = require("Engine.Time")
local Transform = require("Engine.Transform")

local mage = {
    target_actor_id = "player_spawn",
    player_module_alias = "player-controller",
    spawn_distance = 650.0,
    spawn_height_offset = -36.0,
    fly_speed = 170.0,
    exit_speed_multiplier = 2.0,
    hit_damage_min = 8,
    hit_damage_max = 16,
    miss_distance = 28.0,
    despawn_distance = 750.0,
    state = "approach",
    approach_direction_x = 1.0,
    exit_direction_x = -1.0,
    spawn_origin_x = 0.0,
    exit_origin_x = 0.0,
    exit_target_y = 0.0,
    has_crossed_player = false,
    has_touched_player = false,
}

local function log_message(message)
    if Engine.log then
        Engine.log(message)
    elseif print then
        print(message)
    end
end

local function normalize_2d(x, y)
    local length = math.sqrt((x * x) + (y * y))
    if length <= 0.0001 then
        return 0.0, 0.0
    end

    return x / length, y / length
end

local function get_player_position(actor_id)
    if Actor.get_position and actor_id and actor_id ~= "" then
        local x, y, z = Actor.get_position(actor_id)
        if x and y and z then
            return x, y, z
        end
    end

    return nil, nil, nil
end

local function set_visual_facing(self, moving_x)
    local anim_conf = self.get_component and self.get_component("AnimConf") or nil
    if anim_conf and anim_conf.set_flip_x then
        anim_conf.set_flip_x(moving_x < 0.0)
    end
end

local function begin_exit(self, current_x, current_y)
    if self.state == "exit" then
        return
    end

    self.state = "exit"
    self.has_crossed_player = true
    self.exit_origin_x = current_x
    self.exit_target_y = current_y
end

local function hit_player(self, current_x, current_y)
    log_message("mage.lua hit_player")

    local player_script = self.get_component and self.get_component(self.player_module_alias, self.target_actor_id) or nil
    if player_script and player_script.call then
        local damage = math.random(self.hit_damage_min, self.hit_damage_max)
        local damage_called = player_script.call("take_damage", damage)
        if not damage_called then
            log_message("mage.lua hit_player failed to call player.take_damage")
        end
    end

    self.has_touched_player = true
    self.exit_direction_x = -self.approach_direction_x
    begin_exit(self, current_x, current_y)
end

local function miss_player(self, current_x, current_y)
    self.exit_direction_x = self.approach_direction_x
    begin_exit(self, current_x, current_y)
end

local function check_player_touch(self)
    local collider = self.get_component and self.get_component("Collider") or nil
    if not (collider and collider.overlaps_actor) then
        return false
    end

    return collider.overlaps_actor(self.target_actor_id) == true
end

function mage:start()
    local player_x, player_y, player_z = get_player_position(self.target_actor_id)
    if not (player_x and player_y and player_z) then
        player_x, player_y, player_z = Transform.get_position()
    end

    local side = (math.random(0, 1) == 0) and -1.0 or 1.0
    self.approach_direction_x = -side
    self.exit_direction_x = side
    self.spawn_origin_x = player_x + (side * self.spawn_distance)
    self.exit_origin_x = self.spawn_origin_x
    self.exit_target_y = player_y + self.spawn_height_offset
    self.has_crossed_player = false
    self.has_touched_player = false
    self.state = "approach"

    Transform.set_position(self.spawn_origin_x, player_y + self.spawn_height_offset, player_z)
    set_visual_facing(self, self.approach_direction_x)

    log_message("mage.lua start")
end

function mage:update()
    local delta_time = Time.delta_time()
    if not delta_time or delta_time <= 0.0 then
        return
    end

    local player_x, player_y, player_z = get_player_position(self.target_actor_id)
    if not (player_x and player_y and player_z) then
        return
    end

    local mage_x, mage_y, _ = Transform.get_position()

    if self.state == "approach" then
        local to_player_x = player_x - mage_x
        local to_player_y = player_y - mage_y
        local move_x, move_y = normalize_2d(to_player_x, to_player_y)
        Transform.translate(move_x * self.fly_speed * delta_time, move_y * self.fly_speed * delta_time, 0.0)
        set_visual_facing(self, move_x)

        local updated_x, updated_y, _ = Transform.get_position()
        local touched_player = check_player_touch(self)

        local crossed_player = false
        if self.approach_direction_x > 0.0 then
            crossed_player = updated_x >= (player_x + self.miss_distance)
        else
            crossed_player = updated_x <= (player_x - self.miss_distance)
        end

        if touched_player then
            hit_player(self, updated_x, updated_y)
        elseif crossed_player then
            miss_player(self, updated_x, updated_y)
        end
    else
        local move_x = self.exit_direction_x
        local desired_y = self.exit_target_y
        local move_y = desired_y - mage_y
        local normalized_x, normalized_y = normalize_2d(move_x, move_y)
        local exit_speed = self.fly_speed * self.exit_speed_multiplier
        Transform.translate(normalized_x * exit_speed * delta_time, normalized_y * exit_speed * delta_time, 0.0)
        set_visual_facing(self, normalized_x)
    end

    local updated_x, _, _ = Transform.get_position()
    if self.has_crossed_player and math.abs(updated_x - self.exit_origin_x) >= self.despawn_distance then
        self:destroy()
    end
end

function mage:on_destroy()
    log_message("mage.lua on_destroy")
end

return mage
