local Input = require("Engine.Input")
local Scene = require("Engine.Scene")
local Time = require("Engine.Time")

local game_over = {
	pulse_speed = 1.6,
	min_gray = 0.0,
	max_gray = 40.0,
}

local function lerp(a, b, t)
	return a + ((b - a) * t)
end

function game_over:update()
	local background = self.get_component and self:get_component("StaticColor", "game_over_background") or nil
	if background and background.set_color then
		local elapsed = Time.elapsed_time and Time.elapsed_time() or 0.0
		local phase = (math.sin(elapsed * self.pulse_speed) * 0.5) + 0.5
		local gray = lerp(self.min_gray, self.max_gray, phase)
		background:set_color(math.floor(gray + 0.5), math.floor(gray + 0.5), math.floor(gray + 0.5), 255)
	end

	if Input.was_key_pressed and Input.was_key_pressed("ENTER") and Scene.load then
		Scene.load("starting_scene.scene.conf")
	end
end

return game_over