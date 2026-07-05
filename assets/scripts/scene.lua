-- assets/scripts/scene.lua — VAULT: tutorial + rooms 1-8, all Lua data.
-- Hot-reloadable logic; GEOMETRY spawns once per run (restart to reshape).
-- Layout runs NORTH (-z): tutorial | R1 carry | R2 laser-park | R3 throw |
-- R4 timed plate sprint | R5 drone intro | R6 plate+drone-beam | R7 glass wall |
-- R8 finale (2 plates + beam) | end pad. Drone: Q parks/recalls (flies
-- ABOVE the open-top walls — steering, no navmesh, per the game plan).

-- ---- Tutorial wiring (bespoke; the rooms use the generic systems below) --
local plate = {
    center = vec3(-3.0, 0.35, 2.0),
    half   = vec3(0.7, 0.35, 0.7),
    pressed = false,
}
local door = {
    name   = "Exit Door",
    closed = vec3(0.0, 1.25, -7.2),
    open   = vec3(0.0, 3.55, -7.2),
    speed  = 2.2,
}
local laser = {
    origin   = vec3(-6.7, 1.1, -5.0),
    dir      = vec3(1.0, 0.0, 0.0),
    maxDist  = 13.5,
    beamName = "Laser Beam",
    receiver = "Laser Receiver",
    blocked  = false,
}
local glass = {
    name   = "Glass",
    center = vec3(0.0, 1.05, -7.9),
    half   = vec3(1.3, 1.2, 0.4),
    breakSpeed = 4.5,
    broken = false,
}
local winZ = -104.4 -- past room 8's exit

