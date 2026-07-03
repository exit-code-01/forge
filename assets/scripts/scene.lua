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
local exitZ = -45.2 -- past room 3's glass = run complete

-- ---- Rooms 1-3 (week 3): entire rooms are Lua data, spawned via
-- forge.scene.spawn. Corridors link them going NORTH (-z). Guarded by a
-- GLOBAL so hot reloads never double-build.
local function box(name, pos, scale, tex, solid, dynamic)
    local half = solid and vec3(scale.x * 0.5, scale.y * 0.5, scale.z * 0.5) or vec3(0, 0, 0)
    forge.scene.spawn(name, pos, scale, half, tex, dynamic or false)
end

local function shell(id, z0, z1, w) -- floor + side walls, z0 > z1
    local cz, L = (z0 + z1) * 0.5, z0 - z1
    box(id .. " Floor", vec3(0, -0.2, cz), vec3(w, 0.4, L), "checker", true)
    box(id .. " Wall W", vec3(-w * 0.5, 1.6, cz), vec3(0.4, 3.6, L), "checker", true)
    box(id .. " Wall E", vec3(w * 0.5, 1.6, cz), vec3(0.4, 3.6, L), "checker", true)
end

local function doorwayWall(id, z) -- far wall with a 2.2m gap + header
    box(id .. " FWall W", vec3(-3.05, 1.6, z), vec3(3.9, 3.6, 0.4), "checker", true)
    box(id .. " FWall E", vec3(3.05, 1.6, z), vec3(3.9, 3.6, 0.4), "checker", true)
    box(id .. " FWall Top", vec3(0, 3.0, z), vec3(2.2, 0.8, 0.4), "checker", true)
end

local function buildRooms()
    if roomsBuilt then return end
    roomsBuilt = true
    -- corridor A walls (tutorial exit pad is already the floor)
    box("CorA Wall W", vec3(-1.6, 1.6, -8.4), vec3(0.4, 3.6, 2.4), "checker", true)
    box("CorA Wall E", vec3(1.6, 1.6, -8.4), vec3(0.4, 3.6, 2.4), "checker", true)
    -- Room 1: carry the crate to the plate.
    shell("R1", -9.4, -19.4, 10); doorwayWall("R1", -19.4)
    box("Door 1", vec3(0, 1.25, -19.4), vec3(2.2, 2.5, 0.4), "crate", true)
    box("R1 Plate", vec3(3.5, 0.1, -13.0), vec3(1.4, 0.2, 1.4), "laser", true)
    box("Crate R1", vec3(-3.5, 0.4, -17.0), vec3(0.5, 0.5, 0.5), "crate", true, true)
    box("CorB Wall W", vec3(-1.6, 1.6, -20.4), vec3(0.4, 3.6, 2.0), "checker", true)
    box("CorB Wall E", vec3(1.6, 1.6, -20.4), vec3(0.4, 3.6, 2.0), "checker", true)
    box("CorB Floor", vec3(0, -0.2, -20.4), vec3(3.2, 0.4, 2.0), "checker", true)
    -- Room 2: park a crate in the beam (pedestal top y=1.0, beam y=1.1).
    shell("R2", -21.4, -31.4, 10); doorwayWall("R2", -31.4)
    box("Door 2", vec3(0, 1.25, -31.4), vec3(2.2, 2.5, 0.4), "crate", true)
    box("Emitter 2", vec3(-4.6, 1.1, -26.0), vec3(0.3, 0.3, 0.3), "laser", true)
    box("Receiver 2", vec3(4.6, 1.1, -26.0), vec3(0.3, 0.3, 0.3), "laser", true)
    box("Beam 2", vec3(0, 1.1, -26.0), vec3(8.8, 0.05, 0.05), "laser", false)
    box("Pedestal 2", vec3(0, 0.5, -26.0), vec3(1.0, 1.0, 1.0), "checker", true)
    box("Crate R2", vec3(-3.5, 0.4, -23.5), vec3(0.5, 0.5, 0.5), "crate", true, true)
    box("CorC Wall W", vec3(-1.6, 1.6, -32.4), vec3(0.4, 3.6, 2.0), "checker", true)
    box("CorC Wall E", vec3(1.6, 1.6, -32.4), vec3(0.4, 3.6, 2.0), "checker", true)
    box("CorC Floor", vec3(0, -0.2, -32.4), vec3(3.2, 0.4, 2.0), "checker", true)
    -- Room 3: plate opens the door, THROW a crate through the glass.
    shell("R3", -33.4, -43.4, 10); doorwayWall("R3", -43.4)
    box("Door 3", vec3(0, 1.25, -43.4), vec3(2.2, 2.5, 0.4), "crate", true)
    box("R3 Plate", vec3(-3.5, 0.1, -38.0), vec3(1.4, 0.2, 1.4), "laser", true)
    box("Crate R3", vec3(3.5, 0.4, -35.0), vec3(0.5, 0.5, 0.5), "crate", true, true)
    box("Glass 3", vec3(0, 1.05, -44.1), vec3(2.2, 2.1, 0.12), "glass", true)
    box("End Pad", vec3(0, -0.2, -45.4), vec3(3.0, 0.4, 2.8), "checker", true)
    box("End Cap", vec3(0, 1.6, -46.9), vec3(3.0, 3.6, 0.4), "checker", true)
    forge.log("rooms 1-3 built")
