local Engine = require("Engine")
local Input = require("Engine.Input")
local Transform = require("Engine.Transform")
local Rigidbody = require("Engine.Rigidbody")
local DJ = require("Engine.DJ")
local Time = require("Engine.Time")

local player = {
	move_speed = 45.0,
	jump_force = 78.0,
	apex_sfx_played = true,
	apex_velocity_threshold = 6.0,
}

local function log_message(message)
	if Engine.log then
		Engine.log(message)
	elseif print then
		print(message)
	end
end

local function play_stomp_sfx()
	if DJ.play_sound then
		DJ.play_sound("stomp-sfx")
	end
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
	self.apex_sfx_played = true

	if Rigidbody.set_velocity then
		Rigidbody.set_velocity(0.0, 0.0)
	end

	if DJ.load_sound then
		DJ.load_sound("assets/Sounds/stomp-sfx.wav", "stomp-sfx")
	end
	log_message("player.lua start")
end

function player:update()
	local delta_time = get_delta_time()
	local move_x = read_move_input()
	local run_multiplier = 1.0
	if Input.accepted and Input.accepted("Run") then
		run_multiplier = 2.0
	end

	local effective_move_x = move_x * run_multiplier
	local horizontal_speed = math.abs(effective_move_x)
	local is_moving = horizontal_speed > 0.001

	local anim_conf = self.get_component and self.get_component("AnimConf") or nil
	if anim_conf and anim_conf.set then
		anim_conf.set("moving", is_moving)
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
		self.apex_sfx_played = false
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

		if anim_conf.get_number and (not self.apex_sfx_played) then
			local config_vertical_speed = anim_conf.get_number("vertical_speed")
			local config_grounded = anim_conf.get_number("grounded")

			if config_vertical_speed and config_grounded and config_grounded < 0.5 and math.abs(config_vertical_speed) <= self.apex_velocity_threshold then
				play_stomp_sfx()
				self.apex_sfx_played = true
			end
		end
	end
end

function player:on_destroy()
	log_message("player.lua on_destroy")
end

return player
