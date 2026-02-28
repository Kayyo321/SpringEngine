local crate = {
    move_speed = 4.0,
}

local function log_message(message)
    if Engine and Engine.log then
        Engine.log(message)
    elseif print then
        print(message)
    end
end

local function get_delta_time()
    if Time and Time.delta_time then
        return Time.delta_time()
    end

    return 1.0 / 60.0
end

function crate:awake()
    log_message("Crate awake")
end

function crate:start()
    log_message("Crate start")
end

function crate:update() 
    local move_x = 0.0
    local move_y = 0.0

    if Input and Input.accepted then
        if Input.accepted("MoveLeft") then move_x = move_x - 1.0 end
        if Input.accepted("MoveRight") then move_x = move_x + 1.0 end
        if Input.accepted("MoveUp") then move_y = move_y - 1.0 end
        if Input.accepted("MoveDown") then move_y = move_y + 1.0 end
    elseif Input and Input.is_key_down then
        if Input.is_key_down("A") then move_x = move_x - 1.0 end
        if Input.is_key_down("D") then move_x = move_x + 1.0 end
        if Input.is_key_down("W") then move_y = move_y - 1.0 end
        if Input.is_key_down("S") then move_y = move_y + 1.0 end
    end

    if Transform and Transform.translate then
        local delta_time = get_delta_time()
        Transform.translate(
            move_x * self.move_speed * delta_time,
            move_y * self.move_speed * delta_time,
            0.0
        )
    end
end

function crate:on_destroy()
    log_message("Crate on_destroy")
end

return crate