end

-- Generic puzzle systems for the rooms (tutorial keeps its bespoke code).
local plates2 = {
    { center = vec3(3.5, 0.35, -13.0), half = vec3(0.7, 0.3, 0.7), door = "Door 1", pressed = false },
    { center = vec3(-3.5, 0.35, -38.0), half = vec3(0.7, 0.3, 0.7), door = "Door 3", pressed = false },
}
local doors2 = {
    ["Door 1"] = { closed = vec3(0, 1.25, -19.4), open = vec3(0, 3.55, -19.4), speed = 2.2, want = false },
    ["Door 2"] = { closed = vec3(0, 1.25, -31.4), open = vec3(0, 3.55, -31.4), speed = 2.2, want = false },
    ["Door 3"] = { closed = vec3(0, 1.25, -43.4), open = vec3(0, 3.55, -43.4), speed = 2.2, want = false },
}
local lasers2 = {
    { origin = vec3(-4.4, 1.1, -26.0), dir = vec3(1, 0, 0), maxDist = 9.2,
      beam = "Beam 2", receiverX = 4.45, door = "Door 2", blocked = false },
}
local glasses2 = {
    { name = "Glass 3", center = vec3(0, 1.05, -44.1), half = vec3(1.3, 1.2, 0.4),
      breakSpeed = 4.5, broken = false },
}
-- ---------------------------------------------------------------------------

local spawnCount = 0
local won = false

function onStart()
    forge.log("scene.lua loaded (" .. _VERSION .. ")")
    buildRooms()
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

    -- ---- Rooms 1-3 systems: plates hold doors, the laser holds Door 2
    -- while BLOCKED, glass breaks on fast impact.
    for _, pl in ipairs(plates2) do
        local now = #forge.physics.overlap(pl.center, pl.half) > 0
        if now ~= pl.pressed then
            pl.pressed = now
            doors2[pl.door].want = now
            forge.audio.play(now and "assets/sounds/land.wav" or "assets/sounds/grab.wav")
            forge.log(pl.door .. (now and ": opening" or ": closing"))
        end
    end
    for _, lz in ipairs(lasers2) do
        local _, hd = forge.physics.raycast(lz.origin, lz.dir, lz.maxDist)
        local d2 = hd or lz.maxDist
        forge.scene.setPosition(lz.beam, vec3(lz.origin.x + d2 * 0.5, lz.origin.y, lz.origin.z))
        forge.scene.setScale(lz.beam, vec3(d2, 0.05, 0.05))
        local blockedNow = d2 < (lz.receiverX - lz.origin.x) - 0.3
        if blockedNow ~= lz.blocked then
            lz.blocked = blockedNow
            doors2[lz.door].want = blockedNow
            forge.audio.play(blockedNow and "assets/sounds/grab.wav" or "assets/sounds/jump.wav")
            forge.log(lz.door .. (blockedNow and ": beam blocked - opening" or ": beam restored - closing"))
        end
    end
    for name, d in pairs(doors2) do
        local target = d.want and d.open or d.closed
        forge.scene.setPosition(name, moveTowards(forge.scene.getPosition(name), target, d.speed * dt))
    end
    for _, gl in ipairs(glasses2) do
        if not gl.broken then
            for _, body in ipairs(forge.physics.overlap(gl.center, gl.half)) do
                local v2 = forge.physics.velocity(body)
                local sp = math.sqrt(v2.x * v2.x + v2.y * v2.y + v2.z * v2.z)
                if sp > gl.breakSpeed then
                    gl.broken = true
                    forge.scene.destroy(gl.name)
                    forge.fx.burst(gl.center, 40)
                    forge.audio.play("assets/sounds/shatter.wav")
                    forge.log(gl.name .. " SHATTERED")
                    break
                end
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
