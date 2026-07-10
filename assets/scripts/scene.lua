-- assets/scripts/scene.lua — VAULT: tutorial + rooms 1-14, all Lua data.
-- Hot-reloadable logic; GEOMETRY spawns once per run (restart to reshape).
-- Layout runs NORTH (-z): tutorial | R1 carry | R2 laser-park | R3 throw |
-- R4 timed plate sprint | R5 drone intro | R6 plate+drone-beam | R7 glass wall |
-- R8 (2 plates + beam) | R9 relay ring (week 8: gate closes behind you) |
-- R10 atrium (week 8: three wings feed three plates) |
-- R11 range (week 12: charged launch + resonance bells) |
-- R12 errand (week 12: F sends SPARK where you aim) |
-- R13 alley-oop (week 12: SPARK catches your launches; heavy flies alone) |
-- R14 duet (week 12: parallel lanes, every verb) | end pad.
-- Drone: Q parks/recalls at fixed spots (unchanged); F is the week-12 verb —
-- point anywhere, SPARK stations there, presses plates, CATCHES light crates.

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
local winZ = -210.6 -- past room 14's exit (week 12 moved the finish line)

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
    -- R10 atrium (week 8): wing plates open the cages, the hub row opens the
    -- exit. p10w wants sustained weight (drone or crate); p10e is a body-press
    -- sprint; p10a/b/c want the three wing crates.
    { id = "p10w", center = vec3(-5.5, 0.35, -123.5), half = vec3(0.7, 0.3, 0.7), parkId = "aw" },
    { id = "p10e", center = vec3(6.0, 0.35, -124.0),  half = vec3(0.7, 0.3, 0.7), holdTime = 3.0 },
    { id = "p10a", center = vec3(-2.0, 0.35, -132.0), half = vec3(0.7, 0.3, 0.7) },
    { id = "p10b", center = vec3(0.0, 0.35, -132.0),  half = vec3(0.7, 0.3, 0.7) },
    { id = "p10c", center = vec3(2.0, 0.35, -132.0),  half = vec3(0.7, 0.3, 0.7) },
    -- Week 12 plates. p12: SPARK's drone pad across the chasm — a stationed
    -- drone (F) is weight now; latch = the bridge stays out once earned.
    -- p13h: the heavy-cube plate on R13's ledge — only a full-charge launch
    -- delivers the heavy cube there. p14: R14's stage-2 plate, visible only
    -- through the divider glass — SPARK's errand, or a very good throw.
    { id = "p12",  center = vec3(0.0, 0.35, -170.9),  half = vec3(0.8, 0.3, 0.8),
      latch = true, offTex = "metal_blue", visScale = vec3(1.6, 0.2, 1.6) },
    { id = "p13h", center = vec3(2.5, 1.55, -187.6),  half = vec3(0.9, 0.35, 0.9),
      visY = 1.3, visScale = vec3(1.8, 0.2, 1.8) },
    { id = "p14",  center = vec3(3.5, 0.55, -203.4),  half = vec3(0.75, 0.5, 0.75),
      visScale = vec3(1.5, 0.2, 1.5) },
}
-- Resonance bells (week 12): impact targets. A crate crossing the bell's
-- AABB above ringSpeed RINGS it (latched signal, green); slower hits thunk
-- ("harder"). relatch: the ring decays after N seconds (R14's timed gate).
-- Detection is a swept SEGMENT per tracked crate — a 25 m/s crate moves
-- ~0.4 m/frame and must never tunnel a thin dish.
local bells2 = {
    { id = "bA",  name = "Bell A",  center = vec3(-3.0, 1.2, -149.0),
      half = vec3(0.55, 0.55, 0.4), ringSpeed = 7.5 },
    { id = "bB",  name = "Bell B",  center = vec3(3.4, 3.0, -154.0),
      half = vec3(0.55, 0.55, 0.4), ringSpeed = 14.0 },
    { id = "bC",  name = "Bell C",  center = vec3(-3.0, 1.2, -156.3),
      half = vec3(0.55, 0.55, 0.4), ringSpeed = 14.0 },
    { id = "b14", name = "Bell 14", center = vec3(2.0, 3.05, -198.4),
      half = vec3(0.6, 0.6, 0.45), ringSpeed = 14.0, relatch = 6.0,
      baseRot = vec3(0, -90, 0) }, -- dish faces the west-lane aperture
}
-- Snap-sockets (week 12): forgiveness basins. A light crate that lands close
-- and slow eases into the seat — long-range placement stays realization, not
-- aim. Heavy crates never socket (mass rule).
local sockets2 = {
    { id = "s13", name = "Socket 13", seat = vec3(-3.0, 1.45, -188.5) },
    { id = "s14", name = "Socket 14", seat = vec3(3.5, 1.05, -207.4) },
}
-- F-command no-fly volumes (refusal grammar: SPARK head-shakes at these).
local noFly = {
    { center = vec3(0.0, 1.0, -166.9), half = vec3(5.0, 2.8, 2.6) }, -- R12 chasm
}
-- See-through-but-solid panes the AIM ray passes through (R14's divider
-- glass): you can SEE the target, you can't BE there — but SPARK can.
local transparents = {
    { center = vec3(0.0, 1.85, -205.4), half = vec3(0.3, 1.5, 4.0) },
}
local lasers2 = { -- parkId: a parked drone blocks the beam; maxDist defaults 9.2
    { id = "l2", origin = vec3(-4.4, 1.1, -26.0), receiverX = 4.45, beam = "Beam 2" },
    { id = "l6", origin = vec3(-4.4, 1.1, -74.0), receiverX = 4.45, beam = "Beam 6", parkId = "b6" },
    { id = "l8", origin = vec3(-4.4, 1.1, -97.0), receiverX = 4.45, beam = "Beam 8", parkId = "b8" },
    -- R9 relay ring (week 8): KNEE-HIGH beams — a grounded crate cuts them.
    -- Cutting r1/r2 opens the ring gates; r3 opens the exit door.
    { id = "r1", origin = vec3(-6.4, 0.35, -108.0), receiverX = 6.6,
      beam = "Beam r1", maxDist = 13.5 },
    { id = "r2", origin = vec3(-6.4, 0.35, -112.4), receiverX = -2.2,
      beam = "Beam r2", maxDist = 4.6 },
    { id = "r3", origin = vec3(-6.4, 0.35, -117.0), receiverX = 6.6,
      beam = "Beam r3", maxDist = 13.5 },
}
local glasses2 = { -- scale/halfE: what resetGame re-spawns (Glass 10 is x-thin)
    { name = "Glass 3", center = vec3(0, 1.05, -44.1), half = vec3(1.3, 1.2, 0.4),
      scale = vec3(2.2, 2.1, 0.12), halfE = vec3(1.1, 1.05, 0.06) },
    { name = "Glass 7", center = vec3(0, 1.05, -85.0), half = vec3(1.3, 1.2, 0.4),
      scale = vec3(2.2, 2.1, 0.12), halfE = vec3(1.1, 1.05, 0.06) },
    { name = "Glass 10", center = vec3(-4.0, 1.05, -135.4), half = vec3(0.4, 1.2, 1.3),
      scale = vec3(0.12, 2.1, 2.2), halfE = vec3(0.06, 1.05, 1.1) },
    -- R11: Bell C's pane — one charged shot both shatters it AND rings the
    -- bell behind (the shot carries through).
    { name = "Glass 11", center = vec3(-3.0, 1.05, -154.8), half = vec3(1.3, 1.2, 0.4),
      scale = vec3(2.2, 2.1, 0.12), halfE = vec3(1.1, 1.05, 0.06) },
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
    { name = "Crate R9",   pos = vec3(4.0, 0.4, -107.0) },  -- ferried around the ring
    { name = "Crate R10a", pos = vec3(-6.0, 0.4, -128.5) }, -- Wing W (drone cage)
    { name = "Crate R10b", pos = vec3(6.0, 0.4, -130.5) },  -- Wing E (timed cage)
    { name = "Crate R10c", pos = vec3(-6.0, 0.4, -135.4) }, -- Wing N (behind glass)
    { name = "Crate R11a", pos = vec3(0.0, 0.4, -141.5) },  -- range ammunition
    { name = "Crate R11b", pos = vec3(-1.5, 0.4, -142.5) },
    { name = "Crate R13",  pos = vec3(-4.0, 0.4, -177.5) }, -- the alley-oop light cube
    { name = "Heavy R13",  pos = vec3(3.5, 0.6, -176.8) },  -- spawned bespoke (2.4x mass read)
    { name = "Crate R14a", pos = vec3(-3.5, 0.4, -195.8) }, -- duet lane ammunition
    { name = "Crate R14b", pos = vec3(-4.5, 0.4, -196.6) },
}
-- Checkpoints (week 6): the deepest reached room entrance. Player falls into a
-- void (or presses R) -> respawn here. z is the near-wall the player crosses;
-- pos is a safe drop just inside on solid floor. Start is always index 1.
local startPos = vec3(0, 1.5, 5.0)
local voidY = -8.0
local checkpoints = { { z = 100.0, pos = startPos } }
for _, nz in ipairs({ -9.4, -21.4, -33.4, -45.4, -57.4, -69.4, -81.4, -93.4,
                      -105.4, -121.4, -139.4, -159.4, -175.4 }) do
    checkpoints[#checkpoints + 1] = { z = nz, pos = vec3(0, 1.5, nz - 1.6) }
end
-- R14's entry is at ledge height (the Cor13 step-down): spawn a touch high
-- and let the drop land on the vestibule steps.
checkpoints[#checkpoints + 1] = { z = -193.4, pos = vec3(0, 2.6, -194.4) }
local curCp = 1
-- Run progress (week 10): 'furthest' is what the host persists. curCp rewinds
-- on restart/respawn; furthest never does — Continue means "my best run".
local furthest = 1
local state = { plates = {}, lasers = {}, bells = {}, sockets = {} } -- id -> signal
local function plateOn(id) return state.plates[id] == true end
local function beamCut(id) return state.lasers[id] == true end
local function bellRung(id) return state.bells[id] == true end
local function socketFilled(id) return state.sockets[id] == true end
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
    -- R9 relay ring (week 8): each gate is held open by the beam BEHIND it —
    -- walk through, then glove-pull the crate through the open doorway.
    ["Gate 9a"] = { x = -4.4, z = -110.4, cond = function() return beamCut("r1") end },
    ["Gate 9b"] = { x = -4.4, z = -114.4, cond = function() return beamCut("r2") end },
    ["Door 9"] = { z = -119.4, cond = function() return beamCut("r3") end },
    -- R10 atrium (week 8): wing cages + the three-crate exit.
    ["Cage W"] = { x = -4.0, z = -128.5, cond = function() return plateOn("p10w") end },
    ["Cage E"] = { x = 4.0, z = -130.5, cond = function() return plateOn("p10e") end },
    ["Door 10"] = { z = -137.4, cond = function()
        return plateOn("p10a") and plateOn("p10b") and plateOn("p10c")
    end },
    -- Week 12 doors. Door 11: all three bells. Bridge 12 + Door 12: the
    -- drone pad (latched, so recalling SPARK doesn't strand you mid-bridge).
    -- Door 13: the alley-oop socket AND the heavy plate. R14: bell gate
    -- (relatch: closes 6 s after the ring — a stroll, never a reset), the
    -- glass-plate gate, and the socket exit.
    ["Door 11"] = { z = -157.4, cond = function()
        return bellRung("bA") and bellRung("bB") and bellRung("bC")
    end },
    ["Bridge 12"] = { closedPos = vec3(0, -0.65, -171.7), openPos = vec3(0, -0.2, -166.9),
                      speed = 3.0, offTex = "metal_blue",
                      cond = function() return plateOn("p12") end },
    ["Door 12"] = { z = -173.4, cond = function() return plateOn("p12") end },
    ["Door 13"] = { z = -191.4, y = 2.45,
                    cond = function() return socketFilled("s13") and plateOn("p13h") end },
    ["Gate 14a"] = { x = -3.15, z = -201.4, cond = function() return bellRung("b14") end },
    ["Gate 14b"] = { x = -3.15, z = -204.9, cond = function() return plateOn("p14") end },
    ["Door 14"] = { x = -3.0, z = -209.4, cond = function() return socketFilled("s14") end },
}
for _, d in pairs(doors2) do -- x: sideways-facing gates sit off the room axis
    if d.closedPos then     -- explicit path (the bridge slides, not lifts)
        d.closed, d.open = d.closedPos, d.openPos
    else                    -- y: doors on raised floors (R13's ledge)
        local dx, dy = d.x or 0, d.y or 1.25
        d.closed, d.open = vec3(dx, dy, d.z), vec3(dx, dy + 2.3, d.z)
    end
    d.speed = d.speed or 2.2
    d.want = false
end

-- Drone: follows the player from above; Q parks it at the current room's
-- spot (plate weight or beam blocker), Q again recalls it. Week 12 adds the
-- F verb: station SPARK at any aimed point (go-there), where it presses
-- plates and CATCHES light crates out of the air.
local drone = {
    name = "Drone", speed = 6.0, parkedAt = nil, -- parkId or nil
    parks = { -- id -> {pos, zMin, zMax (room extent)}
        p5 = { pos = vec3(3.5, 0.5, -63.0),  zMin = -67.4, zMax = -57.4 },
        b6 = { pos = vec3(0.0, 1.1, -74.0),  zMin = -79.4, zMax = -69.4 },
        b8 = { pos = vec3(0.0, 1.1, -97.0),  zMin = -103.4, zMax = -93.4 },
        aw = { pos = vec3(-5.5, 1.0, -123.5), zMin = -137.4, zMax = -121.4 }, -- R10 Wing W
    },
    station = nil,    -- vec3: the F-command hover point
    carrying = nil,   -- crate name while caught
    sagT = 0,         -- catch reaction: sag + recovery bobs
    failT = 0,        -- heavy-catch fail beat: sink, drop, head-shake
    celebrateT = 0,   -- R14 reunion orbit
    trust = false,    -- flips on the first successful catch (the arc)
    -- Room-flagged behaviors (the game's most trusted signals must never
    -- fire where they would mislead): catcher stance + bell curiosity.
    catcherZones = { { zMin = -191.4, zMax = -175.4 }, { zMin = -209.4, zMax = -193.4 } },
    bellZone = { zMin = -157.4, zMax = -139.4 },
}

-- ---- Room construction ----------------------------------------------------
-- mesh (optional, week 11): a model-registry name; nil means the unit cube.
-- Models are authored in unit bounds, so scale keeps meaning what it meant.
local function box(name, pos, scale, tex, solid, dynamic, mesh)
    local half = solid and vec3(scale.x * 0.5, scale.y * 0.5, scale.z * 0.5) or vec3(0, 0, 0)
    forge.scene.spawn(name, pos, scale, half, tex, dynamic or false, mesh)
end

local function shell(id, z0, z1, w) -- floor + side walls (z0 > z1)
    local cz, L = (z0 + z1) * 0.5, z0 - z1
    box(id .. " Floor", vec3(0, -0.2, cz), vec3(w, 0.4, L), "floor", true)
    box(id .. " Wall W", vec3(-w * 0.5, 1.6, cz), vec3(0.4, 3.6, L), "concrete", true)
    box(id .. " Wall E", vec3(w * 0.5, 1.6, cz), vec3(0.4, 3.6, L), "concrete", true)
end

local function framedWall(id, z, w) -- full wall with a 2.2m doorway gap
    local seg = ((w or 10) - 2.2) * 0.5 -- w=10 -> the original 3.9m segments
    local cx = 1.1 + seg * 0.5
    box(id .. " W", vec3(-cx, 1.6, z), vec3(seg, 3.6, 0.4), "concrete", true)
    box(id .. " E", vec3(cx, 1.6, z), vec3(seg, 3.6, 0.4), "concrete", true)
    box(id .. " Top", vec3(0, 3.0, z), vec3(2.2, 0.8, 0.4), "concrete", true)
end

local function corridor(id, z0) -- 2m long, walls FLUSH with the 2.2m frames
    box(id .. " Floor", vec3(0, -0.2, z0 - 1.0), vec3(3.0, 0.4, 2.0), "floor", true)
    box(id .. " Wall W", vec3(-1.3, 1.6, z0 - 1.0), vec3(0.4, 3.6, 2.0), "concrete", true)
    box(id .. " Wall E", vec3(1.3, 1.6, z0 - 1.0), vec3(0.4, 3.6, 2.0), "concrete", true)
end

local function laserRig(n, z)
    box("Emitter " .. n, vec3(-4.6, 1.1, z), vec3(0.3, 0.3, 0.3), "laser", true, false, "emitter")
    box("Receiver " .. n, vec3(4.6, 1.1, z), vec3(0.3, 0.3, 0.3), "laser", true, false, "receiver")
    box("Beam " .. n, vec3(0, 1.1, z), vec3(8.8, 0.05, 0.05), "laser", false)
end

-- R9 Relay Ring (week 8, from docs/PUZZLE_IDEAS.md): a C-shaped circuit
-- around a solid centre block. Knee-high beams; a grounded crate cuts one.
-- Each gate is held open by the beam BEHIND it — place the crate, walk
-- through, glove-pull the crate through the open doorway, repeat. The gate
-- closing at your heels IS the loop.
local function buildRelayRing()
    shell("R9", -105.4, -119.4, 14)
    framedWall("R9 NearWall", -105.4, 14)
    framedWall("R9 FarWall", -119.4, 14)
    box("Door 9", vec3(0, 1.25, -119.4), vec3(2.2, 2.5, 0.32), "metal_red", true, false, "door")
    -- centre block seals the east side: the path is S lane -> W lane -> N lane
    box("R9 Block", vec3(2.4, 1.6, -112.4), vec3(8.8, 3.6, 4.0), "concrete", true)
    for gi, gz in ipairs({ -110.4, -114.4 }) do -- gated dividers across the W lane
        local id = "R9 Div" .. gi
        box(id .. " W", vec3(-6.0, 1.6, gz), vec3(1.6, 3.6, 0.4), "concrete", true)
        box(id .. " E", vec3(-2.8, 1.6, gz), vec3(1.6, 3.6, 0.4), "concrete", true)
        box(id .. " Top", vec3(-4.4, 3.0, gz), vec3(1.6, 0.8, 0.4), "concrete", true)
        box("Gate 9" .. (gi == 1 and "a" or "b"), vec3(-4.4, 1.25, gz), vec3(1.6, 2.5, 0.32),
            "metal_red", true, false, "door")
    end
    for _, r in ipairs({ { "r1", -108.0, 6.6 }, { "r2", -112.4, -2.2 }, { "r3", -117.0, 6.6 } }) do
        box("Emitter " .. r[1], vec3(-6.6, 0.35, r[2]), vec3(0.3, 0.3, 0.3), "laser", true,
            false, "emitter")
        box("Receiver " .. r[1], vec3(r[3], 0.35, r[2]), vec3(0.3, 0.3, 0.3), "laser", true,
            false, "receiver")
        box("Beam " .. r[1], vec3(0, 0.35, r[2]), vec3(1.0, 0.05, 0.05), "laser", false)
    end
    corridor("Cor9", -119.4)
end

-- R10 Atrium (week 8): a 16m hub with three wings, each holding one crate
-- behind a different lock (drone-park plate / timed body-press / glass you
-- spend a retrieved crate on). Three hub plates + three crates = exit. The
-- player keeps RETURNING to a hub that has visibly changed.
local function buildAtrium()
    shell("R10", -121.4, -137.4, 16)
    framedWall("R10 NearWall", -121.4, 16)
    framedWall("R10 FarWall", -137.4, 16)
    box("Door 10", vec3(0, 1.25, -137.4), vec3(2.2, 2.5, 0.32), "metal_red", true, false, "door")
    -- Wing W (x -8..-4): cage opened by sustained weight on p10w (drone/crate)
    box("Wing W N", vec3(-6, 1.6, -131.0), vec3(4, 3.6, 0.4), "concrete", true)
    box("Wing W S", vec3(-6, 1.6, -126.0), vec3(4, 3.6, 0.4), "concrete", true)
    box("Wing W E1", vec3(-4, 1.6, -130.15), vec3(0.4, 3.6, 1.7), "concrete", true)
    box("Wing W E2", vec3(-4, 1.6, -126.85), vec3(0.4, 3.6, 1.7), "concrete", true)
    box("Wing W Top", vec3(-4, 3.0, -128.5), vec3(0.4, 0.8, 1.6), "concrete", true)
    box("Cage W", vec3(-4, 1.25, -128.5), vec3(0.32, 2.5, 1.6), "metal_red", true, false, "door")
    -- Wing E (x 4..8): timed cage — body-press p10e and sprint
    box("Wing E N", vec3(6, 1.6, -133.0), vec3(4, 3.6, 0.4), "concrete", true)
    box("Wing E S", vec3(6, 1.6, -128.0), vec3(4, 3.6, 0.4), "concrete", true)
    box("Wing E W1", vec3(4, 1.6, -132.15), vec3(0.4, 3.6, 1.7), "concrete", true)
    box("Wing E W2", vec3(4, 1.6, -128.85), vec3(0.4, 3.6, 1.7), "concrete", true)
    box("Wing E Top", vec3(4, 3.0, -130.5), vec3(0.4, 0.8, 1.6), "concrete", true)
    box("Cage E", vec3(4, 1.25, -130.5), vec3(0.32, 2.5, 1.6), "metal_red", true, false, "door")
    -- Wing N (NW corner): crate behind an x-thin glass window
    box("Wing N S", vec3(-6, 1.6, -133.4), vec3(4, 3.6, 0.4), "concrete", true)
    box("Wing N E1", vec3(-4, 1.6, -136.95), vec3(0.4, 3.6, 0.9), "concrete", true)
    box("Wing N E2", vec3(-4, 1.6, -133.85), vec3(0.4, 3.6, 0.9), "concrete", true)
    box("Wing N Top", vec3(-4, 2.75, -135.4), vec3(0.4, 1.3, 2.2), "concrete", true)
    box("Glass 10", vec3(-4, 1.05, -135.4), vec3(0.12, 2.1, 2.2), "glass", true)
end

-- R11 The Range (week 12): charged launch + resonance bells, each taught in
-- isolation then composed once. Bell A rings off the tap-throw the player
-- already owns; Bell B is mounted high and only a charged shot arrives fast
-- enough; Bell C sits behind glass — one charged crate shatters the pane AND
-- rings the bell behind it (the shot carries through).
local function buildRange()
    shell("R11", -139.4, -157.4, 12)
    framedWall("R11 NearWall", -139.4, 12)
    framedWall("R11 FarWall", -157.4, 12)
    box("Door 11", vec3(0, 1.25, -157.4), vec3(2.2, 2.5, 0.32), "metal_red", true, false, "door")
    -- waist-high firing rail (walk around it on the east side)
    box("R11 Rail", vec3(-2.5, 0.55, -146.0), vec3(7.0, 1.1, 0.3), "metal", true)
    box("Bell A Stand", vec3(-3, 0.5, -149.35), vec3(0.18, 1.0, 0.18), "metal", true)
    box("Bell A", vec3(-3, 1.2, -149.0), vec3(1.0, 1.0, 0.7), "metal_orange", true, false,
        "bell")
    box("Bell B Arm", vec3(4.6, 3.0, -154.0), vec3(2.4, 0.14, 0.14), "metal", true)
    box("Bell B", vec3(3.4, 3.0, -154.0), vec3(1.0, 1.0, 0.7), "metal_orange", true, false,
        "bell")
    box("Bell C Stand", vec3(-3, 0.5, -156.65), vec3(0.18, 1.0, 0.18), "metal", true)
    box("Bell C", vec3(-3, 1.2, -156.3), vec3(1.0, 1.0, 0.7), "metal_orange", true, false,
        "bell")
    -- Bell C's pane, with a wing wall so the shot goes THROUGH, not around
    box("R11 Wing", vec3(-5.05, 1.6, -154.8), vec3(1.9, 3.6, 0.4), "concrete", true)
    box("Glass 11", vec3(-3, 1.05, -154.8), vec3(2.2, 2.1, 0.12), "glass", true)
    corridor("Cor11", -157.4)
end

-- R12 Errand (week 12): F taught in total isolation — a chasm no crate or
-- jump can solve. The drone pad across it is a plate only a stationed SPARK
-- can press; the bridge and door latch open so recalling SPARK never
-- strands anyone. Aiming INTO the chasm gets the head-shake: refusal
-- grammar, taught before it matters.
local function buildErrand()
    framedWall("R12 NearWall", -159.4, 10)
    box("R12 Floor S", vec3(0, -0.2, -161.9), vec3(10, 0.4, 5.0), "floor", true)
    box("R12 Floor N", vec3(0, -0.2, -171.4), vec3(10, 0.4, 4.0), "floor", true)
    box("R12 Wall W", vec3(-5, 1.6, -166.4), vec3(0.4, 3.6, 14), "concrete", true)
    box("R12 Wall E", vec3(5, 1.6, -166.4), vec3(0.4, 3.6, 14), "concrete", true)
    -- red rims: hazard language on both chasm lips
    box("R12 Rim S", vec3(0, 0.03, -164.55), vec3(9.2, 0.06, 0.3), "metal_red", true)
    box("R12 Rim N", vec3(0, 0.03, -169.25), vec3(9.2, 0.06, 0.3), "metal_red", true)
    -- the bridge starts stowed under the north deck and slides out
    box("Bridge 12", vec3(0, -0.65, -171.7), vec3(3.0, 0.4, 5.4), "metal_blue", true)
    framedWall("R12 FarWall", -173.4, 10)
    box("Door 12", vec3(0, 1.25, -173.4), vec3(2.2, 2.5, 0.32), "metal_red", true, false,
        "door")
    corridor("Cor12", -173.4)
end

-- R13 Alley-Oop (week 12): the combination room. A crate-eating pit, a
-- ledge with the exit — light crates fly to SPARK's catch (F the marker,
-- launch, watch the sag-and-socket), the heavy cube is too much for SPARK
-- and must be charge-launched onto its plate directly. The catwalk on the
-- east side is the player's own route up.
local function buildAlleyOop()
    framedWall("R13 NearWall", -175.4, 12)
    box("R13 Floor S", vec3(0, -0.2, -177.4), vec3(12, 0.4, 4.0), "floor", true)
    box("R13 Ledge", vec3(0, 0.5, -188.9), vec3(12, 1.4, 5.0), "concrete", true)
    box("R13 Wall W", vec3(-6, 1.6, -183.4), vec3(0.4, 3.6, 16), "concrete", true)
    box("R13 Wall E", vec3(6, 1.6, -183.4), vec3(0.4, 3.6, 16), "concrete", true)
    box("R13 Rim", vec3(0, 0.03, -179.55), vec3(11.2, 0.06, 0.3), "metal_red", true)
    -- catwalk steps over the pit's east edge (proven step-offset heights)
    box("R13 Step 1", vec3(5.0, 0.15, -180.4), vec3(1.6, 0.3, 2.0), "concrete", true)
    box("R13 Step 2", vec3(5.0, 0.3, -182.4), vec3(1.6, 0.6, 2.0), "concrete", true)
    box("R13 Step 3", vec3(5.0, 0.45, -184.9), vec3(1.6, 0.9, 3.0), "concrete", true)
    -- the ledge kit: SPARK's station marker (blue), the snap-socket, and the
    -- heavy plate (its slab spawns from plates2 with visY on the ledge)
    box("Marker 13", vec3(-3, 1.26, -187.3), vec3(1.2, 0.08, 1.2), "metal_blue", true)
    box("Socket 13", vec3(-3, 1.65, -188.5), vec3(1.0, 0.9, 1.0), "metal_orange", false,
        false, "socket")
    -- raised doorway: taller far-wall pieces, Door 13 sits on the ledge
    box("R13 FarWall W", vec3(-3.55, 2.1, -191.4), vec3(4.9, 4.6, 0.4), "concrete", true)
    box("R13 FarWall E", vec3(3.55, 2.1, -191.4), vec3(4.9, 4.6, 0.4), "concrete", true)
    box("R13 FarWall Top", vec3(0, 4.0, -191.4), vec3(2.2, 0.8, 0.4), "concrete", true)
    box("Door 13", vec3(0, 2.45, -191.4), vec3(2.2, 2.5, 0.32), "metal_red", true, false,
        "door")
    -- the heavy cube: 2.4x the light crate's edge, dark metal, launched only
    box("Heavy R13", vec3(3.5, 0.6, -176.8), vec3(1.2, 1.2, 1.2), "metal_dark", true, true,
        "crate")
    -- ledge-height corridor; R14's vestibule steps back down
    box("Cor13 Floor", vec3(0, 1.0, -192.4), vec3(3.0, 0.4, 2.0), "floor", true)
    box("Cor13 Wall W", vec3(-1.3, 2.8, -192.4), vec3(0.4, 3.6, 2.0), "concrete", true)
    box("Cor13 Wall E", vec3(1.3, 2.8, -192.4), vec3(0.4, 3.6, 2.0), "concrete", true)
end

-- R14 Duet (week 12): two lanes split by a divider — solid with a firing
-- aperture for stage 1, see-through glass for stages 2-3. The player's lane
-- is west; everything that matters is east, where only SPARK (and a good
-- throw) can go. Ends at the reunion orbit.
local function buildDuet()
    box("R14 NearWall W", vec3(-3.55, 1.6, -193.4), vec3(4.9, 3.6, 0.4), "concrete", true)
    box("R14 NearWall E", vec3(3.55, 1.6, -193.4), vec3(4.9, 3.6, 0.4), "concrete", true)
    box("R14 Floor", vec3(0, -0.2, -201.4), vec3(12, 0.4, 16), "floor", true)
    box("R14 Wall W", vec3(-6, 1.6, -201.4), vec3(0.4, 3.6, 16), "concrete", true)
    box("R14 Wall E", vec3(6, 1.6, -201.4), vec3(0.4, 3.6, 16), "concrete", true)
    -- vestibule steps down from the ledge-height Cor13 doorway
    box("R14 Step A", vec3(0, 0.4, -193.9), vec3(3.0, 0.8, 1.0), "concrete", true)
    box("R14 Step B", vec3(0, 0.2, -194.7), vec3(3.0, 0.4, 0.6), "concrete", true)
    -- divider, stage-1 section: full height with a generous firing aperture
    -- (x 0, y 2.2..3.4, z -197.7..-199.1) — the shot goes THROUGH
    box("R14 Div A1", vec3(0, 1.7, -196.55), vec3(0.3, 3.4, 2.3), "concrete", true)
    box("R14 Div A2", vec3(0, 1.7, -200.25), vec3(0.3, 3.4, 2.3), "concrete", true)
    box("R14 Div A3", vec3(0, 1.1, -198.4), vec3(0.3, 2.2, 1.4), "concrete", true)
    -- divider, stage-2/3 section: sill + unbreakable window band, open top
    box("R14 Div B Low", vec3(0, 0.55, -205.4), vec3(0.3, 1.1, 8.0), "concrete", true)
    box("R14 Div B Glass", vec3(0, 1.85, -205.4), vec3(0.12, 1.5, 8.0), "glass", true)
    -- stage 1: the bell on its pillar, dish turned toward the aperture
    box("Bell 14 Pillar", vec3(2.0, 1.5, -198.4), vec3(0.16, 3.0, 0.16), "metal", true)
    box("Bell 14", vec3(2.0, 3.05, -198.4), vec3(1.1, 1.1, 0.7), "metal_orange", true, false,
        "bell")
    forge.scene.setRotation("Bell 14", vec3(0, -90, 0))
    -- gates across the west lane (opening x -3.95..-2.35)
    for gi, gz in ipairs({ -201.4, -204.9 }) do
        local id = "R14 G" .. gi
        box(id .. " W", vec3(-4.975, 1.6, gz), vec3(2.05, 3.6, 0.4), "concrete", true)
        box(id .. " E", vec3(-1.25, 1.6, gz), vec3(2.2, 3.6, 0.4), "concrete", true)
        box(id .. " Top", vec3(-3.15, 3.0, gz), vec3(1.6, 0.8, 0.4), "concrete", true)
        box("Gate 14" .. (gi == 1 and "a" or "b"), vec3(-3.15, 1.25, gz),
            vec3(1.6, 2.5, 0.32), "metal_red", true, false, "door")
    end
    -- stage 3: pedestal + socket in the east lane, visible through the glass
    box("R14 Pedestal", vec3(3.5, 0.4, -207.4), vec3(1.2, 0.8, 1.2), "concrete", true)
    box("Socket 14", vec3(3.5, 1.25, -207.4), vec3(1.0, 0.9, 1.0), "metal_orange", false,
        false, "socket")
    -- far wall: the exit door sits in the WEST lane
    box("R14 FarWall W", vec3(-5.05, 1.6, -209.4), vec3(1.9, 3.6, 0.4), "concrete", true)
    box("R14 FarWall E", vec3(2.05, 1.6, -209.4), vec3(7.9, 3.6, 0.4), "concrete", true)
    box("R14 FarWall Top", vec3(-3, 3.0, -209.4), vec3(2.2, 0.8, 0.4), "concrete", true)
    box("Door 14", vec3(-3, 1.25, -209.4), vec3(2.2, 2.5, 0.32), "metal_red", true, false,
        "door")
    -- the end: a short landing and the light above (the week-12 finish line)
    box("End Pad", vec3(-3, -0.2, -211.4), vec3(3.0, 0.4, 4.0), "floor", true)
    box("End Wall W", vec3(-4.3, 1.6, -211.4), vec3(0.4, 3.6, 4.0), "concrete", true)
    box("End Wall E", vec3(-1.7, 1.6, -211.4), vec3(0.4, 3.6, 4.0), "concrete", true)
    box("End Cap", vec3(-3, 1.6, -213.2), vec3(3.0, 3.6, 0.4), "concrete", true)
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
        box("Door " .. i, vec3(0, 1.25, z0 - 10), vec3(2.2, 2.5, 0.32), "metal_red", true,
            false, "door")
        corridor("Cor" .. i, z0 - 10) -- Cor8 now leads on to R9 (week 8)
    end
    -- plates (visual slabs; logic reads the plates2 regions). visY/visScale:
    -- week-12 plates live on ledges and pedestals; offTex: the drone pad is
    -- blue by the colour law (blue = SPARK).
    for _, pl in ipairs(plates2) do
        box("Plate " .. pl.id, vec3(pl.center.x, pl.visY or 0.1, pl.center.z),
            pl.visScale or vec3(1.4, 0.2, 1.4), pl.offTex or "metal_orange", true, false,
            "plate")
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
            box(c.name, c.pos, vec3(0.5, 0.5, 0.5), "crate", true, true, "crate")
        end
    end
    -- week 8 looping rooms
    buildRelayRing()
    buildAtrium()
    corridor("Cor10", -137.4)
    -- week 12 (PITCH & CATCH): the old end cap became R11's doorway — the
    -- facility continues north through the launch pass.
    buildRange()
    buildErrand()
    buildAlleyOop()
    buildDuet()
    -- SPARK (week 11): a real body + eye rig instead of the glass box. Both
    -- colliderless — pure Lua steering, and setRotation only moves visuals.
    -- Blue eye by the colour law: blue = SPARK/drone.
    box("Drone", vec3(1.0, 2.4, 3.0), vec3(1, 1, 1), "metal", false, false, "drone_body")
    box("Drone Eye", vec3(1.0, 2.4, 2.78), vec3(1, 1, 1), "metal_blue", false, false, "drone_eye")
    forge.log("rooms 1-14 built (week 12: the launch pass - bells, F command, catch)")
end

-- ---------------------------------------------------------------------------
local spawnCount = 0
local won = false

function onStart()
    forge.log("scene.lua loaded (" .. _VERSION .. ")")
    buildRooms()
    -- Colour language (week 9): the host spawned the tutorial props before
    -- the law existed — retint them. red = locked, orange = interactable.
    forge.scene.setTexture("Exit Door", "metal_red")
    forge.scene.setTexture("Plate T", "metal_orange")
    -- Week 5 audio beds: persistent looping voices (own volume). Guarded so a
    -- hot-reload of this script doesn't stack a second copy on top.
    if not audioStarted then
        audioStarted = true
        forge.audio.loop("assets/sounds/ambient.wav", 0.30) -- HVAC room tone
        forge.audio.loop("assets/sounds/music.wav", 0.22)   -- minor pad underscore
    end
    forge.log("Q: park/recall drone (rooms 5, 6, 8)  F: send drone where you aim (week 12)")
end

-- Per-room lighting pass (week 5): key-light colour by player z (rgb * HDR
-- intensity; default warm facility key is (3.0, 2.9, 2.7)). Read most-negative
-- first since the layout runs north (-z).
local function roomLight(z)
    if z <= -210.6 then return vec3(2.6, 3.4, 2.6)      -- end pad: success glow
    elseif z <= -193.4 then return vec3(2.8, 2.6, 3.4)  -- R14 duet: violet finale
    elseif z <= -175.4 then return vec3(2.4, 2.9, 3.3)  -- R13 alley-oop: trust teal
    elseif z <= -159.4 then return vec3(3.3, 2.4, 2.5)  -- R12 errand: hazard red
    elseif z <= -139.4 then return vec3(3.4, 3.1, 2.3)  -- R11 range: hot amber
    elseif z <= -121.4 then return vec3(2.9, 2.6, 3.3)  -- R10 atrium: violet calm
    elseif z <= -105.4 then return vec3(3.4, 2.5, 2.1)  -- R9 relay: alarm amber-red
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
    if z <= -210.6 then return "VAULT cleared. Head for the light."
    elseif z <= -193.4 then return "R14: two lanes, one team - what you see through the glass, SPARK can reach"
    elseif z <= -175.4 then return "R13: SPARK catches light crates (F a spot, then launch) - the heavy one flies alone"
    elseif z <= -159.4 then return "R12: aim at the far pad and press F - SPARK flies where you point"
    elseif z <= -139.4 then return "R11: three bells - HOLD the throw to charge it"
    elseif z <= -121.4 then return "R10: three wings, three crates, three plates"
    elseif z <= -105.4 then return "R9: the beam holds the gate - pull the crate through behind you"
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

-- Player-as-weight (week 8 bugfix): the CHARACTER is not a broadphase body,
-- so physics.overlap never saw it — "stand on the plate" (R4, R10) silently
-- never worked. Plates now also test the player's position against the
-- region directly. Tight margin (0.15) so the hub's plate ROW can't be
-- double-pressed by standing between two plates.
local function playerNear(center, half, p)
    return math.abs(p.x - center.x) <= half.x + 0.15
       and math.abs(p.z - center.z) <= half.z + 0.15
       and p.y > center.y - 0.5 and p.y < center.y + 1.7
end

-- Pressure-plate sink (week 7): the DETECTION region is fixed in space, so
-- the visual slab is free to ease down ~8 cm when its plate reads pressed.
-- Generic plates rest at y 0.1; the tutorial slab ("Plate T") rests at 0.05.
local function updatePlateVisuals(dt)
    local step = 0.25 * dt
    for _, pl in ipairs(plates2) do
        local name = "Plate " .. pl.id
        local at = forge.scene.getPosition(name)
        local base = pl.visY or 0.1
        local ty = plateOn(pl.id) and (base - 0.08) or base
        forge.scene.setPosition(name, moveTowards(at, vec3(at.x, ty, at.z), step))
    end
    local tAt = forge.scene.getPosition("Plate T")
    local tTy = plate.pressed and 0.01 or 0.05
    forge.scene.setPosition("Plate T", moveTowards(tAt, vec3(tAt.x, tTy, tAt.z), step))
end

-- =========================================================================
-- Week 12 systems: PITCH & CATCH. Charged launch (the curve lives here, the
-- press-edges live in the host), crate impact tracking, resonance bells,
-- snap-sockets, and the aim helpers SPARK's F command reads.
-- =========================================================================

-- Crate motion tracking: position deltas give every tracked crate a velocity
-- WITHOUT body ids — the same samples feed bells (swept segment), sockets
-- (rest detection) and SPARK's catch. Teleports (respawn/reset warps) read
-- as impossible speeds and are discarded.
local crateTrack = {} -- name -> {id, px..pz cur, qx..qz prev, vx..vz, speed}
local function updateCrateTracking(dt)
    for _, c in ipairs(roomCrates) do
        local at = forge.scene.getPosition(c.name)
        local tr = crateTrack[c.name]
        if not tr then
            tr = { px = at.x, py = at.y, pz = at.z, vx = 0, vy = 0, vz = 0, speed = 0 }
            crateTrack[c.name] = tr
        else
            tr.qx, tr.qy, tr.qz = tr.px, tr.py, tr.pz -- segment start (prev frame)
            tr.px, tr.py, tr.pz = at.x, at.y, at.z
        end
        -- Resolve the crate's BODY id once (unambiguous overlap only), then
        -- read velocity from physics — the honest speed. Wall-clock deltas
        -- under-read whenever a slow frame simulates less time than it took
        -- (the anti-spiral clamp), which gutted every impact threshold.
        if not tr.id then
            local ids = forge.physics.overlap(at, vec3(0.5, 0.5, 0.5))
            if #ids == 1 then tr.id = ids[1] end
        end
        if tr.id and drone.carrying ~= c.name then
            local v = forge.physics.velocity(tr.id)
            local prev = tr.speed or 0
            tr.vx, tr.vy, tr.vz = v.x, v.y, v.z
            tr.speed = math.sqrt(v.x * v.x + v.y * v.y + v.z * v.z)
            -- Impact speed: by the frame the segment test sees the crate
            -- inside a target's AABB, Jolt may have already resolved the
            -- contact and REFLECTED the velocity — the approach speed one
            -- frame earlier is the honest hit strength.
            tr.impact = math.max(tr.speed, prev)
        else
            tr.vx, tr.vy, tr.vz, tr.speed, tr.impact = 0, 0, 0, 0, 0
        end
    end
end

-- Charged launch: forge.player.charge() reports how long the charge-press
-- has been held; this maps it to a speed. The 0.15 s dead zone keeps every
-- tap at the legacy 11 m/s — all ten shipped rooms stand untouched. The
-- whine is three crossfaded loops, so the pitch RISES then audibly PLATEAUS;
-- the click at full power is the taught ceiling tell (sound first, no text).
local reticleCmdT = 0 -- blue command-flash countdown (F answers on the reticle)
local chargeState = { whines = nil, clicked = false, charge01 = 0, was = false }
local function updateCharge(dt)
    if reticleCmdT > 0 then
        reticleCmdT = reticleCmdT - dt
        forge.hud.set_reticle("command", math.min(reticleCmdT / 0.35, 1.0))
        if reticleCmdT <= 0 then
            forge.hud.set_reticle("default", chargeState.charge01)
        end
    end
    local c = forge.player.charge()
    if c > 0 then
        local k = math.min(math.max((c - 0.15) / 0.9, 0.0), 1.0)
        chargeState.charge01 = k
        chargeState.was = true
        forge.player.set_throw_speed(11.0 + 14.0 * k)
        if not chargeState.whines then
            chargeState.whines = {
                forge.audio.loop("assets/sounds/whine_low.wav", 0.05),
                forge.audio.loop("assets/sounds/whine_mid.wav", 0.0),
                forge.audio.loop("assets/sounds/whine_high.wav", 0.0),
            }
        end
        local w = chargeState.whines
        forge.audio.set_volume(w[1], 0.06 + 0.26 * math.max(0.0, 1.0 - k * 1.8))
        forge.audio.set_volume(w[2], 0.30 * math.max(0.0, 1.0 - math.abs(k - 0.55) * 2.4))
        forge.audio.set_volume(w[3], 0.34 * math.max(0.0, (k - 0.5) * 2.0))
        if k >= 1.0 and not chargeState.clicked then
            chargeState.clicked = true
            forge.audio.play("assets/sounds/charge_click.wav", 0.9)
        end
        if reticleCmdT <= 0 then
            forge.hud.set_reticle("default", k)
        end
    elseif chargeState.was then
        chargeState.was = false
        chargeState.clicked = false
        chargeState.charge01 = 0
        if chargeState.whines then
            for _, h in ipairs(chargeState.whines) do forge.audio.stop(h) end
            chargeState.whines = nil
        end
        if reticleCmdT <= 0 then
            forge.hud.set_reticle("default", 0)
        end
    end
end

-- Segment vs AABB (slab method): the anti-tunnel test. A full-charge crate
-- moves ~0.4 m per frame; sampling positions alone could skip a thin dish.
local function segmentHitsBox(x0, y0, z0, x1, y1, z1, c, hx, hy, hz)
    local tmin, tmax = 0.0, 1.0
    local p0 = { x0, y0, z0 }
    local d = { x1 - x0, y1 - y0, z1 - z0 }
    local lo = { c.x - hx, c.y - hy, c.z - hz }
    local hi = { c.x + hx, c.y + hy, c.z + hz }
    for i = 1, 3 do
        if math.abs(d[i]) < 1e-9 then
            if p0[i] < lo[i] or p0[i] > hi[i] then return false end
        else
            local t1, t2 = (lo[i] - p0[i]) / d[i], (hi[i] - p0[i]) / d[i]
            if t1 > t2 then t1, t2 = t2, t1 end
            if t1 > tmin then tmin = t1 end
            if t2 < tmax then tmax = t2 end
            if tmin > tmax then return false end
        end
    end
    return true
end

-- Resonance bells: hit hard enough and they RING (latched green signal any
-- door can read); too soft answers a dull thunk — "harder". relatch bells
-- re-arm after N seconds (R14's timed gate). Instruments, not enemies.
local bellFx = {}     -- name -> wobble seconds remaining
local bellCool = {}   -- id -> thunk cooldown
local bellThunks = 0  -- R11 soft-hint counter (3 thunks -> one text nudge)
local function updateBells(dt)
    for _, b in ipairs(bells2) do
        if b.relatch and state.bells[b.id] == true then
            b.relatchT = (b.relatchT or b.relatch) - dt
            if b.relatchT <= 0 then
                state.bells[b.id] = false
                b.relatchT = nil
                forge.scene.setTexture(b.name, "metal_orange")
                forge.audio.play("assets/sounds/grab.wav", 0.5)
                forge.log("bell " .. b.id .. ": re-armed")
            end
        end
        bellCool[b.id] = math.max((bellCool[b.id] or 0) - dt, 0)
        if state.bells[b.id] ~= true then
            for _, c in ipairs(roomCrates) do
                local tr = crateTrack[c.name]
                if tr and tr.qx and (tr.impact or 0) > 6.0 then
                    if segmentHitsBox(tr.qx, tr.qy, tr.qz, tr.px, tr.py, tr.pz, b.center,
                                      b.half.x + 0.3, b.half.y + 0.3, b.half.z + 0.3) then
                        if tr.impact >= b.ringSpeed then
                            state.bells[b.id] = true
                            b.relatchT = b.relatch
                            bellFx[b.name] = 0.9
                            forge.scene.setTexture(b.name, "metal_green")
                            forge.audio.play("assets/sounds/gong.wav", 0.9)
                            forge.fx.burst(b.center, 26)
                            forge.log("bell " .. b.id .. ": RUNG ("
                                      .. string.format("%.1f", tr.impact) .. " m/s)")
                        elseif bellCool[b.id] <= 0 then
                            bellCool[b.id] = 0.5
                            bellFx[b.name] = 0.25
                            bellThunks = bellThunks + 1
                            forge.audio.play("assets/sounds/thunk.wav", 0.8)
                            forge.fx.burst(b.center, 4)
                            forge.log("bell " .. b.id .. ": too soft ("
                                      .. string.format("%.1f", tr.impact) .. " m/s)")
                        end
                        break
                    end
                end
            end
        end
        -- Wobble answer-back: a decaying euler shake on the dish (visual
        -- only), composed over the bell's base orientation.
        local k = bellFx[b.name]
        if k then
            k = k - dt
            local br = b.baseRot or vec3(0, 0, 0)
            if k <= 0 then
                bellFx[b.name] = nil
                forge.scene.setRotation(b.name, br)
            else
                bellFx[b.name] = k
                local a = math.sin(k * 22.0) * 7.0 * k
                forge.scene.setRotation(b.name, vec3(br.x + a, br.y, br.z + a * 0.5))
            end
        end
    end
end

-- Snap-sockets: the forgiveness basin. Lands close and slow -> eased into
-- the seat; resting in the seat is the (continuous) signal. Heavy crates
-- and glove-held crates are never eased.
local function updateSockets(dt, pp)
    for _, s in ipairs(sockets2) do
        local occupied = false
        -- hysteresis: a seated crate stays "filled" through a small bounce
        local keepD2, keepDy = 0.12, 0.35
        if state.sockets[s.id] == true then
            keepD2, keepDy = 0.30, 0.55
        end
        for _, c in ipairs(roomCrates) do
            if c.name:sub(1, 5) ~= "Heavy" and drone.carrying ~= c.name then
                local at = forge.scene.getPosition(c.name)
                local dx, dy, dz = at.x - s.seat.x, at.y - s.seat.y, at.z - s.seat.z
                local d2 = dx * dx + dz * dz
                if d2 < keepD2 and math.abs(dy) < keepDy then
                    occupied = true
                else
                    local pd = vec3(at.x - pp.x, at.y - pp.y, at.z - pp.z)
                    local nearPlayer = pd.x * pd.x + pd.y * pd.y + pd.z * pd.z < 5.8
                    local tr = crateTrack[c.name]
                    if d2 < 0.64 and math.abs(dy) < 0.9 and (tr and tr.speed or 0) < 3.5
                       and not nearPlayer then
                        forge.scene.setPosition(c.name, moveTowards(at, s.seat, 3.0 * dt))
                        for _, body in ipairs(forge.physics.overlap(at, vec3(0.3, 0.3, 0.3))) do
                            forge.physics.set_velocity(body, vec3(0, 0, 0))
                        end
                    end
                end
            end
        end
        if occupied ~= (state.sockets[s.id] == true) then
            state.sockets[s.id] = occupied
            forge.audio.play(occupied and "assets/sounds/clunk.wav" or "assets/sounds/grab.wav")
            forge.scene.setTexture(s.name, occupied and "metal_green" or "metal_orange")
            forge.fx.burst(s.seat, 10)
            forge.log("socket " .. s.id .. (occupied and ": filled" or ": empty"))
        end
    end
end

-- Aim helpers. aimRay passes through listed see-through panes (R14's
-- divider glass: you can SEE it, SPARK can BE there); flightClear is the
-- straight-line validity rule — a blocked path is a characterful refusal,
-- a stuck drone would be a bug.
local function inBox(p, c, h, pad)
    return math.abs(p.x - c.x) <= h.x + pad and math.abs(p.y - c.y) <= h.y + pad
       and math.abs(p.z - c.z) <= h.z + pad
end

local function aimRay(origin, dir, maxDist)
    local dist, traveled = maxDist, 0.0
    for _ = 1, 3 do
        local _, d = forge.physics.raycast(origin, dir, dist)
        if not d then return nil end
        local hit = vec3(origin.x + dir.x * d, origin.y + dir.y * d, origin.z + dir.z * d)
        local through = false
        for _, tp in ipairs(transparents) do
            if inBox(hit, tp.center, tp.half, 0.08) then
                through = true
                break
            end
        end
        if not through then return hit, traveled + d end
        origin = vec3(hit.x + dir.x * 0.25, hit.y + dir.y * 0.25, hit.z + dir.z * 0.25)
        traveled = traveled + d + 0.25
        dist = dist - d - 0.25
        if dist <= 0.1 then return nil end
    end
    return nil
end

local function flightClear(from, to)
    local dx, dy, dz = to.x - from.x, to.y - from.y, to.z - from.z
    local len = math.sqrt(dx * dx + dy * dy + dz * dz)
    if len < 0.5 then return true end
    local hit = aimRay(from, vec3(dx / len, dy / len, dz / len), len - 0.45)
    return hit == nil
end

-- A settled, F-stationed SPARK is weight: plates read its 2D footprint the
-- way they read a parked drone (parkId path untouched).
local function droneStationedOn(pl)
    if not drone.station then return false end
    local at = forge.scene.getPosition(drone.name)
    local sx = at.x - drone.station.x
    local sy = at.y - drone.station.y
    local sz = at.z - drone.station.z
    if sx * sx + sy * sy + sz * sz > 0.16 then return false end
    return math.abs(drone.station.x - pl.center.x) <= pl.half.x + 0.35
       and math.abs(drone.station.z - pl.center.z) <= pl.half.z + 0.35
end

-- Nearest deposit point (socket seat or plate top) within reach of the
-- station: where a caught crate gets placed.
local function findDeposit()
    local from = drone.station or forge.scene.getPosition(drone.name)
    local best, bestD2 = nil, 2.8 * 2.8
    for _, s in ipairs(sockets2) do
        local dx, dz = s.seat.x - from.x, s.seat.z - from.z
        local d2 = dx * dx + dz * dz
        if d2 < bestD2 then
            best, bestD2 = vec3(s.seat.x, s.seat.y, s.seat.z), d2
        end
    end
    for _, pl in ipairs(plates2) do
        local dx, dz = pl.center.x - from.x, pl.center.z - from.z
        local d2 = dx * dx + dz * dz
        if d2 < bestD2 then
            best, bestD2 = vec3(pl.center.x, pl.center.y, pl.center.z), d2
        end
    end
    return best
end

-- SPARK personality pass (week 11): the drone is a body + eye rig now. It
-- flies nose-first, banks into turns, watches the player when settled, and
-- ANSWERS commands with motion on top of the week-9 beeps — a decaying
-- barrel-roll waggle for "yes", a firm head-shake with a pinched aperture
-- for "no". All procedural Lua; setRotation (ADR-027) is the engine seam.
local droneTime = 0
local droneYaw = 0
local droneMood, droneMoodT = nil, 0 -- "happy" | "no", seconds remaining
local atan2 = math.atan2 or math.atan -- two-arg atan across Lua versions
-- Yaw (deg) that points the model's -Z forward along (dx, dz).
local function yawToward(dx, dz) return math.deg(atan2(-dx, -dz)) end
local function shortArc(a) return (a + 180) % 360 - 180 end

local function updateDrone(dt)
    droneTime = droneTime + dt
    local p = forge.player.position()
    local at = forge.scene.getPosition(drone.name)
    if forge.input.pressed("q") then
        -- The drone ANSWERS (week 9, SPARK spirit): happy double-beep for a
        -- command it takes, a low buzz for "nothing to hold here". Q is the
        -- UNCHANGED park verb; it supersedes any F station.
        drone.station = nil
        if drone.parkedAt then
            drone.parkedAt = nil
            forge.audio.play("assets/sounds/beep_ok.wav")
            droneMood, droneMoodT = "happy", 0.8
            forge.log("drone: recalled")
        else
            for id, park in pairs(drone.parks) do
                if p.z > park.zMin and p.z < park.zMax then
                    drone.parkedAt = id
                    forge.audio.play("assets/sounds/beep_ok.wav")
                    droneMood, droneMoodT = "happy", 0.8
                    forge.log("drone: parking at " .. id)
                    break
                end
            end
            if not drone.parkedAt then
                forge.audio.play("assets/sounds/beep_no.wav")
                droneMood, droneMoodT = "no", 0.8
                forge.log("drone: nothing to hold here")
            end
        end
    end

    -- F (week 12): point + command. Go THERE. The aim ray passes the listed
    -- see-through panes; refusals (no hit, no-fly, blocked flight) get the
    -- shipped head-shake — one clip, three teaching jobs.
    if forge.input.pressed("f") then
        local eye, dir = forge.player.look()
        local hit = aimRay(eye, dir, 30.0)
        local accepted = false
        if hit then
            local dxp, dzp = hit.x - p.x, hit.z - p.z
            if dxp * dxp + dzp * dzp < 4.0 then
                -- F at your own feet (or your held crate): come back
                drone.station = nil
                drone.parkedAt = nil
                drone.deposit = nil
                forge.audio.play("assets/sounds/beep_ok.wav")
                droneMood, droneMoodT = "happy", 0.6
                forge.log("drone: returning")
                accepted = true
            else
                local station = vec3(math.floor(hit.x * 2 + 0.5) / 2, hit.y + 0.9,
                                     math.floor(hit.z * 2 + 0.5) / 2)
                local ok = true
                for _, z in ipairs(noFly) do
                    if inBox(station, z.center, z.half, 0) then
                        ok = false
                        break
                    end
                end
                if ok and not flightClear(at, station) then ok = false end
                if ok then
                    drone.station = station
                    drone.parkedAt = nil
                    drone.deposit = nil
                    forge.audio.play("assets/sounds/beep_ok.wav")
                    droneMood, droneMoodT = "ack", 0.45 -- eye snaps wide, nose dips
                    reticleCmdT = 0.35                  -- the reticle answers in blue
                    forge.fx.burst(station, 6)
                    forge.log(string.format("drone: go there (%.1f, %.1f, %.1f)",
                                            station.x, station.y, station.z))
                    accepted = true
                end
            end
        end
        if not accepted then
            forge.audio.play("assets/sounds/beep_no.wav")
            droneMood, droneMoodT = "no", 0.8
            forge.log("drone: can't go there")
        end
    end

    -- Catch: a stationed SPARK snatches fast LIGHT crates that reach it.
    -- Heavy crates trigger the sink-drop-headshake — the mass rule taught
    -- as comedy, enforced as constraint.
    if drone.station and not drone.carrying and drone.failT <= 0 then
        for _, c in ipairs(roomCrates) do
            local tr = crateTrack[c.name]
            if tr and tr.speed > 6.0 then
                local cx, cy, cz = at.x - tr.px, at.y - tr.py, at.z - tr.pz
                local d2 = cx * cx + cy * cy + cz * cz
                local pdx, pdy, pdz = tr.px - p.x, tr.py - p.y, tr.pz - p.z
                local farFromPlayer = pdx * pdx + pdy * pdy + pdz * pdz > 6.8
                local approaching = tr.vx * cx + tr.vy * cy + tr.vz * cz > 0
                if d2 < 1.7 and farFromPlayer and approaching then
                    if c.name:sub(1, 5) == "Heavy" then
                        drone.failT = 1.3 -- sink under it, drop it, shake it off
                        forge.audio.play("assets/sounds/beep_no.wav")
                        droneMood, droneMoodT = "no", 1.0
                        forge.log("drone: TOO HEAVY")
                    else
                        drone.carrying = c.name
                        drone.sagT = 0.8
                        drone.deposit = findDeposit()
                        if not drone.trust then
                            drone.trust = true -- the arc: flinches become braces
                            forge.log("drone: first catch - trust unlocked")
                        end
                        forge.audio.play("assets/sounds/snap.wav", 0.9)
                        forge.fx.burst(vec3(tr.px, tr.py, tr.pz), 14)
                        forge.log("drone: CAUGHT " .. c.name)
                    end
                    break
                end
            end
        end
    end
    drone.failT = math.max(drone.failT - dt, 0)
    drone.celebrateT = math.max(drone.celebrateT - dt, 0)

    -- Movement target, by priority: fail-sink > deposit run > station >
    -- park > reunion orbit > shoulder cruise.
    local target
    if drone.failT > 0 and drone.station then
        target = vec3(drone.station.x, drone.station.y - 0.75, drone.station.z)
    elseif drone.carrying and drone.deposit then
        target = vec3(drone.deposit.x, drone.deposit.y + 1.15, drone.deposit.z)
    elseif drone.station then
        target = drone.station
    elseif drone.parkedAt then
        target = drone.parks[drone.parkedAt].pos
    elseif drone.celebrateT > 0 then
        local a = drone.celebrateT * 2.6
        target = vec3(p.x + math.cos(a) * 1.6,
                      p.y + 0.6 + (6.0 - drone.celebrateT) * 0.35,
                      p.z + math.sin(a) * 1.6)
    else
        -- cruise ABOVE the walls toward a spot beside/ahead of the player
        target = vec3(p.x + 1.0, 4.2, p.z - 1.3)
    end
    local speed = (drone.station or drone.celebrateT > 0) and 9.0 or drone.speed
    local nextPos = moveTowards(at, target, speed * dt)
    nextPos.y = nextPos.y + math.sin(droneTime * 3.0) * 0.02 -- idle bob
    if drone.sagT > 0 then
        -- catch reaction: sag under the momentum, two recovery bobs
        drone.sagT = drone.sagT - dt
        local k = math.max(drone.sagT, 0) / 0.8
        nextPos.y = nextPos.y - (0.45 * k - math.sin((1.0 - k) * 12.6) * 0.1 * k)
    end
    if droneMood == "flinch" and droneMoodT > 0 then
        nextPos.y = nextPos.y - 0.4 * droneMoodT -- duck!
    end
    forge.scene.setPosition(drone.name, nextPos)

    -- Carried crate rides the underside, velocity pinned; released over the
    -- deposit point (the socket's forgiveness basin does the final seating).
    if drone.carrying then
        local hold = vec3(nextPos.x, nextPos.y - 0.38, nextPos.z)
        forge.scene.setPosition(drone.carrying, hold)
        for _, body in ipairs(forge.physics.overlap(hold, vec3(0.3, 0.3, 0.3))) do
            forge.physics.set_velocity(body, vec3(0, 0, 0))
        end
        if drone.deposit then
            local ddx, ddz = nextPos.x - drone.deposit.x, nextPos.z - drone.deposit.z
            if ddx * ddx + ddz * ddz < 0.09 then
                drone.carrying = nil
                drone.deposit = nil
                forge.audio.play("assets/sounds/beep_ok.wav", 0.8)
                droneMood, droneMoodT = "happy", 0.9 -- the proud barrel-roll
                forge.log("drone: delivered")
            end
        end
    end

    -- Facing: nose-first when going somewhere, watch the player when not.
    local mx, mz = target.x - at.x, target.z - at.z
    local moveDist = math.sqrt(mx * mx + mz * mz)
    local wantYaw
    local eyeScale = 1.0
    if moveDist > 0.35 then
        wantYaw = yawToward(mx, mz)
    else
        wantYaw = yawToward(p.x - at.x, p.z - at.z)
        -- Bell curiosity (R11 only, room-flagged): when idle, glance at the
        -- nearest unrung bell — a zero-cost hint in the learned grammar.
        if not drone.station and not drone.parkedAt and p.z <= drone.bellZone.zMax
           and p.z >= drone.bellZone.zMin then
            local best, bestD2 = nil, 1e9
            for _, b in ipairs(bells2) do
                if state.bells[b.id] ~= true and b.center.z >= drone.bellZone.zMin then
                    local bx, bz = b.center.x - at.x, b.center.z - at.z
                    local d2 = bx * bx + bz * bz
                    if d2 < bestD2 then best, bestD2 = b, d2 end
                end
            end
            if best then
                wantYaw = yawToward(best.center.x - at.x, best.center.z - at.z)
                eyeScale = 1.12
            end
        end
    end
    local turn = shortArc(wantYaw - droneYaw)
    local maxTurn = 240 * dt
    if turn > maxTurn then turn = maxTurn elseif turn < -maxTurn then turn = -maxTurn end
    droneYaw = droneYaw + turn
    -- Bank into the turn, tip into motion; a live mood says it louder.
    local roll = -turn * 2.2
    local pitch = -math.min(moveDist * 6.0, 14.0)
    -- Catcher stance (room-flagged: only where a catch solution exists):
    -- lean back, aperture wide, slow ready sway. The stance IS the hint.
    if drone.station and not drone.carrying and moveDist <= 0.35 and drone.failT <= 0 then
        for _, zn in ipairs(drone.catcherZones) do
            if drone.station.z >= zn.zMin and drone.station.z <= zn.zMax then
                pitch = 15.0
                eyeScale = math.max(eyeScale, 1.3)
                roll = roll + math.sin(droneTime * 2.2) * 6.0
                break
            end
        end
    end
    if drone.trust and chargeState.charge01 > 0.2 then
        -- Trust arc: a charging glove aimed AT SPARK reads as eager, not
        -- threatening — brace and lean in.
        local eye, dir = forge.player.look()
        local vx, vy, vz = at.x - eye.x, at.y - eye.y, at.z - eye.z
        local vl = math.sqrt(vx * vx + vy * vy + vz * vz)
        if vl > 0.5 and (vx * dir.x + vy * dir.y + vz * dir.z) / vl > 0.98 then
            pitch = 10.0
            eyeScale = math.max(eyeScale, 1.35)
        end
    elseif not drone.trust and not drone.station and droneMoodT <= 0 then
        -- Before trust: an un-commanded fast crate whizzing past = flinch.
        for _, c in ipairs(roomCrates) do
            local tr = crateTrack[c.name]
            if tr and tr.speed > 7.0 then
                local fx, fy, fz = tr.px - at.x, tr.py - at.y, tr.pz - at.z
                if fx * fx + fy * fy + fz * fz < 2.0 then
                    droneMood, droneMoodT = "flinch", 0.6
                    break
                end
            end
        end
    end
    if droneMoodT > 0 then
        droneMoodT = droneMoodT - dt
        local k = math.max(droneMoodT, 0)
        if droneMood == "happy" then
            roll = roll + math.sin(k * 18.0) * 22.0 * k -- decaying waggle
            eyeScale = 1.0 + 0.4 * k                    -- aperture pops wide
        elseif droneMood == "ack" then
            pitch = pitch - 33.0 * k                    -- nose-dip toward the point
            eyeScale = 1.0 + 0.5 * k                    -- aperture snaps wide
        elseif droneMood == "flinch" then
            droneYaw = droneYaw + math.sin(k * 26.0) * 5.0 * k -- rattled
            eyeScale = 0.7                                     -- squint
        else -- "no"
            droneYaw = droneYaw + math.sin(k * 20.0) * 9.0 * k -- head-shake
            eyeScale = 1.0 - 0.45 * k                          -- pinched
        end
    end
    if roll > 25 then roll = 25 elseif roll < -25 then roll = -25 end
    forge.scene.setRotation(drone.name, vec3(pitch, droneYaw, roll))
    -- The eye rides the body's front, mirrors its pose, pulses with mood.
    local yr = math.rad(droneYaw)
    forge.scene.setPosition("Drone Eye",
                            vec3(nextPos.x - math.sin(yr) * 0.22, nextPos.y + 0.01,
                                 nextPos.z - math.cos(yr) * 0.22))
    forge.scene.setRotation("Drone Eye", vec3(pitch, droneYaw, roll))
    forge.scene.setScale("Drone Eye", vec3(eyeScale, eyeScale, eyeScale))
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

-- Continue (week 10): the host calls this with the persisted checkpoint when
-- the player picks Continue on the title. A fresh-state run that starts deep:
-- every room is self-contained (its crates spawn in-room), so no latch replay
-- is needed — warp, sync the ladders, done.
function resumeAt(cp)
    curCp = math.max(1, math.min(cp, #checkpoints))
    if curCp > furthest then
        furthest = curCp
    end
    forge.player.teleport(checkpoints[curCp].pos)
    forge.audio.play("assets/sounds/chime.wav", 0.6)
    forge.log("continue: resumed at checkpoint " .. curCp)
end

local function updateRespawn(p)
    -- Advance the checkpoint as the player crosses each room's near wall.
    while curCp < #checkpoints and p.z <= checkpoints[curCp + 1].z do
        curCp = curCp + 1
        forge.audio.play("assets/sounds/chime.wav", 0.6) -- week 9: reward tone
        forge.log("checkpoint reached (" .. curCp .. ")")
        if curCp > furthest then -- week 10: report new bests; host persists
            furthest = curCp
            forge.game.save_checkpoint(furthest)
        end
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
    state.bells = {}
    state.sockets = {}
    for _, d in pairs(doors2) do d.want = false end
    for _, pl in ipairs(plates2) do pl.timer = nil end -- timed plates disarm
    drone.parkedAt = nil
    drone.station = nil
    drone.carrying = nil
    drone.deposit = nil
    drone.sagT = 0
    drone.failT = 0
    drone.celebrateT = 0
    drone.trust = false -- the arc restarts with the run
    plate.pressed = false
    laser.blocked = false
    curHint = nil -- re-push the hint on the first frame back
    -- Colour language resets EXPLICITLY: clearing the latches above skips the
    -- state-change handlers, so green things would stay green without this.
    for name in pairs(doors2) do
        forge.scene.setTexture(name, doors2[name].offTex or "metal_red")
    end
    for _, pl in ipairs(plates2) do
        forge.scene.setTexture("Plate " .. pl.id, pl.offTex or "metal_orange")
    end
    for _, b in ipairs(bells2) do
        b.relatchT = nil
        forge.scene.setTexture(b.name, "metal_orange")
        forge.scene.setRotation(b.name, b.baseRot or vec3(0, 0, 0))
    end
    for _, s in ipairs(sockets2) do forge.scene.setTexture(s.name, "metal_orange") end
    forge.hud.set_reticle("default", 0)
    for _, lz in ipairs(lasers2) do
        forge.scene.setTexture("Receiver " .. lz.id:gsub("^l", ""), "laser")
    end
    forge.scene.setTexture("Exit Door", "metal_red")
    forge.scene.setTexture("Plate T", "metal_orange")
    forge.scene.setTexture("Laser Receiver", "laser")
    -- Re-spawn shattered panes (each pane knows its own scale — Glass 10
    -- faces sideways, so the old shared z-thin scale no longer fits all).
    for _, gl in ipairs(glasses2) do
        if gl.broken then
            forge.scene.spawn(gl.name, gl.center, gl.scale, gl.halfE, "glass", false)
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

    -- ---- Objective hint: push only on change (crossing a room boundary).
    -- R11's one text nudge fires only after three too-soft hits (the sound
    -- teaches first; text is the fallback, per the soft-hints law). ----
    local h = roomHint(pp.z)
    if bellThunks >= 3 and pp.z <= -139.4 and pp.z > -157.4 then
        h = "the glove can push HARDER - HOLD the throw, listen for the click"
    end
    if h ~= curHint then
        curHint = h
        forge.hud.set_hint(h)
    end

    -- ---- Tutorial (bespoke) ----
    local pressedNow = #forge.physics.overlap(plate.center, plate.half) > 0
                       or playerNear(plate.center, plate.half, pp)
    if pressedNow ~= plate.pressed then
        plate.pressed = pressedNow
        forge.audio.play(pressedNow and "assets/sounds/land.wav" or "assets/sounds/grab.wav")
        -- Answer back (week 9): light + sound + particles, synced.
        forge.scene.setTexture("Plate T", pressedNow and "metal_green" or "metal_orange")
        forge.scene.setTexture("Exit Door", pressedNow and "metal_green" or "metal_red")
        forge.fx.burst(vec3(plate.center.x, 0.5, plate.center.z), 8)
        forge.fx.burst(vec3(door.closed.x, 0.4, door.closed.z + 0.4), 10) -- door steam
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
        forge.scene.setTexture(laser.receiver, blockedNow and "metal_green" or "laser")
        forge.fx.burst(receiverPos, 8)
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

    -- ---- Week 12 systems: tracking feeds bells + the catch; the charge
    -- curve feeds the host's release-edge throw. Order matters: track first.
    updateCrateTracking(dt)
    updateCharge(dt)
    updateBells(dt)
    updateSockets(dt, pp)

    -- ---- Drone + plate visuals ----
    updateDrone(dt)
    updatePlateVisuals(dt)

    -- ---- Rooms 1-8 generic systems ----
    for _, pl in ipairs(plates2) do
        local raw = #forge.physics.overlap(pl.center, pl.half) > 0
                    or (pl.parkId ~= nil and droneAt(pl.parkId))
                    or playerNear(pl.center, pl.half, pp)
                    or droneStationedOn(pl) -- week 12: F-stationed SPARK is weight
        if pl.latch and state.plates[pl.id] == true then
            raw = true -- latched plates hold their signal (R12's bridge)
        end
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
            forge.scene.setTexture("Plate " .. pl.id,
                                   now and "metal_green" or (pl.offTex or "metal_orange"))
            forge.fx.burst(vec3(pl.center.x, pl.center.y + 0.15, pl.center.z), 8)
            forge.log("plate " .. pl.id .. (now and ": on" or ": off"))
        end
    end
    for _, lz in ipairs(lasers2) do
        local range = lz.maxDist or 9.2
        local _, hd = forge.physics.raycast(lz.origin, vec3(1, 0, 0), range)
        local d2 = hd or range
        local cut = d2 < (lz.receiverX - lz.origin.x) - 0.3
                    or (lz.parkId ~= nil and droneAt(lz.parkId))
        forge.scene.setPosition(lz.beam, vec3(lz.origin.x + d2 * 0.5, lz.origin.y, lz.origin.z))
        forge.scene.setScale(lz.beam, vec3(d2, 0.05, 0.05))
        if cut ~= (state.lasers[lz.id] == true) then
            state.lasers[lz.id] = cut
            forge.audio.play(cut and "assets/sounds/grab.wav" or "assets/sounds/jump.wav")
            -- receiver names: l2 -> "Receiver 2", r1 -> "Receiver r1"
            local rname = "Receiver " .. lz.id:gsub("^l", "")
            forge.scene.setTexture(rname, cut and "metal_green" or "laser")
            forge.fx.burst(forge.scene.getPosition(rname), 8)
            forge.log("laser " .. lz.id .. (cut and ": blocked" or ": restored"))
        end
    end
    for name, d in pairs(doors2) do
        local wantNow = d.cond()
        if wantNow ~= d.want then
            d.want = wantNow
            forge.audio.play("assets/sounds/land.wav", 0.5) -- door motor thunk
            forge.scene.setTexture(name, wantNow and "metal_green" or (d.offTex or "metal_red"))
            forge.fx.burst(vec3(d.closed.x, d.closed.y - 0.85, d.closed.z + 0.4), 10)
            forge.log(name .. (wantNow and ": opening" or ": closing"))
            -- The reunion (week 12): the final door opening means the duet is
            -- solved — SPARK flies its ascending orbit while you walk out.
            if name == "Door 14" and wantNow then
                drone.celebrateT = 6.0
            end
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
            forge.audio.play("assets/sounds/chime.wav")
            forge.fx.burst(vec3(p.x, p.y + 0.5, p.z), 30)
            forge.log("*** VAULT RUN COMPLETE: all 14 rooms ***")
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
