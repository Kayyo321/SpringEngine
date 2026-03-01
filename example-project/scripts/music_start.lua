local Engine = require("Engine")
local DJ = require("Engine.DJ")

local music_start = {
    started = false,
    music_channel = -1,
}

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
        DJ.load_music(temp_music_path, alias)
    end

    if DJ.play_music then
        self.music_channel = DJ.play_music(alias, true)
    end

    self.started = true
    log_message("music_start.lua loaded and started temp music")
end

function music_start:on_destroy()
    if self.music_channel and self.music_channel >= 0 and DJ.stop_music then
        DJ.stop_music(self.music_channel)
        self.music_channel = -1
    end
end

return music_start
