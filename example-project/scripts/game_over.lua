local Input = require("Engine.Input")
local Scene = require("Engine.Scene")

local game_over = {}

function game_over:update()
	if Input.was_key_pressed and Input.was_key_pressed("ENTER") and Scene.load then
		Scene.load("starting_scene.scene.conf")
	end
end

return game_over