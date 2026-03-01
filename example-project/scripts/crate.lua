local Engine = require("Engine")
local crate = {}

local function log_message(message)
    if Engine.log then
        Engine.log(message)
    elseif print then
        print(message)
    end
end

function crate:awake()
    log_message("Crate awake")
end

function crate:start()
    log_message("Crate start")
end

function crate:update() 
end

function crate:on_destroy()
    log_message("Crate on_destroy")
end

return crate