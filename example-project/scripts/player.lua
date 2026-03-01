local Engine = require("Engine")
local Input = require("Engine.Input")
local Transform = require("Engine.Transform")
local DJ = require("Engine.DJ")
local Time = require("Engine.Time")

local player = {
	move_speed = 6.0,
	jump_speed = 8.0,
	gravity = -24.0,
	vertical_velocity = 0.0,
	is_grounded = false,
	air_time = 0.0,
	max_air_time = 4.0,
	last_schema = nil,
	action_was_active = {},
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
	local move_horizontal = 0.0
	local move_forward = 0.0

	if Input.accepted then
		if Input.accepted("MoveLeft") then move_horizontal = move_horizontal - 1.0 end
		if Input.accepted("MoveRight") then move_horizontal = move_horizontal + 1.0 end
		if Input.accepted("MoveUp") then move_forward = move_forward + 1.0 end
		if Input.accepted("MoveDown") then move_forward = move_forward - 1.0 end
	elseif Input.is_key_down then
		if Input.is_key_down("A") then move_horizontal = move_horizontal - 1.0 end
		if Input.is_key_down("D") then move_horizontal = move_horizontal + 1.0 end
		if Input.is_key_down("W") then move_forward = move_forward + 1.0 end
		if Input.is_key_down("S") then move_forward = move_forward - 1.0 end
	end

	return move_horizontal, move_forward
end

local function get_delta_time()
	return Time.delta_time()
end

local function current_schema_name()
	if Input.current_schema then
		local name = Input.current_schema()
		if name then
			return name
		end
	end

	return "<none>"
end

local function toggle_input_schema()
	if not (Input.was_key_pressed and Input.set_schema and Input.current_schema) then
		return
	end

	if not Input.was_key_pressed("tab") then
		return
	end

	local current = Input.current_schema()
	if current == "Menu" then
		if Input.set_schema("Normal") then
			log_message("Input schema switched to Normal")
		end
	else
		if Input.set_schema("Menu") then
			log_message("Input schema switched to Menu")
		end
	end
end

local function log_schema_inputs()
	if not (Input.accepted and Input.current_schema) then
		return
	end

	local schema = Input.current_schema()
	if not schema then
		return
	end

	if schema ~= player.last_schema then
		player.last_schema = schema
		player.action_was_active = {}
		log_message("Current input schema: " .. schema)
	end

	local function log_when_action_turns_on(action_name, label)
		local is_active = Input.accepted(action_name)
		local was_active = player.action_was_active[action_name] == true

		if is_active and not was_active then
			log_message(label)
		end

		player.action_was_active[action_name] = is_active
	end

	if schema == "Normal" then
		log_when_action_turns_on("MoveUp", "Normal input: W (MoveUp)")
		log_when_action_turns_on("MoveDown", "Normal input: S (MoveDown)")
		log_when_action_turns_on("MoveLeft", "Normal input: A (MoveLeft)")
		log_when_action_turns_on("MoveRight", "Normal input: D (MoveRight)")
		log_when_action_turns_on("Jump", "Normal input: SPACE (Jump)")
	elseif schema == "Menu" then
		log_when_action_turns_on("MoveUp", "Menu input: UP (MoveUp)")
		log_when_action_turns_on("MoveDown", "Menu input: DOWN (MoveDown)")
		log_when_action_turns_on("Confirm", "Menu input: ENTER (Confirm)")
		log_when_action_turns_on("Back", "Menu input: ESCAPE (Back)")
	end
end

function player:awake()
	log_message("player.lua awake")
end

function player:start()
	self.vertical_velocity = 0.0
	self.is_grounded = true
	self.air_time = 0.0
	self.last_schema = current_schema_name()
	if DJ.load_sound then
		DJ.load_sound("assets/Sounds/stomp-sfx.wav", "stomp-sfx")
	end
	log_message("player.lua start")
	log_message("Current input schema: " .. self.last_schema)
	log_message("Press TAB to toggle input schema (Normal/Menu)")
end

function player:update()
	toggle_input_schema()
	log_schema_inputs()

	local delta_time = get_delta_time()
	local move_horizontal, move_forward = read_move_input()

	if Transform.translate then
		Transform.translate(
			move_horizontal * self.move_speed * delta_time,
			0.0,
			move_forward * self.move_speed * delta_time
		)
	end

	local jump_pressed = false
	if Input.accepted and Input.accepted("Jump") then
		jump_pressed = true
	elseif Input.was_key_pressed and Input.was_key_pressed("SPACE") then
		jump_pressed = true
	end

	if jump_pressed and self.is_grounded then
		self.vertical_velocity = self.jump_speed
		self.is_grounded = false
		self.air_time = 0.0
		play_stomp_sfx()
	end

	if self.is_grounded then
		self.air_time = 0.0
	else
		self.air_time = self.air_time + delta_time
		if self.air_time >= self.max_air_time then
			self.is_grounded = true
			self.air_time = 0.0
			self.vertical_velocity = 0.0
		end
	end

	self.vertical_velocity = self.vertical_velocity + self.gravity * delta_time

	if Transform.translate then
		Transform.translate(0.0, self.vertical_velocity * delta_time, 0.0)
	end

	if Transform.get_position and Transform.set_position then
		local position_x, position_y, position_z = Transform.get_position()
		if position_y <= 0.0 then
			Transform.set_position(position_x, 0.0, position_z)
			self.vertical_velocity = 0.0
			self.is_grounded = true
		end
	end
end

function player:on_destroy()
	log_message("player.lua on_destroy")
end

return player
