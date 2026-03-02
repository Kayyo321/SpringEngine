local Input = require("Engine.Input")
local Scene = require("Engine.Scene")
local Time = require("Engine.Time")
local UI = require("Engine.UI")

local game_over = {
	pulse_speed = 1.6,
	min_gray = 0.0,
	max_gray = 40.0,
	fade_duration = 0.45,
	fade_elapsed = 0.0,
	fade_phase = "in",
	restart_requested = false,
}

local function lerp(a, b, t)
	return a + ((b - a) * t)
end

function game_over:start()
	self.fade_elapsed = 0.0
	self.fade_phase = "in"
	self.restart_requested = false
	self.has_fader = false

	local fader = self.get_component and self:get_component("StaticColor", "game_over_fader") or nil
	if fader and fader.set_color then
		self.has_fader = true
	else
		self.fade_phase = "idle"
	end

	if UI.find and UI.set_visible then
		local message = UI.find("game_over", "game_over_message")
		if message then
			UI.set_visible(message, self.fade_phase == "idle")
		end
	end
end

function game_over:update()
	local background = self.get_component and self:get_component("StaticColor", "game_over_background") or nil
	if background and background.set_color then
		local elapsed = Time.elapsed_time and Time.elapsed_time() or 0.0
		local phase = (math.sin(elapsed * self.pulse_speed) * 0.5) + 0.5
		local gray = lerp(self.min_gray, self.max_gray, phase)
		background:set_color(math.floor(gray + 0.5), math.floor(gray + 0.5), math.floor(gray + 0.5), 255)
	end

	local fader = self.get_component and self:get_component("StaticColor", "game_over_fader") or nil
	if self.has_fader and fader and fader.set_color then
		if self.fade_phase == "in" then
			self.fade_elapsed = self.fade_elapsed + (Time.delta_time and Time.delta_time() or 0.0)
			local t = self.fade_elapsed / self.fade_duration
			if t > 1.0 then
				t = 1.0
			end

			local alpha = math.floor(((1.0 - t) * 255.0) + 0.5)
			fader:set_color(0, 0, 0, alpha)

			if t >= 1.0 then
				self.fade_phase = "idle"
				self.fade_elapsed = 0.0
				if UI.find and UI.set_visible then
					local message = UI.find("game_over", "game_over_message")
					if message then
						UI.set_visible(message, true)
					end
				end
			end
		elseif self.fade_phase == "out" then
			self.fade_elapsed = self.fade_elapsed + (Time.delta_time and Time.delta_time() or 0.0)
			local t = self.fade_elapsed / self.fade_duration
			if t > 1.0 then
				t = 1.0
			end

			local alpha = math.floor((t * 255.0) + 0.5)
			fader:set_color(0, 0, 0, alpha)

			if t >= 1.0 and not self.restart_requested then
				self.restart_requested = true
				if Scene.load then
					Scene.load("starting_scene.scene.conf")
				end
			end
		end
	end

	if self.fade_phase == "idle" and Input.was_key_pressed and Input.was_key_pressed("ENTER") then
		if not self.has_fader then
			if Scene.load then
				Scene.load("starting_scene.scene.conf")
			end
			return
		end

		self.fade_phase = "out"
		self.fade_elapsed = 0.0
		if UI.find and UI.set_visible then
			local message = UI.find("game_over", "game_over_message")
			if message then
				UI.set_visible(message, false)
			end
		end
	end
end

return game_over