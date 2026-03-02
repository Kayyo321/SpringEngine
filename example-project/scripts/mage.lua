local Engine = require("Engine")
local Actor = require("Engine.Actor")
local Scene = require("Engine.Scene")
local Time = require("Engine.Time")
local Transform = require("Engine.Transform")

local mage = {
    target_tag = "player",
    target_actor_id = nil,
    player_module_alias = "player-controller",
    spawn_distance = 650.0,
    spawn_height_offset = -36.0,
    fly_speed = 170.0,
    exit_speed_multiplier = 2.0,
    hit_damage_min = 15,
    hit_damage_max = 25,
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
    cached_collider = nil,
    cached_anim_conf = nil,
    cached_player_script = nil,
    cached_player_script_actor_id = nil,
    facing_rotation_offset_degrees = 0.0,
    use_rotation_facing = true,
}

local function get_hud_stats()
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

local function set_flight_rotation(self, direction_x, direction_y)
    if not Transform.set_rotation_euler then
        return
    end

    if math.abs(direction_x) <= 0.0001 and math.abs(direction_y) <= 0.0001 then
        return
    end

    local angle_degrees = math.deg(math.atan2(direction_y, math.abs(direction_x))) + (self.facing_rotation_offset_degrees or 0.0)
    Transform.set_rotation_euler(0.0, 0.0, angle_degrees)
end

local function find_player_actor(self)
    if Scene.find_by_id and self.target_actor_id and self.target_actor_id ~= "" then
        local actor = Scene.find_by_id(self.target_actor_id)
        if actor and actor.id and actor.id ~= "" then
            return actor
        end
    end

    if Scene.find_first_by_tag and self.target_tag and self.target_tag ~= "" then
        local actor = Scene.find_first_by_tag(self.target_tag, false, false)
        if actor and actor.id and actor.id ~= "" then
            if self.target_actor_id ~= actor.id then
                self.cached_player_script = nil
                self.cached_player_script_actor_id = nil
            end
            self.target_actor_id = actor.id
            return actor
        end
    end

    return nil
end

local function get_player_position(self)
    local player_actor_id = self.target_actor_id
    if not (player_actor_id and player_actor_id ~= "") then
        local player_actor = find_player_actor(self)
        player_actor_id = player_actor and player_actor.id or nil
    end

    if Actor.get_position and player_actor_id and player_actor_id ~= "" then
        local x, y, z = Actor.get_position(player_actor_id)
        if x and y and z then
            return x, y, z
        end
    end

    return nil, nil, nil
end

local function set_visual_facing(self, moving_x)
    local anim_conf = self.cached_anim_conf
    if not anim_conf and self.get_component then
        anim_conf = self.get_component("AnimConf")
        self.cached_anim_conf = anim_conf
    end

    if anim_conf and anim_conf.set_flip_x then
        if self.use_rotation_facing then
            anim_conf.set_flip_x(moving_x < 0.0)
            return
        end

        anim_conf.set_flip_x(moving_x < 0.0)
    end
end

local function get_player_script(self, actor_id)
    if not (self.get_component and actor_id and actor_id ~= "") then
        return nil
    end

    if self.cached_player_script and self.cached_player_script_actor_id == actor_id then
        return self.cached_player_script
    end

    self.cached_player_script = self.get_component(self.player_module_alias, actor_id)
    self.cached_player_script_actor_id = actor_id
    return self.cached_player_script
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

local check_player_touch

local function hit_player(self, current_x, current_y, overlapped_player_id)
    log_message("mage.lua hit_player")

    local player_script = get_player_script(self, overlapped_player_id)
    if player_script and player_script.call then
        local damage = math.random(self.hit_damage_min, self.hit_damage_max)
        local damage_called = player_script.call("take_damage", damage, current_x)
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

check_player_touch = function(self)
    local collider = self.cached_collider
    if not collider and self.get_component then
        collider = self.get_component("Collider")
        self.cached_collider = collider
    end

    if not (collider and collider.overlaps_all and Scene.find_by_id) then
        return nil
    end

    local overlaps = collider.overlaps_all()
    if type(overlaps) ~= "table" then
        return nil
    end

    if self.target_actor_id and self.target_actor_id ~= "" then
        for _, actor_id in ipairs(overlaps) do
            if actor_id == self.target_actor_id then
                return actor_id
            end
        end
    end

    for _, actor_id in ipairs(overlaps) do
        if actor_id and actor_id ~= "" then
            local actor = Scene.find_by_id(actor_id)
            if actor and actor.tags then
                for _, tag in ipairs(actor.tags) do
                    if tag == self.target_tag then
                        self.target_actor_id = actor_id
                        return actor_id
                    end
                end
            end
        end
    end

    return nil
end

function mage:start()
    self.cached_collider = self.get_component and self.get_component("Collider") or nil
    self.cached_anim_conf = self.get_component and self.get_component("AnimConf") or nil

    local player_actor = find_player_actor(self)
    if player_actor and player_actor.id and player_actor.id ~= "" then
        get_player_script(self, player_actor.id)
    end

    local player_x, player_y, player_z = get_player_position(self)
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
    set_flight_rotation(self, self.approach_direction_x, 0.0)

    log_message("mage.lua start")
end

function mage:update()
    local delta_time = Time.delta_time()
    if not delta_time or delta_time <= 0.0 then
        return
    end

    local player_x, player_y, player_z = get_player_position(self)
    if not (player_x and player_y and player_z) then
        return
    end

    local mage_x, mage_y, _ = Transform.get_position()

    if self.state == "approach" then
        local to_player_x = player_x - mage_x
        local to_player_y = player_y - mage_y
        local move_x, move_y = normalize_2d(to_player_x, to_player_y)
        set_flight_rotation(self, move_x, move_y)
        Transform.translate(move_x * self.fly_speed * delta_time, move_y * self.fly_speed * delta_time, 0.0)
        set_visual_facing(self, move_x)

        local updated_x, updated_y, _ = Transform.get_position()
        local touched_player_id = check_player_touch(self)

        local crossed_player = false
        if self.approach_direction_x > 0.0 then
            crossed_player = updated_x >= (player_x + self.miss_distance)
        else
            crossed_player = updated_x <= (player_x - self.miss_distance)
        end

        if touched_player_id then
            hit_player(self, updated_x, updated_y, touched_player_id)
        elseif crossed_player then
            miss_player(self, updated_x, updated_y)
        end
    else
        local move_x = self.exit_direction_x
        local desired_y = self.exit_target_y
        local move_y = desired_y - mage_y
        local normalized_x, normalized_y = normalize_2d(move_x, move_y)
        local exit_speed = self.fly_speed * self.exit_speed_multiplier
        set_flight_rotation(self, normalized_x, normalized_y)
        Transform.translate(normalized_x * exit_speed * delta_time, normalized_y * exit_speed * delta_time, 0.0)
        set_visual_facing(self, normalized_x)
    end

    local updated_x, _, _ = Transform.get_position()
    if self.has_crossed_player and math.abs(updated_x - self.exit_origin_x) >= self.despawn_distance then
        self:destroy()
    end
end

function mage:on_destroy()
    local stats = get_hud_stats()
    stats.mages_cleared = (stats.mages_cleared or 0) + 1
    log_message("mage.lua on_destroy")
end

return mage
