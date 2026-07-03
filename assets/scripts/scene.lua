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
local exitZ = -7.6 -- past the doorway = room complete
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
    local pressedNow = forge.physics.overlap(plate.center, plate.half) > 0
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
