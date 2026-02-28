local player = {
	move_speed = 6.0,
	jump_speed = 8.0,
	gravity = -24.0,
	vertical_velocity = 0.0,
	is_grounded = false,
}

local function log_message(message)
	if Engine and Engine.log then
		Engine.log(message)
	end
end

local function read_move_input()
	local move_horizontal = 0.0
	local move_forward = 0.0

	if Input and Input.is_key_down then
		if Input.is_key_down("A") then move_horizontal = move_horizontal - 1.0 end
		if Input.is_key_down("D") then move_horizontal = move_horizontal + 1.0 end
		if Input.is_key_down("W") then move_forward = move_forward + 1.0 end
		if Input.is_key_down("S") then move_forward = move_forward - 1.0 end
	end

	return move_horizontal, move_forward
end

local function get_delta_time()
	if Time and Time.delta_time then
		return Time.delta_time()
	end

	return 1.0 / 60.0
end

function player:awake()
	log_message("player.lua awake")
end

function player:start()
	self.vertical_velocity = 0.0
	self.is_grounded = true
	log_message("player.lua start")
end

function player:update()
	local delta_time = get_delta_time()
	local move_horizontal, move_forward = read_move_input()

	if Transform and Transform.translate then
		Transform.translate(
			move_horizontal * self.move_speed * delta_time,
			0.0,
			move_forward * self.move_speed * delta_time
		)
	end

	if Input and Input.was_key_pressed and Input.was_key_pressed("SPACE") and self.is_grounded then
		self.vertical_velocity = self.jump_speed
		self.is_grounded = false
	end

	self.vertical_velocity = self.vertical_velocity + self.gravity * delta_time

	if Transform and Transform.translate then
		Transform.translate(0.0, self.vertical_velocity * delta_time, 0.0)
	end

	if Transform and Transform.get_position and Transform.set_position then
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
