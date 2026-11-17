-- assets/scripts/spillway.lua - Location 3: THE SPILLWAY (ADR-034).
-- Included by scene.lua after surface.lua. Runs on the VAULT kit like the
-- Surface: sockets, doors, crates and checkpoints appended below are real
-- entries in the shared tables.
--
-- After the Surface transmitter, Wren's caravan answers: it is stranded on the
-- far side of an old hydro dam whose gates jammed. The SIGNAL mission now ends
-- by starting CROSSING and opening the rail cart in the relay yard (fade +
-- teleport, like the freight lift). Winning the Spillway ends the run.
--
-- Layout (world x 760..840, z -28..36, plaza top y = 1.0, WATER rest y = 0;
-- north is -z, the dam is the north wall, its crest at y = 9):
--   S: the landing (rail cart) and the plaza
--   NW  THE SLUICE   - a flooded chamber. A counterweight plate by its mouth
--                      opens the sluice in proportion to load (player = 1,
--                      crate = 1; 3 opens it and latches it for 35 s). The
--                      water drops in steps and stepping stones surface; the
--                      key stands at the far end.
--   N   THE CURRENT  - a gorge whose water runs for a drop. Crates thrown in
--                      float to the grate and lodge in a row: stepping stones
--                      leading UPSTREAM to the key. Crates that slip past the
--                      grate go over the drop and come home.
--   NE  THE TURBINE HALL - dark. SPARK carries the light (send it ahead, F).
--                      A beam, a prism and a receiver restart the runner;
--                      that opens the key's cage.
--   W   the crown winch (three key sockets) and the crown gate: the stairs up
--                      to the dam crest, and 80 m of crest to the caravan.
-- Water is a flat slab and a Lua rule (feet below the surface = the water
-- takes you back to the last checkpoint); currents are forge.player.push and
-- per-frame velocities on props.

local K = VAULT
forge.include("assets/scripts/spillway_dress.lua") -- SPILLWAY_DRESS(K)

if not (Missions and Interact) then error("spillway.lua needs missions.lua (Missions/Interact)") end
local Z = {} -- published as SPILLWAY at the END of the file (a half-loaded module stays off)

-- ---- Constants ----------------------------------------------------------------
local GY = 1.0   -- dry ground top
local WY = 0.0   -- water at rest
local FLOW = 2.4 -- the gorge current, m/s toward +z
local HOLD = 35.0 -- seconds the sluice stays open after the plate is left
local LV = { 0.0, -0.7, -1.4, -2.8 } -- water level for load 0, 1, 2, 3
local SLOT_X = 800.0
local SLOT_Z = { -7.7, -9.9, -12.1 } -- lodge slots, nearest the lip first
local MOODY_FAR = 300

local PLATE = vec3(771.0, GY, -2.0)  -- the counterweight (half 1.5)
local PED_W = vec3(774.0, 2.06, -26.0)
local PED_C = vec3(800.0, 2.06, -21.0)
local PED_E = vec3(837.0, 2.06, -17.0)
local MARK = vec3(822.0, 1.03, -25.5) -- where the prism turns the beam south
local EMIT = vec3(814.2, 1.35, -25.5)
local RECV = vec3(822.0, 1.35, -10.4) -- receiver centre; its north face is z = -10.55
local RECV_FACE = -10.55
local CART = vec3(406.2, 0.3, 4.5)    -- the cart in the Surface relay yard
local STATION = vec3(800.0, 1.15, 31.5) -- the rail cart on the Spillway plaza

-- Mutable state survives a hot reload (globals); resetGame rebuilds it.
local function freshMem()
    return {
        here = false, cartLit = nil, arrived = false,
        ride = { phase = "idle", t = 0, x = STATION.x - 12 },
        sl = { level = 0.0, load = 0, latched = false, hold = 0, beepT = 0, opened = 0, plateY = nil },
        lane = { lodged = {}, foamT = 0, lastN = 0 },
        tb = { on = false, conn = 0, omega = 0, spin = 0, connected = false },
        pinned = {}, crownOpen = false, crownT = 0, winT = nil, crank = 0,
        area = nil, visited = {}, hint = nil,
        noProg = {}, hintLv = {}, progSig = {},
        talk = nil, wrenYaw = -90, wrenAnim = "Idle",
        drownCool = 0, sink = {}, hallK = 0, hallApplied = -1, moodT = 0,
        flowPhase = 0, lampT = 0, splashT = 0,
    }
end
SPILLWAY_MEM = SPILLWAY_MEM or freshMem()
local function M() return SPILLWAY_MEM end
Z.mem = M

-- ---- Mood: dusk (the dark hall is a blend toward HALL) ----------------------------
local DUSK = {
    zenith = vec3(0.14, 0.13, 0.36), horizon = vec3(0.95, 0.50, 0.30), ground = vec3(0.22, 0.15, 0.14),
    sunSize = 3.2, sunInt = 3.6, halo = 0.55,
    fog = vec3(0.60, 0.40, 0.40), fogD = 0.0042, exposure = 0.88, bloom = 0.36,
    key = vec3(6.0, 3.5, 2.0), dir = vec3(-0.80, 0.20, 0.56),
    ambSky = vec3(0.34, 0.28, 0.46), ambGround = vec3(0.30, 0.20, 0.16),
    amb = 0.04, music = 0.14,
}
local HALL = { -- inside the turbine hall: almost nothing; SPARK's light and the beam are the hall
    fog = vec3(0.010, 0.012, 0.018), fogD = 0.014, key = vec3(0.05, 0.045, 0.05),
    ambSky = vec3(0.020, 0.022, 0.030), ambGround = vec3(0.014, 0.014, 0.020),
}
local function mix(a, b, t) return a + (b - a) * t end
local function vmix(a, b, t) return vec3(mix(a.x, b.x, t), mix(a.y, b.y, t), mix(a.z, b.z, t)) end

local function applySky()
    forge.render.set_sky({
        zenith = DUSK.zenith, horizon = DUSK.horizon, ground = DUSK.ground,
        sun_size = DUSK.sunSize, sun_intensity = DUSK.sunInt, sun_halo = DUSK.halo,
    })
    forge.render.set_exposure(DUSK.exposure)
    forge.render.set_bloom(DUSK.bloom)
    forge.render.set_view_distance(MOODY_FAR)
    if HUM.amb then forge.audio.set_volume(HUM.amb, DUSK.amb) end
    if HUM.music then forge.audio.set_volume(HUM.music, DUSK.music) end
end

local function applyLight(h)
    forge.render.set_fog(vmix(DUSK.fog, HALL.fog, h), mix(DUSK.fogD, HALL.fogD, h))
    forge.render.set_light(vmix(DUSK.key, HALL.key, h), DUSK.dir)
    forge.render.set_ambient(vmix(DUSK.ambSky, HALL.ambSky, h), vmix(DUSK.ambGround, HALL.ambGround, h), 1.0)
    M().hallApplied = h
end

local function applyMoodNow()
    applySky()
    applyLight(M().hallK)
end

local function inHall(p) return p.x > 813.0 and p.x < 839.0 and p.z < -6.5 and p.z > -28.0 and p.y < 8.0 end

