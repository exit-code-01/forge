-- assets/scripts/scene.lua — VAULT tutorial room: week-2 puzzle vocabulary.
-- The ENTIRE puzzle is data + logic in this file, hot-reloadable while the
-- game runs. Tune the wiring table and save — no rebuild, no restart.

-- ---- Puzzle wiring (designer-tunable) ------------------------------------
local plate = {
    center = vec3(-3.0, 0.35, 2.0), -- sits just above the plate slab
    half   = vec3(0.7, 0.35, 0.7),
    pressed = false,
}
local door = {
    name   = "Exit Door",
    closed = vec3(0.0, 1.25, -7.2),
    open   = vec3(0.0, 3.55, -7.2), -- slides UP into the wall
    speed  = 2.2,                   -- m/s
}
local laser = {
    origin   = vec3(-6.7, 1.1, -5.0), -- just past the emitter face
    dir      = vec3(1.0, 0.0, 0.0),
    maxDist  = 13.5,
    beamName = "Laser Beam",
    receiver = "Laser Receiver",
    blocked  = false,
}
local glass = {
    name   = "Glass",
    center = vec3(0.0, 1.05, -7.9),
    half   = vec3(1.3, 1.2, 0.4), -- slightly fat: catches incoming crates
    breakSpeed = 4.5,
    broken = false,
}
local exitZ = -8.3 -- past the (former) glass = room complete
-- ---------------------------------------------------------------------------

local spawnCount = 0
local won = false

function onStart()
    forge.log("scene.lua loaded (" .. _VERSION .. ")")
    forge.log("puzzle: put a crate on the plate, walk through the door")
end

local function moveTowards(from, to, maxStep)
    local d = vec3(to.x - from.x, to.y - from.y, to.z - from.z)
    local len = math.sqrt(d.x * d.x + d.y * d.y + d.z * d.z)
    if len <= maxStep or len < 1e-6 then
        return to
    end
    local k = maxStep / len
    return vec3(from.x + d.x * k, from.y + d.y * k, from.z + d.z * k)
end

function onUpdate(dt)
    -- Pressure plate: any dynamic body in the region holds it down.
    local pressedNow = #forge.physics.overlap(plate.center, plate.half) > 0
    if pressedNow ~= plate.pressed then
        plate.pressed = pressedNow
        forge.audio.play(pressedNow and "assets/sounds/land.wav" or "assets/sounds/grab.wav")
        forge.log(pressedNow and "plate: PRESSED - door opening"
                              or "plate: released - door closing")
    end

    -- Door chases its target; the host teleports the collider with it.
    local target = plate.pressed and door.open or door.closed
    local at = forge.scene.getPosition(door.name)
    forge.scene.setPosition(door.name, moveTowards(at, target, door.speed * dt))

    -- Laser: raycast emitter -> receiver; anything in between blocks it.
    -- The beam VISUAL stretches to the first hit (held crates cast a gap).
    local hitBody, hitDist = forge.physics.raycast(laser.origin, laser.dir, laser.maxDist)
    local dist = hitDist or laser.maxDist
    forge.scene.setPosition(laser.beamName,
                            vec3(laser.origin.x + dist * 0.5, laser.origin.y, laser.origin.z))
    forge.scene.setScale(laser.beamName, vec3(dist, 0.05, 0.05))
    local receiverPos = forge.scene.getPosition(laser.receiver)
    local blockedNow = dist < (receiverPos.x - laser.origin.x) - 0.3
    if blockedNow ~= laser.blocked then
        laser.blocked = blockedNow
        forge.audio.play(blockedNow and "assets/sounds/grab.wav" or "assets/sounds/jump.wav")
        forge.log(blockedNow and "laser: BLOCKED" or "laser: circuit restored")
    end

    -- Glass: any dynamic body moving fast enough through its region breaks it.
    if not glass.broken then
        for _, body in ipairs(forge.physics.overlap(glass.center, glass.half)) do
            local v = forge.physics.velocity(body)
            local speed = math.sqrt(v.x * v.x + v.y * v.y + v.z * v.z)
            if speed > glass.breakSpeed then
                glass.broken = true
                forge.scene.destroy(glass.name)
                forge.fx.burst(glass.center, 40)
                forge.audio.play("assets/sounds/shatter.wav")
                forge.log("glass: SHATTERED (impact " .. string.format("%.1f", speed) .. " m/s)")
                break
            end
        end
    end

    -- Win condition: the player walked out through the doorway.
    if not won then
        local p = forge.player.position()
        if p.z < exitZ then
            won = true
            forge.audio.play("assets/sounds/kick.wav")
            forge.fx.burst(vec3(p.x, p.y + 0.5, p.z), 30)
            forge.log("*** ROOM COMPLETE ***")
        end
    end

    -- E: rain extra throwable crates (handy for reaching the plate).
    if forge.input.pressed("e") then
        for i = 1, 3 do
            local at2 = vec3((i - 2) * 0.9, 3.0 + i * 0.6, -1.5)
            forge.physics.spawnBox(vec3(0.25, 0.25, 0.25), at2, true, 0.4)
            forge.fx.burst(at2, 8)
        end
        forge.audio.play("assets/sounds/kick.wav")
        spawnCount = spawnCount + 3
        forge.log("crate drop! total spawned: " .. spawnCount)
    end
end
