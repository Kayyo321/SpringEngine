local Scene = require("Engine.Scene")
local Time = require("Engine.Time")

local trigger = {
	player_tag = "player",
	player_module_alias = "player-controller",
	damage = 20,
	reset_x = 0.0,
	reset_y = 1.0,
	reset_z = 0.0,
	hit_cooldown = 0.2,
	cooldown_remaining = 0.0,
}

function trigger:update()
	if self.cooldown_remaining > 0.0 then
		local delta_time = Time.delta_time and Time.delta_time() or 0.0
		self.cooldown_remaining = math.max(0.0, self.cooldown_remaining - delta_time)
		return
	end

	local collider = self.get_component and self:get_component("Collider") or nil
	if not (collider and collider.overlaps_all and Scene.find_by_id) then
		return
	end

	local overlaps = collider:overlaps_all()
	if type(overlaps) ~= "table" then
		return
	end

	for _, actor_id in ipairs(overlaps) do
		local actor = Scene.find_by_id(actor_id)
		if actor and actor.tags then
			for _, tag in ipairs(actor.tags) do
				if tag == self.player_tag then
					local player_script = self.get_component and self:get_component(self.player_module_alias, actor_id) or nil
					if player_script and player_script.call then
						player_script.call("take_damage", self.damage)
						player_script.call("reset_to_stage_center", self.reset_x, self.reset_y, self.reset_z)
					end
					self.cooldown_remaining = self.hit_cooldown
					return
				end
			end
		end
	end
end

return trigger