-- ---- Generic puzzle data: rooms 1-8 -------------------------------------
local plates2 = { -- parkId: a parked drone counts as weight
    { id = "p1",  center = vec3(3.5, 0.35, -13.0),  half = vec3(0.7, 0.3, 0.7) },
    { id = "p3",  center = vec3(-3.5, 0.35, -38.0), half = vec3(0.7, 0.3, 0.7) },
    { id = "p4a", center = vec3(-3.0, 0.35, -50.0), half = vec3(0.7, 0.3, 0.7) },
    -- R4 redesign (week 7): ONE crate, TWO plates. p4b HOLDS its signal for a
    -- few seconds after release — weigh p4a with the crate, press p4b with
    -- your own body, then sprint the door before the hold runs out.
    { id = "p4b", center = vec3(3.0, 0.35, -50.0),  half = vec3(0.7, 0.3, 0.7), holdTime = 3.5 },
    { id = "p5",  center = vec3(3.5, 0.35, -63.0),  half = vec3(0.7, 0.3, 0.7), parkId = "p5" },
    { id = "p6",  center = vec3(-3.5, 0.35, -75.0), half = vec3(0.7, 0.3, 0.7) },
    { id = "p7",  center = vec3(-3.5, 0.35, -88.0), half = vec3(0.7, 0.3, 0.7) },
    { id = "p8a", center = vec3(-3.0, 0.35, -99.0), half = vec3(0.7, 0.3, 0.7) },
    { id = "p8b", center = vec3(3.0, 0.35, -99.0),  half = vec3(0.7, 0.3, 0.7) },
}
local lasers2 = { -- parkId: a parked drone blocks the beam
    { id = "l2", origin = vec3(-4.4, 1.1, -26.0), receiverX = 4.45, beam = "Beam 2" },
    { id = "l6", origin = vec3(-4.4, 1.1, -74.0), receiverX = 4.45, beam = "Beam 6", parkId = "b6" },
    { id = "l8", origin = vec3(-4.4, 1.1, -97.0), receiverX = 4.45, beam = "Beam 8", parkId = "b8" },
}
local glasses2 = {
    { name = "Glass 3", center = vec3(0, 1.05, -44.1), half = vec3(1.3, 1.2, 0.4) },
    { name = "Glass 7", center = vec3(0, 1.05, -85.0), half = vec3(1.3, 1.2, 0.4) },
}
-- Every throwable crate as DATA (name + spawn), so buildRooms spawns them and
-- week-6 respawn/reset can return them home. Tutorial crates (A/B/C) are
-- spawned by the host but are name-addressable, so they respawn too.
local roomCrates = {
    { name = "Crate A",   pos = vec3(0.0, 0.4, 2.5) },
    { name = "Crate B",   pos = vec3(-1.2, 0.4, 1.2) },
    { name = "Crate C",   pos = vec3(1.3, 0.4, 0.8) },
    { name = "Crate R1",  pos = vec3(-3.5, 0.4, -17.0) },
    { name = "Crate R2",  pos = vec3(-3.5, 0.4, -23.5) },
    { name = "Crate R3",  pos = vec3(3.5, 0.4, -35.0) },
    { name = "Crate R4a", pos = vec3(-3.0, 0.4, -46.5) }, -- R4's ONLY crate (timed plate)
    { name = "Crate R6",  pos = vec3(3.5, 0.4, -71.0) },
    { name = "Crate R7",  pos = vec3(-3.5, 0.4, -82.5) },
    { name = "Crate R8a", pos = vec3(-3.0, 0.4, -94.5) },
    { name = "Crate R8b", pos = vec3(3.0, 0.4, -94.5) },
}
-- Checkpoints (week 6): the deepest reached room entrance. Player falls into a
-- void (or presses R) -> respawn here. z is the near-wall the player crosses;
-- pos is a safe drop just inside on solid floor. Start is always index 1.
local startPos = vec3(0, 1.5, 5.0)
local voidY = -8.0
local checkpoints = { { z = 100.0, pos = startPos } }
for _, nz in ipairs({ -9.4, -21.4, -33.4, -45.4, -57.4, -69.4, -81.4, -93.4 }) do
    checkpoints[#checkpoints + 1] = { z = nz, pos = vec3(0, 1.5, nz - 1.6) }
end
local curCp = 1
local state = { plates = {}, lasers = {} } -- id -> pressed/blocked
local function plateOn(id) return state.plates[id] == true end
local function beamCut(id) return state.lasers[id] == true end
local doors2 = {
    ["Door 1"] = { z = -19.4, cond = function() return plateOn("p1") end },
    ["Door 2"] = { z = -31.4, cond = function() return beamCut("l2") end },
    ["Door 3"] = { z = -43.4, cond = function() return plateOn("p3") end },
    ["Door 4"] = { z = -55.4, cond = function() return plateOn("p4a") and plateOn("p4b") end },
    ["Door 5"] = { z = -67.4, cond = function() return plateOn("p5") end },
    ["Door 6"] = { z = -79.4, cond = function() return plateOn("p6") and beamCut("l6") end },
    ["Door 7"] = { z = -91.4, cond = function() return plateOn("p7") end },
    ["Door 8"] = { z = -103.4,
                   cond = function() return plateOn("p8a") and plateOn("p8b") and beamCut("l8") end },
}
for _, d in pairs(doors2) do
    d.closed, d.open, d.speed, d.want = vec3(0, 1.25, d.z), vec3(0, 3.55, d.z), 2.2, false
end

-- Drone: follows the player from above; Q parks it at the current room's
-- spot (plate weight or beam blocker), Q again recalls it.
local drone = {
    name = "Drone", speed = 6.0, parkedAt = nil, -- parkId or nil
    parks = { -- id -> {pos, zMin, zMax (room extent)}
        p5 = { pos = vec3(3.5, 0.5, -63.0),  zMin = -67.4, zMax = -57.4 },
        b6 = { pos = vec3(0.0, 1.1, -74.0),  zMin = -79.4, zMax = -69.4 },
        b8 = { pos = vec3(0.0, 1.1, -97.0),  zMin = -103.4, zMax = -93.4 },
    },
}

-- ---- Room construction ----------------------------------------------------
local function box(name, pos, scale, tex, solid, dynamic)
    local half = solid and vec3(scale.x * 0.5, scale.y * 0.5, scale.z * 0.5) or vec3(0, 0, 0)
    forge.scene.spawn(name, pos, scale, half, tex, dynamic or false)
end

local function shell(id, z0, z1, w) -- floor + side walls (z0 > z1)
    local cz, L = (z0 + z1) * 0.5, z0 - z1
    box(id .. " Floor", vec3(0, -0.2, cz), vec3(w, 0.4, L), "floor", true)
    box(id .. " Wall W", vec3(-w * 0.5, 1.6, cz), vec3(0.4, 3.6, L), "concrete", true)
    box(id .. " Wall E", vec3(w * 0.5, 1.6, cz), vec3(0.4, 3.6, L), "concrete", true)
end

local function framedWall(id, z) -- full wall with a 2.2m doorway gap
    box(id .. " W", vec3(-3.05, 1.6, z), vec3(3.9, 3.6, 0.4), "concrete", true)
    box(id .. " E", vec3(3.05, 1.6, z), vec3(3.9, 3.6, 0.4), "concrete", true)
    box(id .. " Top", vec3(0, 3.0, z), vec3(2.2, 0.8, 0.4), "concrete", true)
end

local function corridor(id, z0) -- 2m long, walls FLUSH with the 2.2m frames
    box(id .. " Floor", vec3(0, -0.2, z0 - 1.0), vec3(3.0, 0.4, 2.0), "floor", true)
    box(id .. " Wall W", vec3(-1.3, 1.6, z0 - 1.0), vec3(0.4, 3.6, 2.0), "concrete", true)
    box(id .. " Wall E", vec3(1.3, 1.6, z0 - 1.0), vec3(0.4, 3.6, 2.0), "concrete", true)
end

local function laserRig(n, z)
    box("Emitter " .. n, vec3(-4.6, 1.1, z), vec3(0.3, 0.3, 0.3), "laser", true)
    box("Receiver " .. n, vec3(4.6, 1.1, z), vec3(0.3, 0.3, 0.3), "laser", true)
    box("Beam " .. n, vec3(0, 1.1, z), vec3(8.8, 0.05, 0.05), "laser", false)
end

local function buildRooms()
    if roomsBuilt then return end
    roomsBuilt = true
    corridor("CorA", -7.4) -- gap fix: was 3.2m wide, now flush at 2.2m frames
    local roomZ = { -9.4, -21.4, -33.4, -45.4, -57.4, -69.4, -81.4, -93.4 }
    for i = 1, 8 do
        local z0 = roomZ[i]
        local id = "R" .. i
        shell(id, z0, z0 - 10, 10)
        framedWall(id .. " NearWall", z0)      -- gap fix: rooms had open entries
        framedWall(id .. " FarWall", z0 - 10)
        -- 0.32 thick vs the 0.4 frame: recessed 4 cm so an OPEN door's faces
        -- never sit coplanar with the frame's top piece (z-fighting fix).
        box("Door " .. i, vec3(0, 1.25, z0 - 10), vec3(2.2, 2.5, 0.32), "metal", true)
        if i < 8 then corridor("Cor" .. i, z0 - 10) end
    end
    -- plates (visual slabs; logic reads the plates2 regions)
    for _, pl in ipairs(plates2) do
        box("Plate " .. pl.id, vec3(pl.center.x, 0.1, pl.center.z), vec3(1.4, 0.2, 1.4),
            "metal", true)
    end
    -- room contents (fixtures here; crates spawn from roomCrates below)
    laserRig(2, -26.0)
    box("Pedestal 2", vec3(0, 0.5, -26.0), vec3(1.0, 1.0, 1.0), "metal", true)
    box("Glass 3", vec3(0, 1.05, -44.1), vec3(2.2, 2.1, 0.12), "glass", true)
    laserRig(6, -74.0)
    -- R7: glass wall mid-room (framed, pane fills the gap)
    framedWall("R7 MidWall", -85.0)
    box("Glass 7", vec3(0, 1.05, -85.0), vec3(2.2, 2.1, 0.12), "glass", true)
    laserRig(8, -97.0)
    -- throwable crates from the shared data table (skip the host-spawned
    -- tutorial crates A/B/C — the room ids start at R).
    for _, c in ipairs(roomCrates) do
        if c.name:sub(1, 7) == "Crate R" then
            box(c.name, c.pos, vec3(0.5, 0.5, 0.5), "crate", true, true)
        end
    end
    -- end section (sealed: pad, side walls, cap)
    box("End Pad", vec3(0, -0.2, -104.9), vec3(3.0, 0.4, 3.0), "floor", true)
    box("End Wall W", vec3(-1.3, 1.6, -104.9), vec3(0.4, 3.6, 3.0), "concrete", true)
    box("End Wall E", vec3(1.3, 1.6, -104.9), vec3(0.4, 3.6, 3.0), "concrete", true)
    box("End Cap", vec3(0, 1.6, -106.5), vec3(3.0, 3.6, 0.4), "concrete", true)
    -- the companion drone (visual entity; pure Lua steering)
    box("Drone", vec3(1.0, 2.4, 3.0), vec3(0.4, 0.25, 0.4), "glass", false)
    forge.log("rooms 1-8 built (near walls + flush corridors: map sealed)")
end

-- ---------------------------------------------------------------------------
local spawnCount = 0
local won = false

function onStart()
    forge.log("scene.lua loaded (" .. _VERSION .. ")")
    buildRooms()
    -- Week 5 audio beds: persistent looping voices (own volume). Guarded so a
    -- hot-reload of this script doesn't stack a second copy on top.
    if not audioStarted then
        audioStarted = true
        forge.audio.loop("assets/sounds/ambient.wav", 0.30) -- HVAC room tone
        forge.audio.loop("assets/sounds/music.wav", 0.22)   -- minor pad underscore
    end
    forge.log("Q: park/recall drone (rooms 5, 6, 8)")
end

-- Per-room lighting pass (week 5): key-light colour by player z (rgb * HDR
-- intensity; default warm facility key is (3.0, 2.9, 2.7)). Read most-negative
-- first since the layout runs north (-z).
local function roomLight(z)
    if z <= -104.4 then return vec3(2.6, 3.4, 2.6)      -- end pad: success glow
    elseif z <= -93.4 then return vec3(3.7, 3.0, 2.1)   -- R8 finale: hot amber
    elseif z <= -81.4 then return vec3(2.0, 2.3, 2.9)   -- R7 glass: tense dim blue
    elseif z <= -69.4 then return vec3(2.4, 3.0, 3.0)   -- R6 plate+beam: teal
    elseif z <= -57.4 then return vec3(2.5, 3.05, 3.05) -- R5 drone intro: cool teal-white
    elseif z <= -45.4 then return vec3(2.6, 2.7, 2.9)   -- R4 double plate: cool dim
    elseif z <= -33.4 then return vec3(2.7, 2.75, 2.85) -- R3 throw: neutral-cool
    elseif z <= -21.4 then return vec3(3.1, 3.0, 2.8)   -- R2 laser: clean warm-white
    elseif z <= -9.4 then return vec3(3.0, 3.05, 3.2)   -- R1 carry: clean cool
    else return vec3(3.0, 2.9, 2.7)                     -- tutorial: warm facility
    end
end
local curLight = vec3(3.0, 2.9, 2.7)

-- Objective hints (week 7 QoL): one line per room, host draws it top-centre.
-- Same most-negative-first z ladder as roomLight.
local function roomHint(z)
    if z <= -104.4 then return "VAULT cleared. Head for the light."
    elseif z <= -93.4 then return "R8: two plates AND the beam - everything you know"
    elseif z <= -81.4 then return "R7: the plate is behind glass; crates break glass"
    elseif z <= -69.4 then return "R6: plate + beam - the drone (Q) can hold one"
    elseif z <= -57.4 then return "R5: no crates here; Q parks the drone on the plate"
    elseif z <= -45.4 then return "R4: one crate, two plates - hold one yourself and RUN"
    elseif z <= -33.4 then return "R3: the plate is across the room; throw the crate"
    elseif z <= -21.4 then return "R2: something must sit in the beam"
    elseif z <= -9.4 then return "R1: carry a crate onto the plate"
    else return "weigh the plate to open the door - LMB grab / LMB throw"
    end
end
local curHint = nil

local function moveTowards(from, to, maxStep)
    local d = vec3(to.x - from.x, to.y - from.y, to.z - from.z)
    local len = math.sqrt(d.x * d.x + d.y * d.y + d.z * d.z)
    if len <= maxStep or len < 1e-6 then
        return to
    end
    local k = maxStep / len
    return vec3(from.x + d.x * k, from.y + d.y * k, from.z + d.z * k)
end

-- Pressure-plate sink (week 7): the DETECTION region is fixed in space, so
-- the visual slab is free to ease down ~8 cm when its plate reads pressed.
-- Generic plates rest at y 0.1; the tutorial slab ("Plate T") rests at 0.05.
local function updatePlateVisuals(dt)
    local step = 0.25 * dt
    for _, pl in ipairs(plates2) do
        local name = "Plate " .. pl.id
        local at = forge.scene.getPosition(name)
        local ty = plateOn(pl.id) and 0.02 or 0.1
        forge.scene.setPosition(name, moveTowards(at, vec3(at.x, ty, at.z), step))
    end
    local tAt = forge.scene.getPosition("Plate T")
    local tTy = plate.pressed and 0.01 or 0.05
    forge.scene.setPosition("Plate T", moveTowards(tAt, vec3(tAt.x, tTy, tAt.z), step))
end

local droneTime = 0
local function updateDrone(dt)
    droneTime = droneTime + dt
    local p = forge.player.position()
    if forge.input.pressed("q") then
        if drone.parkedAt then
            drone.parkedAt = nil
            forge.audio.play("assets/sounds/jump.wav")
            forge.log("drone: recalled")
        else
            for id, park in pairs(drone.parks) do
                if p.z > park.zMin and p.z < park.zMax then
                    drone.parkedAt = id
                    forge.audio.play("assets/sounds/grab.wav")
                    forge.log("drone: parking at " .. id)
                    break
                end
            end
            if not drone.parkedAt then forge.log("drone: nothing to hold here") end
        end
    end
    local target
    if drone.parkedAt then
        target = drone.parks[drone.parkedAt].pos
    else
        -- cruise ABOVE the walls toward a spot beside/ahead of the player
        target = vec3(p.x + 1.0, 4.2, p.z - 1.3)
    end
    local at = forge.scene.getPosition(drone.name)
    local nextPos = moveTowards(at, target, drone.speed * dt)
    nextPos.y = nextPos.y + math.sin(droneTime * 3.0) * 0.02 -- idle bob
    forge.scene.setPosition(drone.name, nextPos)
end

local function droneAt(parkId)
    if drone.parkedAt ~= parkId then return false end
    local at = forge.scene.getPosition(drone.name)
    local goal = drone.parks[parkId].pos
    local dx, dy, dz = at.x - goal.x, at.y - goal.y, at.z - goal.z
    return dx * dx + dy * dy + dz * dz < 0.09 -- actually arrived
end

-- ---- Checkpoints + void recovery (week 6) --------------------------------
local function respawnPlayer()
    forge.player.teleport(checkpoints[curCp].pos)
    forge.audio.play("assets/sounds/land.wav")
    forge.log("respawn at checkpoint " .. curCp)
end

local function updateRespawn(p)
    -- Advance the checkpoint as the player crosses each room's near wall.
    while curCp < #checkpoints and p.z <= checkpoints[curCp + 1].z do
        curCp = curCp + 1
        forge.log("checkpoint reached (" .. curCp .. ")")
    end
    -- Player fell into a void, or asked for a manual respawn (R).
    if p.y < voidY or forge.input.pressed("r") then
        respawnPlayer()
    end
    -- Crates that fell out come home (closes the corridor-void loss bug).
    for _, c in ipairs(roomCrates) do
        if forge.scene.getPosition(c.name).y < voidY then
            forge.scene.setPosition(c.name, c.pos)
        end
    end
end

-- Full restart (week 6): the host calls this from the menu's Restart. A soft
-- reset — no scene rebuild — that returns every mutable thing to its start:
-- player, crates, puzzle latches, drone, and re-spawns any shattered glass.
function resetGame()
    forge.player.teleport(startPos)
    curCp = 1
    won = false
    for _, c in ipairs(roomCrates) do
        forge.scene.setPosition(c.name, c.pos)
    end
    state.plates = {}
    state.lasers = {}
    for _, d in pairs(doors2) do d.want = false end
    for _, pl in ipairs(plates2) do pl.timer = nil end -- timed plates disarm
    drone.parkedAt = nil
    plate.pressed = false
    laser.blocked = false
    curHint = nil -- re-push the hint on the first frame back
    -- Re-spawn shattered panes (all glass shares one visual scale).
    for _, gl in ipairs(glasses2) do
        if gl.broken then
            forge.scene.spawn(gl.name, gl.center, vec3(2.2, 2.1, 0.12),
                              vec3(1.1, 1.05, 0.06), "glass", false)
            gl.broken = false
        end
    end
    if glass.broken then
        forge.scene.spawn(glass.name, glass.center, vec3(2.2, 2.1, 0.12),
                          vec3(1.1, 1.05, 0.06), "glass", false)
        glass.broken = false
    end
    forge.log("*** game reset ***")
end

function onUpdate(dt)
    local pp = forge.player.position()

    -- ---- Checkpoints + void recovery (respawn player and lost crates) ----
    updateRespawn(pp)

    -- ---- Per-room lighting: ease the key light toward this room's mood so
    -- crossing a doorway fades colour over ~0.4 s instead of snapping. ----
    local tgt = roomLight(pp.z)
    local k = math.min(dt * 2.5, 1.0)
    curLight = vec3(curLight.x + (tgt.x - curLight.x) * k,
                    curLight.y + (tgt.y - curLight.y) * k,
                    curLight.z + (tgt.z - curLight.z) * k)
    forge.render.set_light(curLight)

    -- ---- Objective hint: push only on change (crossing a room boundary) ----
    local h = roomHint(pp.z)
    if h ~= curHint then
        curHint = h
        forge.hud.set_hint(h)
    end

    -- ---- Tutorial (bespoke) ----
    local pressedNow = #forge.physics.overlap(plate.center, plate.half) > 0
    if pressedNow ~= plate.pressed then
        plate.pressed = pressedNow
        forge.audio.play(pressedNow and "assets/sounds/land.wav" or "assets/sounds/grab.wav")
        forge.log(pressedNow and "plate: PRESSED - door opening"
                              or "plate: released - door closing")
    end
    local target = plate.pressed and door.open or door.closed
    forge.scene.setPosition(door.name,
                            moveTowards(forge.scene.getPosition(door.name), target,
                                        door.speed * dt))

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

    -- ---- Drone + plate visuals ----
    updateDrone(dt)
    updatePlateVisuals(dt)

    -- ---- Rooms 1-8 generic systems ----
    for _, pl in ipairs(plates2) do
        local raw = #forge.physics.overlap(pl.center, pl.half) > 0
                    or (pl.parkId ~= nil and droneAt(pl.parkId))
        local now = raw
        -- Timed plates (week 7): the signal HOLDS for holdTime seconds after
        -- the weight leaves — press with your body, then sprint the door.
        if pl.holdTime then
            if raw then
                pl.timer = pl.holdTime
            elseif pl.timer and pl.timer > 0 then
                pl.timer = pl.timer - dt
                now = pl.timer > 0
            else
                now = false
            end
        end
        if now ~= (state.plates[pl.id] == true) then
            state.plates[pl.id] = now
            forge.audio.play(now and "assets/sounds/land.wav" or "assets/sounds/grab.wav")
            forge.log("plate " .. pl.id .. (now and ": on" or ": off"))
        end
    end
    for _, lz in ipairs(lasers2) do
        local _, hd = forge.physics.raycast(lz.origin, vec3(1, 0, 0), 9.2)
        local d2 = hd or 9.2
        local cut = d2 < (lz.receiverX - lz.origin.x) - 0.3
                    or (lz.parkId ~= nil and droneAt(lz.parkId))
        forge.scene.setPosition(lz.beam, vec3(lz.origin.x + d2 * 0.5, lz.origin.y, lz.origin.z))
        forge.scene.setScale(lz.beam, vec3(d2, 0.05, 0.05))
        if cut ~= (state.lasers[lz.id] == true) then
            state.lasers[lz.id] = cut
            forge.audio.play(cut and "assets/sounds/grab.wav" or "assets/sounds/jump.wav")
            forge.log("laser " .. lz.id .. (cut and ": blocked" or ": restored"))
        end
    end
    for name, d in pairs(doors2) do
        local wantNow = d.cond()
        if wantNow ~= d.want then
            d.want = wantNow
            forge.audio.play("assets/sounds/land.wav", 0.5) -- door motor thunk
            forge.log(name .. (wantNow and ": opening" or ": closing"))
        end
        forge.scene.setPosition(name,
                                moveTowards(forge.scene.getPosition(name),
                                            d.want and d.open or d.closed, d.speed * dt))
    end
    for _, gl in ipairs(glasses2) do
        if not gl.broken then
            for _, body in ipairs(forge.physics.overlap(gl.center, gl.half)) do
                local v2 = forge.physics.velocity(body)
                local sp = math.sqrt(v2.x * v2.x + v2.y * v2.y + v2.z * v2.z)
                if sp > 4.5 then
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

    -- ---- Win ----
    if not won then
        local p = forge.player.position()
        if p.z < winZ then
            won = true
            forge.audio.play("assets/sounds/kick.wav")
            forge.fx.burst(vec3(p.x, p.y + 0.5, p.z), 30)
            forge.log("*** VAULT RUN COMPLETE: all 8 rooms ***")
            forge.game.win() -- raise the win screen (host owns the menu)
        end
    end

    -- E: rain extra throwable crates (handy anywhere).
    if forge.input.pressed("e") then
        local p = forge.player.position()
        for i = 1, 3 do
            local at2 = vec3(p.x + (i - 2) * 0.9, p.y + 2.5, p.z - 2.0)
            forge.physics.spawnBox(vec3(0.25, 0.25, 0.25), at2, true, 0.4)
            forge.fx.burst(at2, 8)
        end
        forge.audio.play("assets/sounds/kick.wav")
        spawnCount = spawnCount + 3
        forge.log("crate drop! total spawned: " .. spawnCount)
    end
end
