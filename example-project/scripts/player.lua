local Engine = require("Engine")
local Input = require("Engine.Input")
local Scene = require("Engine.Scene")
local Transform = require("Engine.Transform")
local Rigidbody = require("Engine.Rigidbody")
local DJ = require("Engine.DJ")
local Time = require("Engine.Time")
local UI = require("Engine.UI")

local player = {
	health = 100.0,
	move_speed = 120.0,
	jump_force = 78.0 * 14.0,
	jump_gravity_scale = 5.0,
	stomp_default_volume = 0.1,
	stomp_channel_volume = 0.1,
	facing_x = 1.0,
	is_dead = false,
	is_dying_transition = false,
	death_fade_duration = 0.45,
	death_fade_elapsed = 0.0,
	death_scene_requested = false,
	hud_hidden_for_transition = false,
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

local function play_stomp_sfx()
	if DJ.play_sound then
		local channel = DJ.play_sound("stomp-sfx")
		if channel and channel >= 0 and DJ.set_sound_channel_volume then
			DJ.set_sound_channel_volume(channel, player.stomp_channel_volume)
		end
	end
end

local function fade_music_for_game_over(t)
	if not DJ.set_music_channel_volume then
		return
	end

	local music_state = _G.__springengine_music_state
	if type(music_state) ~= "table" then
		return
	end

	local channel = music_state.channel
	if not channel or channel < 0 then
		return
	end

	local base_volume = music_state.base_volume or 1.0
	local next_volume = base_volume * (1.0 - t)
	if next_volume < 0.0 then
		next_volume = 0.0
	end

	DJ.set_music_channel_volume(channel, next_volume)
end

local function read_move_input()
	local move_x = 0.0

	if Input.accepted then
		if Input.accepted("MoveLeft") then move_x = move_x - 1.0 end
		if Input.accepted("MoveRight") then move_x = move_x + 1.0 end
	end

	return move_x
end

local function get_delta_time()
	return Time.delta_time()
end

function player:awake()
	log_message("player.lua awake")
end

function player:start()
	self.facing_x = 1.0
	self.is_dead = false
	self.is_dying_transition = false
	self.death_fade_elapsed = 0.0
	self.death_scene_requested = false
	self.hud_hidden_for_transition = false

	local stats = get_hud_stats()
	stats.player_max_health = self.health
	stats.player_health = self.health

	if Rigidbody.set_velocity then
		Rigidbody.set_velocity(0.0, 0.0)
	end

	if Rigidbody.set_gravity_scale then
		Rigidbody.set_gravity_scale(self.jump_gravity_scale)
	end

	if DJ.load_sound then
		DJ.load_sound("assets/Sounds/stomp-sfx.wav", "stomp-sfx", self.stomp_default_volume)
	end
	log_message("player.lua start")
end

function player:update()
	if self.is_dead and self.is_dying_transition then
		self.death_fade_elapsed = self.death_fade_elapsed + get_delta_time()
		local t = self.death_fade_elapsed / self.death_fade_duration
		if t > 1.0 then
			t = 1.0
		end

		local alpha = math.floor((t * 255.0) + 0.5)
		local fader = self.get_component and self:get_component("StaticColor", "scene_fader") or nil
		if fader and fader.set_color then
			fader:set_color(0, 0, 0, alpha)
		end

		fade_music_for_game_over(t)

		if not self.hud_hidden_for_transition and UI.find and UI.set_visible then
			local hud_root = UI.find("hud", "root_canvas")
			if hud_root then
				UI.set_visible(hud_root, false)
			end
			self.hud_hidden_for_transition = true
		end

		if t >= 1.0 and not self.death_scene_requested and Scene.load then
			self.death_scene_requested = true
			Scene.load("game_over.scene.conf")
		end

		return
	end

	if self.is_dead then
		return
	end

	local delta_time = get_delta_time()
	local move_x = read_move_input()
	local run_multiplier = 1.0
	if Input.accepted and Input.accepted("Run") then
		run_multiplier = 2.0
	end

	local effective_move_x = move_x * run_multiplier
	local horizontal_speed = math.abs(effective_move_x)
	local is_moving = horizontal_speed > 0.001

	if move_x < -0.001 then
		self.facing_x = -1.0
	elseif move_x > 0.001 then
		self.facing_x = 1.0
	end

	local anim_conf = self.get_component and self.get_component("AnimConf") or nil
	if anim_conf and anim_conf.set then
		anim_conf.set("moving", is_moving)
	end

	if anim_conf and anim_conf.set_flip_x then
		anim_conf.set_flip_x(self.facing_x < 0.0)
	end

	if Transform.translate then
		Transform.translate(
			effective_move_x * self.move_speed * delta_time,
			0.0,
			0.0
		)
	end

	local jump_pressed = false
	if Input.accepted and Input.accepted("Jump") then
		jump_pressed = true
	elseif Input.was_key_pressed and Input.was_key_pressed("SPACE") then
		jump_pressed = true
	end

	local is_grounded = true
	if Rigidbody.is_grounded then
		is_grounded = Rigidbody.is_grounded()
	end

	if jump_pressed and is_grounded and Rigidbody.apply_force then
		Rigidbody.apply_force(0.0, -1.0, self.jump_force)
		play_stomp_sfx()
	end

	local vertical_velocity = 0.0
	if Rigidbody.get_velocity then
		local _, velocity_y = Rigidbody.get_velocity()
		vertical_velocity = velocity_y or 0.0
	end

	if anim_conf and anim_conf.set_number then
		anim_conf.set_number("speed", horizontal_speed)
		anim_conf.set_number("vertical_speed", vertical_velocity)
		anim_conf.set_number("grounded", is_grounded and 1.0 or 0.0)
	end
end

function player:on_destroy()
	log_message("player.lua on_destroy")
end

function player:take_damage(amount) 
	self.health = self.health - amount
	local stats = get_hud_stats()
	stats.player_health = self.health
	stats.player_max_health = math.max(stats.player_max_health or self.health, self.health)
	log_message("Player took damage, health now: " .. tostring(self.health))

	if self.health <= 0.0 then
		self:die()
	end
end

function player:die()
	if self.is_dead then
		return
	end

	self.is_dead = true
	self.is_dying_transition = true
	self.death_fade_elapsed = 0.0
	self.death_scene_requested = false
	log_message("Player has died.")
end

function player:reset_to_stage_center(x, y, z)
	if Transform.set_position then
		Transform.set_position(x or 0.0, y or 1.0, z or 0.0)
	end

	if Rigidbody.set_velocity then
		Rigidbody.set_velocity(0.0, 0.0)
	end
end

return player
