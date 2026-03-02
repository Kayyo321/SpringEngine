local Engine = require("Engine")
local DJ = require("Engine.DJ")

local music_start = {
    started = false,
    music_channel = -1,
    music_default_volume = 0.25,
    music_channel_volume = 0.25,
}

local function get_music_state()
    if type(_G.__springengine_music_state) ~= "table" then
        _G.__springengine_music_state = {
            channel = -1,
            base_volume = 1.0,
        }
    end

    return _G.__springengine_music_state
end

local function log_message(message)
    if Engine.log then
        Engine.log(message)
    elseif print then
        print(message)
    end
end

function music_start:start()
    if self.started then
        return
    end

    local temp_music_path = "assets/Sounds/ice-loop.mp3"
    local alias = "ice-loop"

    if DJ.load_music then
        DJ.load_music(temp_music_path, alias, self.music_default_volume)
    else
        log_message("DJ.load_music is unavailable")
    end

    if DJ.play_music then
        self.music_channel = DJ.play_music(alias, true)
    else
        log_message("DJ.play_music is unavailable")
    end

    if self.music_channel and self.music_channel >= 0 and DJ.set_music_channel_volume then
        DJ.set_music_channel_volume(self.music_channel, self.music_channel_volume)

        if DJ.get_music_channel_volume then
            local applied_volume = DJ.get_music_channel_volume(self.music_channel)
            log_message(
                "music_start channel="
                    .. tostring(self.music_channel)
                    .. " default_volume="
                    .. tostring(self.music_default_volume)
                    .. " channel_volume="
                    .. tostring(self.music_channel_volume)
                    .. " applied="
                    .. tostring(applied_volume)
            )
        end

        local music_state = get_music_state()
        music_state.channel = self.music_channel
        music_state.base_volume = self.music_channel_volume
    elseif self.music_channel and self.music_channel >= 0 then
        log_message("DJ.set_music_channel_volume is unavailable")
    end

    self.started = true
    log_message("music_start.lua loaded and started music")
end

function music_start:on_destroy()
    if self.music_channel and self.music_channel >= 0 and DJ.stop_music then
        DJ.stop_music(self.music_channel)

        local music_state = get_music_state()
        music_state.channel = -1
        self.music_channel = -1
    end
end

return music_start