-- ---- Data appended to the shared systems (include time, before the build) ----------
local CP = {} -- area -> checkpoint index (past the Surface's)
for _, c in ipairs({
    { "land", vec3(792.0, 1.95, 29.5) }, { "plaza", vec3(800.0, 1.95, 12.0) },
    { "w", vec3(776.0, 1.95, -4.4) }, { "c", vec3(800.0, 1.95, -3.2) },
    { "e", vec3(825.0, 1.95, -8.5) }, { "crest", vec3(762.0, 9.95, -26.0) },
}) do
    K.checkpoints[#K.checkpoints + 1] = { z = -1e9, pos = c[2], area = c[1] }
    CP[c[1]] = #K.checkpoints
end
Z.CP = CP
function Z.ownsCp(cp) return cp >= CP.land and cp <= CP.crest end

-- Movable props. Names start with "Spl " so nothing else claims them.
local CRATES = {
    { name = "Spl Crate W1", pos = vec3(766.8, 1.36, -4.8), area = "w" },
    { name = "Spl Crate W2", pos = vec3(766.8, 1.36, -3.2), area = "w" },
    { name = "Spl Crate W3", pos = vec3(766.8, 1.36, -1.6), area = "w" },
    { name = "Spl Crate C1", pos = vec3(796.5, 1.36, -4.4), area = "c" },
    { name = "Spl Crate C2", pos = vec3(800.0, 1.36, -4.4), area = "c" },
    { name = "Spl Crate C3", pos = vec3(803.5, 1.36, -4.4), area = "c" },
}
local KEYS = {
    { name = "Spl Key W", pos = PED_W, area = "w", glow = vec3(0.2, 1.5, 1.7), obj = "sluice" },
    { name = "Spl Key C", pos = PED_C, area = "c", glow = vec3(1.9, 1.2, 0.2), obj = "current" },
    { name = "Spl Key E", pos = PED_E, area = "e", glow = vec3(1.1, 0.3, 1.9), obj = "turbine" },
}
local PRISM = { name = "Spl Prism", pos = vec3(836.4, 1.36, -26.4), area = "e" }
Z.CRATES, Z.KEYS, Z.PRISM = CRATES, KEYS, PRISM
local PROPS = {} -- everything that can fall in the water, in one list
for _, c in ipairs(CRATES) do PROPS[#PROPS + 1] = c end
for _, c in ipairs(KEYS) do PROPS[#PROPS + 1] = c end
PROPS[#PROPS + 1] = PRISM
for _, c in ipairs(PROPS) do K.crates[#K.crates + 1] = { name = c.name, pos = c.pos } end
local function propRec(name)
    for _, c in ipairs(PROPS) do if c.name == name then return c end end
end
local function isCrate(name) return name:sub(1, 10) == "Spl Crate " end
local function isKey(name) return name:sub(1, 8) == "Spl Key " end

-- the crown winch: three seats, any key in any seat (the Surface's junction pattern)
local SEATS = {}
for i, z in ipairs({ -3.6, -2.4, -1.2 }) do
    local s = { id = "s_wk" .. i, name = "Spl Winch " .. i, seat = vec3(761.6, 2.15, z), accepts = isKey,
                grabby = true, magnet = 1.2, offTex = "metal_yellow",
                sfxOn = "assets/sounds/power_up.wav", sfxOff = "assets/sounds/power_down.wav" }
    K.sockets[#K.sockets + 1] = s
    SEATS[i] = s
end

-- three data cores (11 -> 14)
Z.coreIdx = {}
for _, c in ipairs({ vec3(778.4, -0.4, -16.0), vec3(761.3, 3.7, 34.5), vec3(815.6, 1.9, -27.2) }) do
    CORE_POS[#CORE_POS + 1] = c
    Z.coreIdx[#Z.coreIdx + 1] = #CORE_POS
end

-- the crown gate (kinematic: it carries nothing, but a lifted gate must never shove the player)
K.addDoor("Spl Crown", { closedPos = vec3(762.0, 2.6, -6.75), openPos = vec3(762.0, 8.6, -6.75),
    speed = 1.6, cond = function() return M().crownOpen == true end })
-- the cage in the turbine hall: lifts when the runner turns
K.addDoor("Spl Cage Door", { closedPos = vec3(835.0, 2.3, -17.0), openPos = vec3(835.0, 5.6, -17.0),
    speed = 2.0, offTex = "metal_red", cond = function() return M().tb.on == true end })

-- SPARK follows higher on the plaza and flies the hall
local prevCruise = K.ext.cruiseY
K.ext.cruiseY = function(p)
    if p.x > 600 then return inHall(p) and p.y + 2.2 or p.y + 3.0 end
    if prevCruise then return prevCruise(p) end
    return 4.2
end

-- ---- Build (once per process; geometry never re-spawns on reload) ---------------------
local count = 0
local MATTE = 0.9
local STRUCT_FILL = 0.08
local function blk(name, x0, x1, y0, y1, z0, z1, tex, tint, solid, mesh)
    local pos = vec3((x0 + x1) * 0.5, (y0 + y1) * 0.5, (z0 + z1) * 0.5)
    K.box(name, pos, vec3(x1 - x0, y1 - y0, z1 - z0), tex or "concrete", solid ~= false, false, mesh)
    if tint then
        forge.scene.setTint(name, tint)
        if (tex or "concrete") == "concrete" then
            forge.scene.setRoughness(name, MATTE)
        else
            forge.scene.setEmissive(name, tint * STRUCT_FILL)
        end
    end
    count = count + 1
end
local function prop(name, pos, scale, tex, solid, mesh, opts)
    K.box(name, pos, scale, tex, solid, false, mesh, opts)
    count = count + 1
end
local ROCK = vec3(0.70, 0.58, 0.50)
local ROCK2 = vec3(0.58, 0.48, 0.44)
local ROCK3 = vec3(0.48, 0.40, 0.38)
local CONC = vec3(0.74, 0.68, 0.64)
local CONC2 = vec3(0.60, 0.55, 0.54)
local WATER_TINT = vec3(0.05, 0.27, 0.30)
local WATER_GLOW = vec3(0.010, 0.085, 0.100)

local function waterSlab(name, x0, x1, z0, z1, y)
    local c = vec3((x0 + x1) * 0.5, y - 0.05, (z0 + z1) * 0.5)
    forge.scene.spawn(name, c, vec3(x1 - x0, 0.1, z1 - z0), vec3(0, 0, 0), "concrete", false)
    forge.scene.setTint(name, WATER_TINT)
    forge.scene.setEmissive(name, WATER_GLOW)
    forge.scene.setRoughness(name, 0.06)
    forge.scene.setMetallic(name, 0.25)
    count = count + 1
end

local function buildGround()
    -- plaza, canyon walls, the dam and its crest, the far terrace and the reservoir
    blk("Spl Plaza", 740, 860, -1.5, GY, -6, 60, "concrete", CONC)
    blk("Spl Cliff S", 736, 864, -4, 16, 36, 62, "concrete", ROCK2)
    blk("Spl Cliff W", 730, 760, -4, 14, -200, 62, "concrete", ROCK)
    blk("Spl Cliff E", 864, 890, -4, 14, -200, 62, "concrete", ROCK)
    blk("Spl Cliff E2", 840, 864, -4, 14, -26, 36, "concrete", ROCK)
    blk("Spl Cliff N", 730, 890, -4, 16, -200, -180, "concrete", ROCK3)
    blk("Spl Dam", 760, 840, -4, 9, -37, -28, "concrete", CONC2)
    blk("Spl Terrace", 840, 864, -4, 9, -37, -26, "concrete", ROCK)
    blk("Spl Parapet N", 760, 864, 9, 10.1, -37, -36.6, "concrete", CONC)
    blk("Spl Parapet S", 764, 840, 9, 10.8, -28.4, -28, "concrete", CONC) -- x 760..764 is the stairs' way onto the crest
    waterSlab("Spl Reservoir", 760, 864, -180, -37, 7.4)
    -- the crown stairs (x 760..764, rising north behind the crown gate) and their landing
    local rise = 8.0 / 27.0
    for i = 1, 27 do
        local z0 = -6.9 - (i - 1) * 0.6
        blk("Spl Stair " .. i, 760, 764, -4, GY + rise * i, z0 - 0.6, z0, "concrete", CONC)
    end
    blk("Spl Stair Landing", 760, 764, -4, 9, -28, -23.1, "concrete", CONC)
    blk("Spl Stair Foot", 760, 764, -4, GY, -6.9, -6.0, "concrete", CONC) -- under the crown gate
end

local function buildWest()
    local H = 5.6
    blk("Spl Chamber W", 764, 766, -4, 9.6, -28, -6, "concrete", CONC2)
    blk("Spl Chamber E", 782, 784, -4, H, -28, -6, "concrete", CONC2)
    blk("Spl Chamber Floor", 766, 782, -5, -4, -28, -6, "concrete", ROCK3)
    blk("Spl Chamber Shore S", 766, 782, -4, GY, -9, -6, "concrete", CONC)
    blk("Spl Chamber Shore N", 766, 782, -4, GY, -28, -23.9, "concrete", CONC)
    -- the stairs up out of the chamber: 11 steps from the last stone to the shore
    local r = (GY - (-2.2)) / 11
    for i = 1, 11 do
        local z0 = -20.6 - (i - 1) * 0.3
        blk("Spl Chamber Step " .. i, 772, 776, -4, -2.2 + r * i, z0 - 0.3, z0, "concrete", i % 2 == 0 and CONC or CONC2)
    end
    -- the stones: tops 0.3 / -0.5 / -1.3 / -2.2 (each needs one more notch of drain)
    for _, s in ipairs({ { "S0", 774.2, -10.8, 0.3 }, { "S1", 774.6, -13.4, -0.5 }, { "S2", 773.8, -16.0, -1.3 },
                         { "S3", 774.2, -18.6, -2.2 }, { "S2b", 778.4, -16.0, -1.3 } }) do
        blk("Spl Stone " .. s[1], s[2] - 0.9, s[2] + 0.9, -4, s[4], s[3] - 0.9, s[3] + 0.9, "concrete", ROCK)
    end
    waterSlab("Spl Water W", 766, 782, -23.9, -9, WY)
    -- the sluice gate at the dam end (a dark frame in the face: where the water goes)
    blk("Spl Sluice Frame", 770, 778, -3.5, 4.5, -27.9, -27.7, "metal_dark", nil, false)
    blk("Spl Sluice Slab", 771, 777, -3.5, 3.8, -27.7, -27.6, "metal", vec3(0.5, 0.45, 0.4), false)
    -- the counterweight: a frame, the plate, a gauge post with three lamps
    blk("Spl Plate Frame", PLATE.x - 1.7, PLATE.x + 1.7, 0.9, 1.04, PLATE.z - 1.7, PLATE.z + 1.7, "metal_dark")
    prop("Spl Plate W", vec3(PLATE.x, 1.07, PLATE.z), vec3(3.0, 0.06, 3.0), "metal_orange", true, "plate")
    blk("Spl Gauge Post", 774.8, 775.2, 1.0, 2.6, -4.0, -3.6, "metal_dark")
    for i = 1, 3 do
        prop("Spl Gauge " .. i, vec3(775.0, 1.7 + i * 0.4, -3.4), vec3(0.3, 0.3, 0.3), "metal_orange", false)
    end
    blk("Spl Key Ped W", PED_W.x - 0.8, PED_W.x + 0.8, 1.0, 1.7, PED_W.z - 0.8, PED_W.z + 0.8, "metal_dark")
end

local function buildCentre()
    blk("Spl Gorge W", 784, 794, -4, 5.6, -28, -6, "concrete", ROCK2)
    blk("Spl Gorge E", 806, 814, -4, 7.5, -28, -6, "concrete", ROCK2)
    blk("Spl Gorge Floor", 794, 806, -5, -4, -28, -6, "concrete", ROCK3)
    blk("Spl Gorge Platform", 794, 806, -4, GY, -28, -15.2, "concrete", CONC)
    waterSlab("Spl Water C", 794, 806, -15.2, -6.0, WY)
    -- the grate (visual: the lodging is script) and the culvert the water comes out of
    prop("Spl Rack", vec3(800, 0.28, -6.65), vec3(12, 1.4, 0.2), "grate", false)
    prop("Spl Culvert", vec3(800, 0.5, -15.14), vec3(5.0, 1.0, 0.12), "metal_dark", false)
    prop("Spl Outflow", vec3(800, 0.03, -14.7), vec3(5.0, 0.04, 1.2), "metal_white", false)
    forge.scene.setEmissive("Spl Outflow", vec3(0.7, 1.1, 1.5))
    -- flow marks: slide with the current, so the lane reads without a word
    for i = 1, 6 do
        local x = 795.8 + ((i - 1) % 3) * 4.2
        prop("Spl Flow " .. i, vec3(x, 0.03, -14), vec3(0.35, 0.03, 1.5), "metal_white", false)
        forge.scene.setEmissive("Spl Flow " .. i, vec3(0.55, 0.9, 1.2))
    end
    blk("Spl Lip Mark", 794, 806, GY - 0.01, GY + 0.01, -6.4, -6.0, "hazard", nil, false)
    blk("Spl Key Ped C", PED_C.x - 0.8, PED_C.x + 0.8, 1.0, 1.7, PED_C.z - 0.8, PED_C.z + 0.8, "metal_dark")
end

local function buildEast()
    local H = 7.0
    -- floor ring around the bay (bay x 818..834, z -23..-11), the bay floor, walls, roof
    blk("Spl Hall Ring N", 814, 838, -4, GY, -28, -23, "concrete", CONC2)
    blk("Spl Hall Ring S", 814, 838, -4, GY, -11, -6, "concrete", CONC2)
    blk("Spl Hall Ring W", 814, 818, -4, GY, -23, -11, "concrete", CONC2)
    blk("Spl Hall Ring E", 834, 838, -4, GY, -23, -11, "concrete", CONC2)
    blk("Spl Bay Floor", 818, 834, -5, -4, -23, -11, "concrete", ROCK3)
    blk("Spl Hall Wall E", 838, 840, -4, H, -28, -6, "concrete", CONC2)
    blk("Spl Hall Front 1", 814, 823, GY, H, -7, -6, "concrete", CONC2)
    blk("Spl Hall Front 2", 827, 838, GY, H, -7, -6, "concrete", CONC2)
    blk("Spl Hall Lintel", 823, 827, 4.2, H, -7, -6, "concrete", CONC2)
    blk("Spl Hall Roof", 814, 840, H, H + 0.5, -28, -6, "metal_dark")
    waterSlab("Spl Water E", 818, 834, -23, -11, WY)
    -- the cage (E ring): grate walls, a roof, the lifting door (kit door "Spl Cage Door")
    blk("Spl Cage N", 835.15, 838, GY, 3.4, -19, -18.85, "grate")
    blk("Spl Cage S", 835.15, 838, GY, 3.4, -15.15, -15, "grate")
    blk("Spl Cage Roof", 835.15, 838, 3.4, 3.55, -19, -15, "metal_dark")
    prop("Spl Cage Door", vec3(835.0, 2.3, -17.0), vec3(0.15, 2.6, 3.7), "metal_red", true, nil, { kinematic = true })
    blk("Spl Key Ped E", PED_E.x - 0.5, PED_E.x + 0.5, 1.0, 1.7, PED_E.z - 0.5, PED_E.z + 0.5, "metal_dark")
    -- the beam: emitter on the west wall, a marker for the prism, the receiver on a post
    prop("Spl Emitter", EMIT, vec3(0.3, 0.3, 0.3), "laser", true, "emitter")
    prop("Spl Beam A", vec3(826.0, EMIT.y, EMIT.z), vec3(0.001, 0.001, 0.001), "laser", false)
    prop("Spl Beam B", vec3(MARK.x, EMIT.y, -18.0), vec3(0.001, 0.001, 0.001), "laser", false)
    prop("Spl Marker", MARK, vec3(1.0, 0.06, 1.0), "metal_violet", false)
    forge.scene.setEmissive("Spl Marker", vec3(0.10, 0.03, 0.20)) -- only just there in the dark
    blk("Spl Recv Post", RECV.x - 0.2, RECV.x + 0.2, 1.0, 1.2, RECV.z - 0.2, RECV.z + 0.2, "metal_dark")
    prop("Spl Receiver", RECV, vec3(0.3, 0.3, 0.3), "laser", true, "receiver")
    forge.scene.setRotation("Spl Receiver", vec3(0, -90, 0))
    -- the prism waits in the north-east corner, dark until SPARK's light finds it
    forge.scene.spawn(PRISM.name, PRISM.pos, vec3(0.7, 0.7, 0.7), vec3(0.35, 0.35, 0.35), "metal_violet", true,
                      "prism", { lockRotation = true })
    forge.scene.setEmissive(PRISM.name, vec3(0.05, 0.015, 0.10))
    count = count + 1
    -- the doorway's amber lamp (a fixture the player can read the hall by from the plaza)
    prop("Spl Hall Lamp", vec3(825.0, 3.9, -5.7), vec3(0.4, 0.4, 0.2), "metal_yellow", false, "lamp")
    forge.scene.setEmissive("Spl Hall Lamp", vec3(2.0, 1.4, 0.4))
end

local function buildCrown()
    -- the gate at the stairs' foot, its posts, and the winch beside it
    prop("Spl Crown", vec3(762.0, 2.6, -6.75), vec3(4.0, 3.2, 0.3), "metal_red", true, nil, { kinematic = true })
    blk("Spl Crown Post W", 759.7, 760.3, GY, 5.2, -7.0, -6.5, "metal_dark")
    blk("Spl Crown Post E", 763.7, 764.3, GY, 5.2, -7.0, -6.5, "metal_dark")
    blk("Spl Crown Lintel", 759.7, 764.3, 5.2, 5.6, -7.0, -6.5, "metal_dark")
    prop("Spl Crown Lamp", vec3(762.0, 5.0, -6.4), vec3(0.4, 0.3, 0.2), "metal_red", false, "lamp")
    forge.scene.setEmissive("Spl Crown Lamp", vec3(1.8, 0.1, 0.05))
    blk("Spl Winch Base", 760.2, 763.0, GY, 1.8, -4.4, -0.4, "metal_dark")
    prop("Spl Winch Drum", vec3(760.7, 2.5, -2.4), vec3(0.5, 0.5, 3.4), "metal_orange", false)
    for _, s in ipairs(SEATS) do
        prop(s.name, vec3(s.seat.x, s.seat.y + 0.1, s.seat.z), vec3(1.0, 0.9, 1.0), "metal_yellow", false, "socket")
    end
end

local function buildLanding()
    -- the rail cart and its rails (the Surface-side cart is built in buildYard)
    blk("Spl Rail A", 762, 806, GY - 0.02, GY + 0.04, 30.4, 30.6, "metal_dark", nil, false)
    blk("Spl Rail B", 762, 806, GY - 0.02, GY + 0.04, 32.4, 32.6, "metal_dark", nil, false)
    blk("Spl Buffer", 806.6, 807.4, GY, 2.0, 30.0, 33.0, "metal_dark")
    prop("Spl Car", vec3(STATION.x, STATION.y, STATION.z), vec3(3.4, 0.3, 2.6), "metal_yellow", true, nil,
         { kinematic = true })
    forge.scene.setEmissive("Spl Car", K.glow.metal_yellow)
    blk("Spl Station Post", 806.6, 807.0, GY, 4.2, 29.6, 30.0, "metal_dark")
    prop("Spl Station Sign", vec3(806.8, 4.0, 30.3), vec3(0.2, 0.6, 1.6), "metal_yellow", false)
    forge.scene.setEmissive("Spl Station Sign", vec3(1.4, 1.0, 0.15))
    -- a stepped hide for the first data core (jump 0.9, then 0.9)
    blk("Spl Hide Step 1", 762, 766, -1.5, 1.9, 32.5, 35.5, "concrete", ROCK)
    blk("Spl Hide Step 2", 760, 762.6, -1.5, 2.8, 33.0, 36.0, "concrete", ROCK2)
end

-- the Surface end of the ride: a dead rail cart in the relay yard, lit once the signal is sent
local function buildYard()
    blk("Spl Cart Pad", 403.4, 408.6, 0, 0.12, 3.0, 6.0, "metal_dark", nil, true)
    blk("Spl Cart Rail A", 402.0, 408.6, 0.1, 0.16, 3.5, 3.65, "metal", nil, false)
    blk("Spl Cart Rail B", 402.0, 408.6, 0.1, 0.16, 5.35, 5.5, "metal", nil, false)
    prop("Spl Cart", vec3(CART.x, 0.30, CART.z), vec3(2.6, 0.3, 2.0), "metal_yellow", true, nil, { kinematic = true })
    forge.scene.setEmissive("Spl Cart", K.glow.metal_yellow)
    blk("Spl Cart Post", 408.2, 408.6, 0, 2.6, 2.9, 3.3, "metal_dark")
    prop("Spl Cart Lamp", vec3(408.4, 2.7, 3.1), vec3(0.4, 0.4, 0.4), "metal_red", false, "lamp")
    forge.scene.setEmissive("Spl Cart Lamp", vec3(1.8, 0.1, 0.05))
end

local function spawnMovables()
    for _, c in ipairs(CRATES) do
        K.box(c.name, c.pos, vec3(0.7, 0.7, 0.7), "crate", true, true, "crate")
        count = count + 1
    end
    for _, k in ipairs(KEYS) do
        forge.scene.spawn(k.name, k.pos, vec3(0.5, 0.7, 0.5), vec3(0.25, 0.35, 0.25), "metal_yellow", true,
                          "battery", { lockRotation = true })
        forge.scene.setEmissive(k.name, k.glow)
        count = count + 1
    end
    -- the caravan's keeper (a second Wren at the end of the crest)
    local model = forge.scene.has_model("wren_bot") and "wren_bot" or "knight"
    forge.scene.spawn("Wren C", vec3(851.5, 9.0, -31.5), vec3(1, 1, 1), vec3(0, 0, 0), "", false, model)
    forge.scene.spawn("Wren C Body", vec3(851.5, 9.9, -31.5), vec3(0.001, 0.001, 0.001), vec3(0.35, 0.9, 0.35),
                      "metal", false)
    forge.anim.play("Wren C", "Idle", { fade = 0 })
    count = count + 2
end

local function build()
    if SPILLWAY_BUILT then return end
    SPILLWAY_BUILT = true
    buildGround()
    buildWest()
    buildCentre()
    buildEast()
    buildCrown()
    buildLanding()
    buildYard()
    spawnMovables()
    if SPILLWAY_DRESS then count = count + SPILLWAY_DRESS(K) end
    SPILLWAY_ENTITIES = count + 3 -- + the cores spawned by buildRooms
    forge.log("spillway built: " .. SPILLWAY_ENTITIES .. " entities")
end

-- ---- Small helpers -------------------------------------------------------------------
local function held(name)
    return forge.player.holding() == name or K.drone.carrying == name
end
local function inRect(p, x0, x1, z0, z1) return p.x >= x0 and p.x <= x1 and p.z >= z0 and p.z <= z1 end
local function feetY(pp) return pp.y - 0.9 end
local function dist2(a, b) local dx, dy, dz = a.x - b.x, a.y - b.y, a.z - b.z return dx * dx + dy * dy + dz * dz end
local function say(line, secs) forge.hud.say("WREN", line, secs or 5.0) end
local function progress(area) -- anything the player did in `area`: the hint clock restarts
    local m = M()
    m.noProg[area] = 0
end

-- ---- Water -----------------------------------------------------------------------------
-- A body: x/z rectangle, a level function, `lane` = the current runs through it.
local WATERS = {
    { id = "w", x0 = 766, x1 = 782, z0 = -23.9, z1 = -9.0, level = function() return M().sl.level end },
    { id = "c", x0 = 794, x1 = 806, z0 = -15.2, z1 = -6.0, level = function() return WY end, lane = true },
    { id = "e", x0 = 818, x1 = 834, z0 = -23.0, z1 = -11.0, level = function() return WY end },
}
local function waterAt(p) -- the body whose rectangle holds p, or nil
    for _, w in ipairs(WATERS) do
        if inRect(p, w.x0, w.x1, w.z0, w.z1) then return w end
    end
    return nil
end
Z.WATERS = WATERS

local function splash(at, big)
    forge.audio.play_at("assets/sounds/splash.wav", at, big and 1.0 or 0.7, big and 0.9 or 1.15)
    forge.fx.burst(at, big and 36 or 16, vec3(1.4, 2.0, 2.6))
end

local function setLevel(name, y) -- the slab's top surface at y
    local at = forge.scene.getPosition(name)
    forge.scene.setPosition(name, vec3(at.x, y - 0.05, at.z))
end

local function drown(pp, w)
    local m = M()
    if m.drownCool > 0 then return end
    m.drownCool = 1.2
    splash(vec3(pp.x, w.level(), pp.z), true)
    forge.camera.shake(0.5)
    forge.hud.toast("The water takes you.", 2.0)
    forge.log("spillway: DROWN in " .. w.id .. " at " .. string.format("%.1f %.1f %.1f", pp.x, pp.y, pp.z))
    K.respawn()
end

-- props in a pool (not the gorge's crates, which float): under the surface for 0.6 s = home
local function updateProps(dt)
    local m = M()
    for _, c in ipairs(PROPS) do
        local at = forge.scene.getPosition(c.name)
        local w = waterAt(at)
        local sunk = false
        if w and not held(c.name) and not (w.lane and isCrate(c.name)) then
            sunk = at.y < w.level() - 0.2 and at.y > -4.6
        end
        if sunk then
            m.sink[c.name] = (m.sink[c.name] or 0) + dt
            if m.sink[c.name] > 0.6 then
                m.sink[c.name] = nil
                splash(vec3(at.x, w.level(), at.z), false)
                forge.log("spillway: " .. c.name .. " sank - sent home")
                K.sendHome(c)
            end
        else
            m.sink[c.name] = nil
        end
    end
end

-- ---- THE CURRENT: the gorge's lane, crates that float and lodge ------------------------
local LANE = WATERS[2]
local function laneHolds(p) return inRect(p, LANE.x0, LANE.x1, LANE.z0, LANE.z1) end

local function lodgedCount()
    local n = 0
    for _ in pairs(M().lane.lodged) do n = n + 1 end
    return n
end
Z.lodgedCount = lodgedCount
local function slotFree(i)
    for _, s in pairs(M().lane.lodged) do if s == i then return false end end
    return true
end

local function updateLane(dt, pp)
    local m = M()
    local lane = m.lane
    -- the player is pushed in the lane: hard in the air, a nudge while standing on a lodged crate
    if laneHolds(pp) and pp.y < 3.2 then
        local standing = forge.player.grounded()
        forge.player.push(vec3(0, 0, standing and 0.7 or FLOW))
    end
    for _, c in ipairs(CRATES) do
        local name = c.name
        local at = forge.scene.getPosition(name)
        local id = forge.scene.bodyId(name)
        local slot = lane.lodged[name]
        if slot then
            -- a lodged crate stays put unless the glove (or SPARK) takes it, or it is knocked far off
            local tz = SLOT_Z[slot]
            local dx, dz = at.x - SLOT_X, at.z - tz
            if held(name) or dx * dx + dz * dz > 1.0 then
                lane.lodged[name] = nil
                forge.log("lane: " .. name .. " left slot " .. slot)
            else
                forge.scene.setPosition(name, vec3(SLOT_X, 0.0, tz))
                if id ~= 0 then forge.physics.set_velocity(id, vec3(0, 0, 0)) end
            end
        elseif laneHolds(at) and at.y < 0.9 and at.y > -3.0 and not held(name) and id ~= 0 then
            -- float: hold the crate half-submerged, carry it with the current, funnel it to the middle
            local v = forge.physics.velocity(id)
            local vy = math.max(-2.0, math.min(2.0, (0.0 - at.y) * 6.0))
            local vz = v.z + (FLOW - v.z) * math.min(1.0, 3.0 * dt)
            local target
            for i = 1, 3 do if slotFree(i) then target = i break end end
            -- toward the middle to lodge; with every slot taken, round the row (a crate slips past) and over the drop
            local tx = target and SLOT_X or (at.x < SLOT_X and 796.4 or 803.6)
            if target then -- a free slot nearer the lip than a lodged crate: go round the row first
                for j = 1, 3 do
                    if not slotFree(j) and SLOT_Z[j] > at.z - 0.6 and SLOT_Z[j] < SLOT_Z[target] + 0.6 then
                        tx = at.x < SLOT_X and 796.4 or 803.6
                    end
                end
            end
            local vx = (tx - at.x) * 0.9
            forge.physics.set_velocity(id, vec3(vx, vy, vz))
            -- lodge in the first free slot (nearest the lip first)
            if target and at.z >= SLOT_Z[target] - 0.3 then
                lane.lodged[name] = target
                forge.scene.setPosition(name, vec3(SLOT_X, 0.0, SLOT_Z[target]))
                forge.physics.set_velocity(id, vec3(0, 0, 0))
                forge.audio.play_at("assets/sounds/thunk.wav", at, 0.9, 0.9)
                splash(vec3(at.x, 0.0, at.z), false)
                progress("c")
                forge.log("lane: " .. name .. " lodged in slot " .. target)
            elseif at.z > -6.9 then -- no slot free, or it slipped through: over the drop
                forge.audio.play_at("assets/sounds/splash.wav", at, 0.9, 0.7)
                forge.fx.burst(at, 24, vec3(1.4, 2.0, 2.6))
                forge.log("lane: " .. name .. " went over the drop - sent home")
                K.sendHome(c)
            end
        end
    end
    -- foam: white-blue bursts along the lane, rate-limited, only within earshot
    lane.foamT = lane.foamT - dt
    if lane.foamT <= 0 and dist2(pp, vec3(800, 0, -10)) < 55 * 55 then
        lane.foamT = 0.22
        local x = 795.0 + math.random() * 10.0
        forge.fx.burst(vec3(x, 0.15, -14.2 + math.random() * 6.5), 3, vec3(1.3, 1.9, 2.4))
        forge.fx.burst(vec3(800 + (math.random() - 0.5) * 4.0, 0.2, -14.8), 4, vec3(1.6, 2.2, 2.8)) -- the outflow
    end
    -- the flow marks slide with the current
    m.flowPhase = (m.flowPhase + FLOW * 0.9 * dt) % 9.0
    for i = 1, 6 do
        local z = -14.6 + ((m.flowPhase + (i - 1) * 1.5) % 9.0)
        forge.scene.setPosition("Spl Flow " .. i, vec3(795.8 + ((i - 1) % 3) * 4.2, 0.03, math.min(z, -6.9)))
    end
    if lane.lastN ~= lodgedCount() then lane.lastN = lodgedCount() end
end

-- ---- THE SLUICE: a counterweight in proportion to load ------------------------------
local function plateLoad(pp)
    local n = 0
    for _, c in ipairs(CRATES) do
        if c.area == "w" and not held(c.name) then
            local at = forge.scene.getPosition(c.name)
            if math.abs(at.x - PLATE.x) < 1.6 and math.abs(at.z - PLATE.z) < 1.6 and at.y > 1.0 and at.y < 2.6 then
                n = n + 1
            end
        end
    end
    if math.abs(pp.x - PLATE.x) < 1.6 and math.abs(pp.z - PLATE.z) < 1.6 and feetY(pp) > 0.8 and feetY(pp) < 1.8 then
        n = n + 1 -- the player is one weight
    end
    return math.min(n, 3)
end
Z.plateLoad = plateLoad

local function updateSluice(dt, pp)
    local m = M()
    local sl = m.sl
    local load = plateLoad(pp)
    if load ~= sl.load then
        sl.load = load
        forge.audio.play_at("assets/sounds/clunk.wav", PLATE, 0.8, 0.8 + 0.15 * load)
        forge.log("sluice: load " .. load)
        progress("w")
    end
    if load >= 3 then
        if not sl.latched then
            forge.hud.toast("THE SLUICE IS OPEN", 3.0)
            forge.audio.play("assets/sounds/power_up.wav", 0.6, 0.7)
            forge.log("sluice: latched open")
        end
        sl.latched, sl.hold = true, HOLD
    elseif sl.latched then
        sl.hold = sl.hold - dt
        if sl.hold <= 10.0 and sl.hold > 0 then -- the chain is running out: a beep a second, then the water comes back
            sl.beepT = sl.beepT - dt
            if sl.beepT <= 0 then
                sl.beepT = 1.0
                forge.audio.play_at("assets/sounds/chirp_sad.wav", PLATE, 0.8, 1.0)
                if sl.hold <= 10.0 and sl.hold + 1.0 > 10.0 then forge.hud.toast("THE SLUICE IS CLOSING", 2.5) end
            end
        end
        if sl.hold <= 0 then
            sl.latched = false
            forge.log("sluice: closing")
        end
    end
    local L = sl.latched and 3 or load
    local target = LV[L + 1]
    if sl.level > target then
        sl.level = math.max(sl.level - 0.5 * dt, target)
    elseif sl.level < target then
        sl.level = math.min(sl.level + 0.35 * dt, target)
    end
    if math.abs(sl.level - (sl.shown or 99)) > 0.002 then
        sl.shown = sl.level
        setLevel("Spl Water W", sl.level)
        if forge.audio.set_volume and HUM.spl_sluice then forge.audio.set_volume(HUM.spl_sluice, 0.28) end
    elseif HUM.spl_sluice then
        forge.audio.set_volume(HUM.spl_sluice, 0.0)
    end
    -- the plate dips a hair per weight; the gauge lamps show the load (flashing when the chain runs out)
    local py = 1.07 - 0.02 * load
    if py ~= sl.plateY then
        sl.plateY = py
        forge.scene.setPosition("Spl Plate W", vec3(PLATE.x, py, PLATE.z))
        forge.scene.setTexture("Spl Plate W", load >= 3 and "metal_green" or "metal_orange")
    end
    m.lampT = m.lampT + dt
    local flash = sl.latched and load < 3 and sl.hold < 10.0 and (math.floor(m.lampT * 4) % 2 == 0)
    sl.lamps = sl.lamps or {}
    for i = 1, 3 do
        local on = i <= (sl.latched and 3 or load) and not flash
        if sl.lamps[i] ~= on then
            sl.lamps[i] = on
            forge.scene.setTexture("Spl Gauge " .. i, on and "metal_green" or "metal_orange")
            forge.scene.setEmissive("Spl Gauge " .. i, on and vec3(0.15, 1.4, 0.3) or vec3(0.25, 0.10, 0.02))
        end
    end
end

-- ---- THE TURBINE HALL: SPARK's light, the beam, the runner, the cage ---------------------
local function turbineOn(quiet)
    local tb = M().tb
    if tb.on then return end
    tb.on = true
    forge.scene.setTexture("Spl Receiver", "metal_green")
    forge.scene.setTexture("Spl Marker", "metal_green")
    forge.scene.setEmissive("Spl Marker", vec3(0.10, 0.9, 0.25))
    forge.render.light("spl_hall1", vec3(816.5, 5.2, -17.0), vec3(2.0, 1.7, 1.1), 14.0)
    forge.render.light("spl_hall2", vec3(826.0, 5.2, -25.0), vec3(2.0, 1.7, 1.1), 14.0)
    forge.render.light("spl_hall3", vec3(832.0, 5.2, -9.0), vec3(2.0, 1.7, 1.1), 14.0)
    forge.log("turbine: RUNNING (the beam reaches the receiver)")
    if not quiet then
        forge.audio.play("assets/sounds/turbine_start.wav", 1.0)
        forge.audio.play("assets/sounds/secret.wav", 0.7)
        forge.fx.burst(vec3(828, 1.5, -17), 40, vec3(0.4, 2.4, 0.8))
        forge.camera.shake(0.35)
        forge.hud.toast("THE TURBINE TURNS", 3.0)
        progress("e")
    end
end

local function updateTurbine(dt, pp)
    local m = M()
    local tb = m.tb
    -- SPARK's lamp: only inside the hall, wherever SPARK is
    local dp = forge.scene.getPosition(K.drone.name)
    m.sparkLit = inHall(dp)
    if m.sparkLit then
        forge.render.light("spl_spark", dp, vec3(2.7, 2.4, 1.7), 9.0)
    else
        forge.render.clear_light("spl_spark")
    end
    -- the beam: emitter -> (prism) -> receiver. The same raycast scheme as R17's reactor.
    local pat = forge.scene.getPosition(PRISM.name)
    local tr = K.crateTrack[PRISM.name]
    local dx, dz = pat.x - MARK.x, pat.z - MARK.z
    local dh = math.sqrt(dx * dx + dz * dz)
    if not held(PRISM.name) and tr and tr.speed < 1.5 and dh < 0.7 and dh > 0.005 then
        pat = K.moveTowards(pat, vec3(MARK.x, pat.y, MARK.z), 1.5 * dt) -- forgiveness: ease onto the marker
        forge.scene.setPosition(PRISM.name, pat)
        local pid = forge.scene.bodyId(PRISM.name)
        if pid ~= 0 then forge.physics.set_velocity(pid, vec3(0, 0, 0)) end
    end
    local o = vec3(EMIT.x + 0.3, EMIT.y, EMIT.z)
    local hid, hd = forge.physics.raycast(o, vec3(1, 0, 0), 30.0)
    hd = hd or 30.0
    local onPrism = hid ~= nil and forge.scene.bodyName(hid) == PRISM.name
    forge.scene.setPosition("Spl Beam A", vec3(o.x + hd * 0.5, o.y, o.z))
    forge.scene.setScale("Spl Beam A", vec3(hd, 0.05, 0.05))
    local connected = false
    if onPrism then
        local o2 = vec3(pat.x, EMIT.y, pat.z + 0.45)
        local _, d2 = forge.physics.raycast(o2, vec3(0, 0, 1), 20.0)
        d2 = d2 or 20.0
        forge.scene.setPosition("Spl Beam B", vec3(o2.x, o2.y, o2.z + d2 * 0.5))
        forge.scene.setScale("Spl Beam B", vec3(0.05, 0.05, d2))
        local reach = RECV_FACE - o2.z
        connected = math.abs(pat.x - MARK.x) <= 0.2 and d2 >= reach - 0.12
        if onPrism ~= tb.prismHit then progress("e") end
    else
        forge.scene.setScale("Spl Beam B", vec3(0.001, 0.001, 0.001))
    end
    tb.prismHit = onPrism
    if connected ~= tb.connected then
        tb.connected = connected
        forge.audio.play_at(connected and "assets/sounds/prism.wav" or "assets/sounds/chirp_sad.wav", RECV, 1.0, 1.0)
        forge.fx.burst(RECV, 14, connected and vec3(0.3, 2.0, 0.6) or vec3(2.0, 0.4, 0.3))
        forge.log("turbine: beam " .. (connected and "CONNECTED" or "DISCONNECTED"))
        progress("e")
    end
    -- the runner restarts after the beam has held for 0.8 s (and stays running)
    if not tb.on then
        tb.conn = connected and (tb.conn + dt) or 0
        if tb.conn >= 0.8 then turbineOn(false) end
    end
    tb.omega = tb.on and math.min(tb.omega + 40.0 * dt, 110.0) or 0
    if tb.omega > 0 then
        tb.spin = (tb.spin + tb.omega * dt) % 360
        forge.scene.setRotation("Spl Turbine", vec3(0, tb.spin, 0))
    end
    -- the bay churns once the runner turns (rate-limited)
    if tb.on and dist2(pp, vec3(826, 1, -17)) < 45 * 45 then
        tb.foamT = (tb.foamT or 0) - dt
        if tb.foamT <= 0 then
            tb.foamT = 0.35
            local a = math.random() * 6.283
            forge.fx.burst(vec3(828 + math.cos(a) * 3.0, 0.1, -17 + math.sin(a) * 3.0), 3, vec3(1.3, 1.9, 2.4))
        end
    end
end

-- ---- Keys, the winch, the crown gate ---------------------------------------------------
local function keyAtSeat(name)
    local at = forge.scene.getPosition(name)
    for _, s in ipairs(SEATS) do
        local dx, dy, dz = at.x - s.seat.x, at.y - s.seat.y, at.z - s.seat.z
        if dx * dx + dz * dz < 0.12 and math.abs(dy) < 0.35 and K.state.sockets[s.id] == true then return s end
    end
    return nil
end
local function seatedCount()
    local n = 0
    for _ in pairs(M().pinned) do n = n + 1 end
    return n
end
Z.seatedCount = seatedCount

local function crownUp(quiet)
    local m = M()
    if m.crownOpen then return end
    m.crownOpen = true
    forge.scene.setTexture("Spl Crown", "metal_green")
    forge.scene.setEmissive("Spl Crown Lamp", vec3(0.2, 1.6, 0.3))
    forge.log("crown: the gate lifts")
    if not quiet then
        m.crownT = 4.0
        forge.audio.play("assets/sounds/power_up.wav", 1.0, 0.8)
        forge.audio.play("assets/sounds/gong.wav", 0.8, 0.7)
        forge.audio.play("assets/sounds/secret.wav", 0.8)
        forge.camera.shake(0.3)
        forge.hud.toast("THE CROWN GATE LIFTS", 4.0)
        say("The winch is turning! Go on - across the crest, to the caravan.", 6.0)
    end
end

local function pinKey(name, quiet)
    local m = M()
    if m.pinned[name] then return end
    local s = keyAtSeat(name)
    if not s then -- restored from a save: first free seat
        for _, cand in ipairs(SEATS) do
            local taken = false
            for _, sid in pairs(m.pinned) do if sid == cand.id then taken = true end end
            if not taken then s = cand break end
        end
    end
    if not s then return end
    m.pinned[name] = s.id
    forge.scene.setPosition(name, s.seat)
    if not quiet then
        forge.fx.burst(s.seat, 18, vec3(2.0, 1.6, 0.3))
        forge.audio.play_at("assets/sounds/clunk.wav", s.seat, 1.0, 0.8)
    end
    forge.log("winch: " .. name .. " locked into " .. s.id)
    if seatedCount() >= 3 then crownUp(quiet) end
end

local function seatOf(id)
    for _, s in ipairs(SEATS) do if s.id == id then return s end end
end

local function updatePins(dt)
    local m = M()
    for name, sid in pairs(m.pinned) do
        local s = seatOf(sid)
        if forge.player.holding() == name then
            forge.player.drop()
            forge.audio.play("assets/sounds/beep_no.wav", 0.7)
            forge.hud.toast("The winch has locked that key in.", 2.0)
        end
        local at = forge.scene.getPosition(name)
        if dist2(at, s.seat) > 0.0004 then
            forge.scene.setPosition(name, s.seat)
            local id = forge.scene.bodyId(name)
            if id ~= 0 then forge.physics.set_velocity(id, vec3(0, 0, 0)) end
        end
    end
    -- the drum turns while the gate rises
    if m.crownT > 0 then
        m.crownT = m.crownT - dt
        m.crank = (m.crank + 140.0 * dt) % 360
        forge.scene.setRotation("Spl Winch Drum", vec3(0, 0, m.crank))
    end
end

-- ---- The ride (Surface relay yard -> the Spillway landing) -------------------------------
local function setCartLamp(on)
    forge.scene.setTexture("Spl Cart Lamp", on and "metal_green" or "metal_red")
    forge.scene.setEmissive("Spl Cart Lamp", on and vec3(0.2, 1.8, 0.3) or vec3(1.8, 0.1, 0.05))
end

local function updateRide(dt, pp)
    local m = M()
    local r = m.ride
    if r.phase == "idle" then return end
    if r.phase == "fade" then
        r.t = r.t + dt
        forge.hud.set_fade(math.min(r.t / 0.9, 1.0))
        if r.t >= 1.0 then
            r.x = STATION.x - 12
            forge.scene.setPosition("Spl Car", vec3(r.x, STATION.y, STATION.z))
            forge.player.teleport(vec3(r.x, 2.3, STATION.z))
            forge.player.set_yaw(0)
            local dr = K.drone
            dr.station, dr.parkedAt, dr.carrying, dr.deposit = nil, nil, nil, nil
            forge.scene.setPosition(dr.name, vec3(r.x + 1.0, 4.5, STATION.z - 1.0))
            m.here = true
            m.hallK = 0
            applyMoodNow()
            r.phase, r.t = "arrive", 0
            forge.log("rail cart: arrived at the Spillway")
        end
    elseif r.phase == "arrive" then
        r.t = r.t + dt
        forge.hud.set_fade(math.max(1.0 - r.t / 0.9, 0.0))
        r.x = math.min(r.x + 3.2 * dt, STATION.x)
        forge.scene.setPosition("Spl Car", vec3(r.x, STATION.y, STATION.z))
        if r.x >= STATION.x and r.t >= 0.9 then
            r.phase = "done"
            m.arrived = true
            forge.hud.set_fade(0)
            forge.audio.play("assets/sounds/clunk.wav", 0.8)
            forge.audio.play("assets/sounds/chime.wav", 0.8)
            m.visited.land = true
            K.setCp(CP.land, true)
            forge.hud.toast("THE SPILLWAY", 3.0)
        end
    end
end

-- ---- Wren at the caravan, the win ----------------------------------------------------------
local function wrenPlay(clip, loop)
    local m = M()
    if forge.anim.play("Wren C", clip, { loop = loop, fade = 0.25 }) then m.wrenAnim = clip end
end

local function talkNext()
    local m = M()
    local tk = m.talk
    if not tk then return end
    tk.i = tk.i + 1
    local line = tk.lines[tk.i]
    if not line then
        m.talk = nil
        return
    end
    tk.t = 2.0 + #line * 0.045
    forge.hud.say("WREN", line, tk.t)
    forge.log("wren: " .. line)
end

local function updateWren(dt, pp)
    local m = M()
    local W = vec3(851.5, 9.0, -31.5)
    local dx, dz = pp.x - W.x, pp.z - W.z
    local want = (dx * dx + dz * dz < 144) and math.deg((math.atan2 or math.atan)(dx, dz)) or -90
    local turn = (want - m.wrenYaw + 180) % 360 - 180
    local step = 150 * dt
    if turn > step then turn = step elseif turn < -step then turn = -step end
    m.wrenYaw = m.wrenYaw + turn
    forge.scene.setRotation("Wren C", vec3(0, m.wrenYaw, 0))
    if m.wrenAnim ~= "Idle" then
        local t, len = forge.anim.time("Wren C")
        if t and len and t >= len - 0.3 then wrenPlay("Idle", true) end
    end
    local tk = m.talk
    if tk then
        tk.t = tk.t - dt
        if tk.t <= 0 then talkNext() end
    end
end

-- ---- The mission ------------------------------------------------------------------------------
local function spillwayCores()
    local n = 0
    for _, i in ipairs(Z.coreIdx) do if K.cores.got[i] then n = n + 1 end end
    return n
end
local function keyObjective(id, name, text)
    return { id = id, text = text, done = function() return keyAtSeat(name) ~= nil end }
end
Missions.define({
    id = "crossing", title = "CROSSING", sub = "reach the caravan", ordered = false,
    objectives = {
        keyObjective("sluice", "Spl Key W", "Key: the sluice  (west)"),
        keyObjective("current", "Spl Key C", "Key: the current  (centre)"),
        keyObjective("turbine", "Spl Key E", "Key: the turbine hall  (east)"),
        { id = "crown", text = "Seat the keys: the crown winch", done = function() return M().crownOpen == true end },
        { id = "caravan", text = "Cross the dam to the caravan",
          hidden = function() return not M().crownOpen end },
        { id = "cores", optional = true,
          text = function() return "Optional: Spillway data cores  " .. spillwayCores() .. "/3" end,
          done = function() return spillwayCores() >= 3 end },
    },
    onObjective = function(oid, quiet)
        for _, k in ipairs(KEYS) do
            if k.obj == oid then pinKey(k.name, quiet) end
        end
    end,
    onComplete = function(quiet)
        if not quiet then M().winT = 4.5 end -- the cheer, then the win screen
    end,
})

-- Called by surface.lua when SIGNAL completes: the caravan answers.
function Z.onSignal(quiet)
    if Missions.status("crossing") == 0 then Missions.start("crossing", quiet) end
    setCartLamp(true)
    M().cartLit = true
    if not quiet then
        forge.audio.play("assets/sounds/prism.wav", 1.0)
        forge.hud.say("WREN", "They answered! The caravan is stuck past the Spillway - the old dam's gates jammed. "
                      .. "Take the rail cart by the tower.", 7.0)
    end
end
function Z.wrenLine() -- what Wren says on the Surface once the signal is sent
    if Missions.status("crossing") == 2 then return "You did it. The valley is open again." end
    return "The caravan is past the old dam. The rail cart by the tower runs on the relay's power. Go on."
end

-- ---- Areas, respawn points, hints ----------------------------------------------------------
local function areaAt(p)
    if p.y > 8.0 and p.z < -22.0 then return "crest" end
    if p.x >= 764.5 and p.x < 786.0 and p.z < 6.0 then return "w" end   -- the chamber and the plate
    if p.x >= 786.0 and p.x < 814.0 and p.z < 4.0 then return "c" end   -- the gorge and the lip
    if p.x >= 814.0 and p.z < -6.2 then return "e" end                  -- the hall
    if p.z > 22.0 then return "land" end
    return "plaza"
end

local function solved(area) -- the key is seated, or already in someone's hands
    for _, k in ipairs(KEYS) do
        if k.area == area then
            return Missions.isDone("crossing", k.obj) or dist2(forge.scene.getPosition(k.name), k.pos) > 2.5
        end
    end
    return true
end

local LABEL = {
    w = "THE SLUICE: the chamber is flooded", c = "THE CURRENT: the water runs for the drop",
    e = "THE TURBINE HALL: it is dark in there", crest = "The dam crest: the caravan is at the far end",
}
local HINTS = { -- [area] = { first (vague), second (plain) }
    w = { "That plate by the chamber is a scale. Listen to the chains.",
          "The sluice wants three weights: your crates, or you and two of them. It holds a while after you step off." },
    c = { "Whatever floats down there stops at the grate.",
          "Throw crates upstream. They lodge in a row: jump along them, against the current." },
    e = { "The hall is dark. Your drone could carry a light for you.",
          "Send SPARK ahead with F. Find the prism and bring it to the violet mark." },
}

local function hintFor(area)
    local m = M()
    if Missions.status("crossing") == 2 then return "The caravan is free." end
    if m.crownOpen then return "The crown gate is open - cross the dam" end
    if area and LABEL[area] and not solved(area) then return LABEL[area] end
    if area == "crest" then return LABEL.crest end
    return "Three keys to the crown winch  (" .. seatedCount() .. "/3)"
end

local function updateAreas(dt, pp)
    local m = M()
    local a = areaAt(pp)
    if m.arrived and a and a ~= m.area then
        m.area = a
        K.setCp(CP[a], m.visited[a] == true)
        m.visited[a] = true
    end
    local h = hintFor(m.area)
    if h ~= m.hint then
        m.hint = h
        forge.hud.set_hint(h)
    end
    -- one short contextual hint per sub-area after ~40 s without progress; a plainer one 60 s after that
    local ar = m.area
    if ar and HINTS[ar] and not solved(ar) and m.arrived then
        m.noProg[ar] = (m.noProg[ar] or 0) + dt
        local lv = m.hintLv[ar] or 0
        if lv == 0 and m.noProg[ar] >= 40.0 then
            m.hintLv[ar] = 1
            say(HINTS[ar][1], 6.0)
            forge.audio.play("assets/sounds/chirp_hint.wav", 0.5)
        elseif lv == 1 and m.noProg[ar] >= 100.0 then
            m.hintLv[ar] = 2
            say(HINTS[ar][2], 7.0)
            forge.audio.play("assets/sounds/chirp_hint.wav", 0.5)
        end
    end
end

-- ---- Audio beds -------------------------------------------------------------------------------
local function updateAudio(dt, pp)
    if HUM.spl_water then
        local d = math.min(math.sqrt(dist2(pp, vec3(790, 0, -12))), math.sqrt(dist2(pp, vec3(826, 0, -17))))
        local vol = math.max(0.0, math.min(0.6, 0.7 - d / 70.0))
        if inHall(pp) then vol = vol * 0.6 end
        forge.audio.set_volume(HUM.spl_water, vol)
    end
    if HUM.spl_turbine then
        local on = M().tb.on
        local d = math.sqrt(dist2(pp, vec3(828, 1, -17)))
        forge.audio.set_volume(HUM.spl_turbine, on and math.max(0.0, math.min(0.35, 0.45 - d / 90.0)) or 0.0)
    end
end

-- ---- Hooks called by scene.lua / surface.lua ---------------------------------------------------
function Z.update(dt, pp)
    local m = M()
    -- an old save (or the hook-less state): SIGNAL done means CROSSING is open
    if Missions.status("signal") == 2 and Missions.status("crossing") == 0 then
        Z.onSignal(true)
    end
    if m.cartLit == nil or m.cartLit ~= (Missions.status("crossing") ~= 0) then
        m.cartLit = Missions.status("crossing") ~= 0
        setCartLamp(m.cartLit)
    end
    updateRide(dt, pp)
    local here = pp.x > 600
    if here ~= m.here then m.here = here end
    if not here then return end
    m.drownCool = math.max(m.drownCool - dt, 0)
    -- mood: dusk, and the hall's dark when you step in (eased)
    local target = inHall(pp) and 1.0 or 0.0
    m.hallK = m.hallK + (target - m.hallK) * math.min(dt * 3.0, 1.0)
    if math.abs(m.hallK - 1.0) < 0.005 then m.hallK = 1.0 elseif math.abs(m.hallK) < 0.005 then m.hallK = 0.0 end
    m.moodT = m.moodT + dt
    local sm = SURFACE and SURFACE.mem and SURFACE.mem()
    if sm and sm.blendT and sm.blendT < 1.5 then -- the Surface is still blending its mood: win it back each frame
        applyMoodNow()
    elseif math.abs(m.hallK - m.hallApplied) > 0.004 then
        applyLight(m.hallK)
    elseif m.moodT > 2.0 then -- belt and braces: the Surface's blend must never leak in
        m.moodT = 0
        applyMoodNow()
    end
    updatePins(dt)
    updateLane(dt, pp)
    updateSluice(dt, pp)
    updateTurbine(dt, pp)
    updateProps(dt)
    -- the water takes whoever is under its surface
    local w = waterAt(pp)
    if w and feetY(pp) < w.level() - 0.35 and pp.y > -6.0 then drown(pp, w) end
    updateWren(dt, pp)
    updateAreas(dt, pp)
    updateAudio(dt, pp)
    if m.crownOpen == false and seatedCount() >= 3 then crownUp(false) end
    if m.winT then
        m.winT = m.winT - dt
        if m.winT <= 0 then
            m.winT = nil
            K.win()
        end
    end
end

function Z.onStart()
    build()
    if not HUM.spl_water then HUM.spl_water = forge.audio.loop("assets/sounds/water_loop.wav", 0.0) end
    if not HUM.spl_sluice then HUM.spl_sluice = forge.audio.loop("assets/sounds/hum_platform.wav", 0.0) end
    if not HUM.spl_turbine then HUM.spl_turbine = forge.audio.loop("assets/sounds/hum_belt.wav", 0.0) end
    local m = M()
    setCartLamp(Missions.status("crossing") ~= 0)
    -- plaza lamps and the doorway/caravan glow (the hall's own lights come on with the turbine)
    forge.render.light("spl_lamp1", vec3(786.0, 3.5, 27.0), vec3(2.6, 1.6, 0.8), 11.0)
    forge.render.light("spl_lamp2", vec3(814.0, 3.5, 27.0), vec3(2.6, 1.6, 0.8), 11.0)
    forge.render.light("spl_lamp3", vec3(771.0, 3.5, -4.5), vec3(2.6, 1.6, 0.8), 10.0)
    forge.render.light("spl_lamp4", vec3(800.0, 3.5, -3.0), vec3(2.6, 1.6, 0.8), 10.0)
    forge.render.light("spl_halldoor", vec3(825.0, 3.6, -4.5), vec3(2.6, 1.7, 0.5), 8.0)
    forge.render.light("spl_van1", vec3(848.0, 11.0, -32.0), vec3(2.6, 1.7, 0.8), 12.0)
    forge.render.light("spl_van2", vec3(856.0, 11.0, -30.0), vec3(2.6, 1.7, 0.8), 10.0)
    if m.tb.on then
        forge.render.light("spl_hall1", vec3(816.5, 5.2, -17.0), vec3(2.0, 1.7, 1.1), 14.0)
        forge.render.light("spl_hall2", vec3(826.0, 5.2, -25.0), vec3(2.0, 1.7, 1.1), 14.0)
        forge.render.light("spl_hall3", vec3(832.0, 5.2, -9.0), vec3(2.0, 1.7, 1.1), 14.0)
    end
    if m.here then applyMoodNow() end
    m.hint = nil
    -- the E verbs: the cart, the caravan
    Interact.add({ id = "spl_cart", pos = vec3(CART.x, 1.0, CART.z), radius = 3.2, body = "Spl Cart",
        prompt = "E  Ride the rail cart to the Spillway",
        enabled = function() return M().ride.phase == "idle" and not M().here and Missions.status("crossing") == 1 end,
        use = function()
            local r = M().ride
            r.phase, r.t = "fade", 0
            forge.audio.play("assets/sounds/power_up.wav", 0.8, 0.7)
            forge.audio.play("assets/sounds/hum_platform.wav", 0.5)
            forge.hud.set_hint("")
            forge.log("rail cart: boarding")
        end })
    Interact.add({ id = "caravan", pos = vec3(850.0, 9.9, -31.5), radius = 3.6, body = "Wren C Body",
        prompt = function() return M().talk and "E  Next" or "E  Talk" end,
        enabled = function() return M().here and Missions.status("crossing") ~= 0 end,
        use = function()
            local m2 = M()
            if m2.talk then talkNext() return end
            if Missions.status("crossing") == 1 and m2.crownOpen then
                wrenPlay("Cheer", false)
                forge.audio.play("assets/sounds/fanfare.wav", 0.8)
                m2.talk = { lines = { "UNIT-7! You crossed the dam!",
                                      "The gates are free and the whole caravan is rolling.",
                                      "The valley is open again. Thank you." }, i = 0, t = 0 }
                talkNext()
                Missions.complete("crossing", "caravan")
            elseif Missions.status("crossing") == 2 then
                m2.talk = { lines = { "Go on, UNIT-7. The road is yours." }, i = 0, t = 0 }
                talkNext()
            else
                m2.talk = { lines = { "The crown gate is still shut? Seat the three keys at the winch." }, i = 0, t = 0 }
                talkNext()
            end
        end })
end

-- Continue onto a Spillway checkpoint: re-derive the world from the saved mission.
function Z.resume(cp)
    local m = M()
    m.ride.phase, m.arrived, m.here = "done", true, true
    forge.scene.setPosition("Spl Car", vec3(STATION.x, STATION.y, STATION.z))
    Missions.load()
    m.area, m.hint = nil, nil
    for a in pairs(CP) do m.visited[a] = true end
    for _, k in ipairs(KEYS) do
        if Missions.isDone("crossing", k.obj) then pinKey(k.name, true) end
    end
    if Missions.isDone("crossing", "turbine") then turbineOn(true) end
    if Missions.isDone("crossing", "crown") or seatedCount() >= 3 then
        crownUp(true)
        forge.scene.setPosition("Spl Crown", vec3(762.0, 8.6, -6.75))
    end
    setCartLamp(Missions.status("crossing") ~= 0)
    m.hallK = 0
    applyMoodNow()
    forge.log("spillway: resumed at checkpoint " .. cp .. " (" .. seatedCount() .. " keys seated)")
end

-- R in the Spillway: this area's loose props go home.
function Z.resetArea(cp)
    local area = K.checkpoints[cp] and K.checkpoints[cp].area
    local n = 0
    local m = M()
    for _, c in ipairs(PROPS) do
        if c.area == area and not m.pinned[c.name] then
            m.lane.lodged[c.name] = nil
            K.sendHome(c)
            n = n + 1
        end
    end
    forge.log("spillway area reset (" .. tostring(area) .. "): " .. n .. " props returned")
    return n
end

-- resetGame: water up, keys loose, the hall dark, the gate down, the cart dead.
function Z.reset()
    SPILLWAY_MEM = freshMem()
    if not SPILLWAY_BUILT then return end
    local m = M()
    setLevel("Spl Water W", WY)
    forge.scene.setPosition("Spl Crown", vec3(762.0, 2.6, -6.75))
    forge.scene.setTexture("Spl Crown", "metal_red")
    forge.scene.setEmissive("Spl Crown Lamp", vec3(1.8, 0.1, 0.05))
    forge.scene.setPosition("Spl Cage Door", vec3(835.0, 2.3, -17.0))
    forge.scene.setPosition("Spl Car", vec3(STATION.x, STATION.y, STATION.z))
    forge.scene.setTexture("Spl Receiver", "laser")
    forge.scene.setTexture("Spl Marker", "metal_violet")
    forge.scene.setEmissive("Spl Marker", vec3(0.10, 0.03, 0.20))
    forge.scene.setScale("Spl Beam B", vec3(0.001, 0.001, 0.001))
    forge.scene.setRotation("Spl Turbine", vec3(0, 0, 0))
    forge.scene.setRotation("Spl Winch Drum", vec3(0, 0, 0))
    for i = 1, 3 do forge.render.clear_light("spl_hall" .. i) end
    forge.render.clear_light("spl_spark")
    setCartLamp(false)
    m.cartLit = false
    wrenPlay("Idle", true)
    forge.hud.set_fade(0)
    for _, v in ipairs({ "spl_water", "spl_sluice", "spl_turbine" }) do
        if HUM[v] then forge.audio.set_volume(HUM[v], 0.0) end
    end
end

SPILLWAY = Z
