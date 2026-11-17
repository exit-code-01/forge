-- assets/scripts/surface.lua — Location 2: THE SURFACE (ADR-031).
-- Included by scene.lua after missions.lua. Everything here runs on the
-- facility's own systems through the VAULT kit: the plates, bells, sockets,
-- belt and crates appended below are real entries in the shared tables.
--
-- Layout (world x ~ 356..444, z ~ -44..44, ground y = 0; north is -z):
--   head-house + freight lift (S, z 36)  ->  relay yard + tower (centre)
--   W: THE RAVINE   - a 12 m gap; the bridge answers to SPARK's pad on a
--                     rock you can only SEE from the lookout (station SPARK).
--   NE: THE LEDGE   - a locked shed; the bell on the north cliff opens it,
--                     16 m away: only a fully charged throw rings it.
--   E: THE ORE BELT - the belt drops its cell into an arc yard; SPARK
--                     stationed under the drop catches it and sets it in
--                     the cradle (catch + deposit).
-- Three cells in the junction by the gate power the tower; the transmitter
-- inside ends the run (VAULT.win).

local K = VAULT
forge.include("assets/scripts/surface_dress.lua") -- SURFACE_DRESS(K): the manifest dressing
local X = -3.0 -- the facility's R15-R17 axis (the freight shaft)

if not (Missions and Interact) then error("surface.lua needs missions.lua (Missions/Interact)") end
local S = {} -- published as SURFACE at the END of the file (a half-loaded module stays off)

-- Mutable state survives a hot reload (globals); resetGame rebuilds it.
local function freshMem()
    return {
        loc = "facility", blendT = 0, keyFrom = vec3(3.0, 2.9, 2.7),
        ride = { phase = "idle", y = 0.1, v = 0, t = 0, sy = -3.4 },
        pinned = {},            -- cell name -> seat (locked in the junction)
        powered = false, powerT = 0, transmitted = false, winT = nil,
        visited = {}, hint = nil, area = nil,
        talk = nil, tipIdx = 0, wrenYaw = 200, wrenAnim = "Idle", wrenAnimT = 0,
        coil = { 0, 0.8, 1.6, 2.4 }, coilFlash = { 0, 0, 0, 0 }, s2T = 0, zapCool = 0,
        lockBeepT = 0,
    }
end
SURFACE_MEM = SURFACE_MEM or freshMem()
local M = function() return SURFACE_MEM end
S.mem = M

-- ---- Location mood (sky, key light, fog, exposure, audio beds) ----------
local FAC = { -- the renderer's indoor defaults (ADR-030) + scene.lua's fog
    zenith = vec3(0.018, 0.022, 0.034), horizon = vec3(0.052, 0.054, 0.068),
    ground = vec3(0.030, 0.030, 0.034), sunSize = 1.5, sunInt = 0.12, halo = 0.15,
    fog = vec3(0.020, 0.024, 0.034), fogD = 0.010, exposure = 1.0, bloom = 0.6,
    dir = vec3(0.5, 0.8, 0.35), far = 100, amb = 0.30, music = 0.22,
}
local SUR = { -- afternoon valley: sun in the south-east behind the head-house,
    -- so the way north (tower, terraces) is front-lit; a light haze
    zenith = vec3(0.10, 0.24, 0.72), horizon = vec3(0.46, 0.58, 0.80),
    ground = vec3(0.26, 0.25, 0.22), sunSize = 2.2, sunInt = 3.0, halo = 0.30,
    fog = vec3(0.48, 0.60, 0.80), fogD = 0.0026, exposure = 0.80, bloom = 0.30,
    key = vec3(5.4, 5.0, 4.3), dir = vec3(0.63, 0.57, 0.53), far = 320,
    amb = 0.06, music = 0.14, -- the HVAC bed drops to a far-off wind-like hush
    -- hemisphere ambient (ADR-033): blue sky light from above, a warm bounce
    -- off the sunlit valley floor from below. The facility keeps the
    -- renderer's key-derived default (weight 0 in applyMood).
    ambSky = vec3(0.46, 0.52, 0.62), ambGround = vec3(0.32, 0.29, 0.24),
}
local function lerp(a, b, t) return a + (b - a) * t end
local function lv(a, b, t) return vec3(lerp(a.x, b.x, t), lerp(a.y, b.y, t), lerp(a.z, b.z, t)) end

local function applyMood(t, keyFrom)
    local k = t * t * (3 - 2 * t)
    forge.render.set_sky({
        zenith = lv(FAC.zenith, SUR.zenith, k), horizon = lv(FAC.horizon, SUR.horizon, k),
        ground = lv(FAC.ground, SUR.ground, k), sun_size = lerp(FAC.sunSize, SUR.sunSize, k),
        sun_intensity = lerp(FAC.sunInt, SUR.sunInt, k), sun_halo = lerp(FAC.halo, SUR.halo, k),
    })
    forge.render.set_fog(lv(FAC.fog, SUR.fog, k), lerp(FAC.fogD, SUR.fogD, k))
    forge.render.set_exposure(lerp(FAC.exposure, SUR.exposure, k))
    forge.render.set_bloom(lerp(FAC.bloom, SUR.bloom, k))
    forge.render.set_light(lv(keyFrom, SUR.key, k), lv(FAC.dir, SUR.dir, k))
    forge.render.set_ambient(SUR.ambSky, SUR.ambGround, k) -- k = 0: the facility default
    forge.render.set_view_distance(t > 0 and SUR.far or FAC.far)
    if HUM.amb then forge.audio.set_volume(HUM.amb, lerp(FAC.amb, SUR.amb, k)) end
    if HUM.music then forge.audio.set_volume(HUM.music, lerp(FAC.music, SUR.music, k)) end
end

-- Location by position: the Surface is far east of the facility.
local function locAt(p) return p.x > 200 and "surface" or "facility" end

local function setLocation(loc, instant)
    local m = M()
    if loc == m.loc and not instant then return end
    m.loc = loc
    if loc == "surface" then
        m.keyFrom = K.keyLight()
        m.blendT = instant and 1.5 or 0
        applyMood(instant and 1 or 0, m.keyFrom)
        forge.log("location: the Surface" .. (instant and " (instant)" or " (blend)"))
    else
        m.blendT = 0
        applyMood(0, K.keyLight())
        forge.log("location: the facility")
    end
end

function S.ownsMood() return M().loc == "surface" end

-- ---- Data appended to the shared systems (include time, before the build)
local function isCell(name) return name:sub(1, 5) == "Cell " end

local CP = {} -- area -> checkpoint index (past the facility's)
for _, c in ipairs({
    { "arrival", vec3(400, 1.0, 29.0) }, { "relay", vec3(396, 1.0, 17.5) },
    { "ravine", vec3(382, 1.0, -22.0) }, { "ledge", vec3(423, 5.5, -19.0) },
    { "belt", vec3(416, 3.5, 25.5) },
}) do
    K.checkpoints[#K.checkpoints + 1] = { z = -1e9, pos = c[2], area = c[1] }
    CP[c[1]] = #K.checkpoints
end
S.CP = CP

local CELLS = {
    { name = "Cell A", pos = vec3(359.0, 0.36, -38.0), area = "ravine", obj = "ravine" },
    { name = "Cell B", pos = vec3(441.0, 4.86, -23.0), area = "ledge", obj = "shed" },
    { name = "Cell C", pos = vec3(426.0, 8.86, 30.2), area = "belt", obj = "belt" },
}
S.CELLS = CELLS
local CRATE_S2 = { name = "Crate S2", pos = vec3(430.0, 4.75, -24.5) }
for _, c in ipairs(CELLS) do K.crates[#K.crates + 1] = { name = c.name, pos = c.pos } end
K.crates[#K.crates + 1] = CRATE_S2

-- (a) the ravine: SPARK's pad on the mesa top, latched like R12's
K.plates[#K.plates + 1] = { id = "ps_pad", center = vec3(359.8, 4.85, -30.2),
    half = vec3(0.8, 0.3, 0.8), latch = true, offTex = "metal_blue", noPlayer = true,
    visY = 4.55, visScale = vec3(1.6, 0.1, 1.6) } -- thin, set 2.5 m back from every edge
-- F aim assist: the pad is 0.1 m thick and ~26 m away (a 0.3 degree aim
-- otherwise); the drop mark is hidden behind the terrace lip from the run
-- plate, so aiming at the lip / the yard's near half also sends SPARK there.
K.fSnaps[#K.fSnaps + 1] = { at = vec3(359.8, 4.85, -30.2), r = 4.5,
    station = vec3(359.8, 5.6, -30.2) }
K.fSnaps[#K.fSnaps + 1] = { at = vec3(426.0, 0.0, 16.0), r = 7.0, station = vec3(426.0, 3.4, 16.5),
    when = function(p) return p.x > 405 and p.x < 424.5 and p.z > 8 and p.z < 30 and p.y > 2.3 end }
-- (c) the ore belt's run plate on the terrace
K.plates[#K.plates + 1] = { id = "ps_belt", center = vec3(417.0, 2.85, 18.0),
    half = vec3(0.7, 0.3, 0.7), visY = 2.6 }
-- (b) the ledge: a big cliff bell, full-charge only. Its collider is 2 x 2 x
-- 1.4, so a crate stops 0.95 m from its centre: the detection box must
-- reach past that (0.9 + the 0.3 pad) or a hit can bounce off unheard.
-- Playtest: a throw aimed AT the bell sags ~2 m over 16 m and hit the cliff
-- below it. The box therefore reaches down the cliff face (y 3.0 .. 8.8): any
-- fast hit on the bell or its plate rings, so a thrower needs no lob.
K.bells[#K.bells + 1] = { id = "bS", name = "Srf Bell", center = vec3(431.0, 5.9, -43.2),
    half = vec3(1.3, 2.9, 0.9), ringSpeed = 14.0 }
-- the junction (three power seats) + the ore-belt cradle (SPARK's deposit)
local SEATS = {}
for i, x in ipairs({ 404.2, 406.0, 407.8 }) do
    local s = { id = "s_j" .. i, name = "Srf Socket " .. i, seat = vec3(x, 1.15, 11.2),
                accepts = isCell, grabby = true, offTex = "metal_yellow",
                sfxOn = "assets/sounds/power_up.wav", sfxOff = "assets/sounds/power_down.wav" }
    K.sockets[#K.sockets + 1] = s
    SEATS[i] = s
end
K.sockets[#K.sockets + 1] = { id = "s_cr", name = "Srf Cradle", seat = vec3(423.5, 2.85, 16.0),
                              accepts = isCell, grabby = true }
-- SPARK's catcher stance where a catch solution exists (under the belt end)
K.drone.catcherZones[#K.drone.catcherZones + 1] = { zMin = 13.0, zMax = 19.0, xMin = 424.0, xMax = 433.0 }
-- the ore belt: runs north only while ps_belt is weighed
K.belts[#K.belts + 1] = { id = "bs", name = "Srf Belt", x = 426.0, z0 = 31.0, z1 = 19.0,
    hw = 0.55, speed = 2.2, cur = 0, off = 0, pips = "Srf Pip ", npips = 6, zMin = 4.0,
    zMax = 36.0, hearX = 16.0, hum = "belt_s", humVol = 0.3, y = 8.85, pipY = 8.515,
    dirFn = function() return K.plateOn("ps_belt") and -1 or 0 end }
-- three surface data cores (8 -> 11)
S.coreIdx = {}
for _, c in ipairs({ vec3(358.0, 0.9, -42.0), vec3(441.0, 5.4, -17.6), vec3(382.0, 0.9, 42.0) }) do
    CORE_POS[#CORE_POS + 1] = c
    S.coreIdx[#S.coreIdx + 1] = #CORE_POS
end
-- doors (a bridge, a shed door, the relay gate) through the kit's normaliser
K.addDoor("Srf Bridge", { closedPos = vec3(359.9, -0.3, -22.0), openPos = vec3(372.0, -0.15, -22.0),
    speed = 3.0, offTex = "metal_blue", cond = function() return K.plateOn("ps_pad") end })
K.addDoor("Srf Shed Door", { closedPos = vec3(438.0, 5.7, -23.0), openPos = vec3(438.0, 5.7, -24.9),
    speed = 2.0, cond = function() return K.bellRung("bS") end })
K.addDoor("Srf Gate", { x = 400.0, z = 9.0, y = 1.25, lift = 2.6,
    cond = function() return M().powered == true end })

-- SPARK follows higher on terraces and up the freight shaft
K.ext.cruiseY = function(p)
    local r = M().ride.phase
    if p.x > 200 then return p.y + 3.0 end
    if r == "rise" or r == "fade" or r == "close" then return math.max(4.2, p.y + 2.4) end
    return 4.2
end

-- ---- Build (once per process; geometry never re-spawns on reload) -------
local count = 0
local MATTE = 0.9 -- terrain roughness (ADR-033): rock and soil, no sun glare
local STRUCT_FILL = 0.08
local function blk(name, x0, x1, y0, y1, z0, z1, tex, tint, solid, mesh)
    local pos = vec3((x0 + x1) * 0.5, (y0 + y1) * 0.5, (z0 + z1) * 0.5)
    K.box(name, pos, vec3(x1 - x0, y1 - y0, z1 - z0), tex or "concrete", solid ~= false, false, mesh)
    if tint then
        forge.scene.setTint(name, tint)
        if (tex or "concrete") == "concrete" then
            -- terrain: matte, and its shade comes from the Surface's sky
            -- ambient (applyMood) - the old emissive "shade fill" is gone
            forge.scene.setRoughness(name, MATTE)
        else
            -- built shells on dark metal/panel textures keep half the old
            -- fill: without it the tower reads black against the sky
            forge.scene.setEmissive(name, tint * STRUCT_FILL)
        end
    end
    count = count + 1
end
local function prop(name, pos, scale, tex, solid, mesh)
    K.box(name, pos, scale, tex, solid, false, mesh)
    count = count + 1
end
local GRASS = vec3(0.50, 0.66, 0.34)
local ROCK = vec3(0.74, 0.66, 0.58)
local ROCK2 = vec3(0.64, 0.57, 0.50)
local ROCK3 = vec3(0.56, 0.50, 0.45)
local DIRT = vec3(0.78, 0.66, 0.50)

-- When the platform-top mesh is loaded (tools/gen_tops.py, ADR-032) it is the
-- walking surface of the mesa, lookout, ledge, terrace and ore shelf at their
-- exact heights, and the boxes sink 6 cm under it as backing.
local function TOPS() return forge.scene.has_model("srf_terr_tops") and -0.06 or 0 end

local function buildGround()
    -- valley floor, with holes for the ravine (x 366..378) and the lift pit.
    -- When the ground mesh is loaded (tools/gen_ground.py, ADR-032) IT is the
    -- floor at y = 0 and these boxes sink 6 cm to become its backing.
    local gy = forge.scene.has_model("srf_terr_ground") and -0.06 or 0
    blk("Srf Ground W", 340, 366, -2, gy, -60, 60, "concrete", GRASS)
    blk("Srf Ground N", 378, 460, -2, gy, -60, 34, "concrete", GRASS)
    blk("Srf Ground S", 378, 460, -2, gy, 39, 60, "concrete", GRASS)
    blk("Srf Ground SW", 378, 398.5, -2, gy, 34, 39, "concrete", GRASS)
    blk("Srf Ground SE", 401.5, 460, -2, gy, 34, 39, "concrete", GRASS)
    -- the ravine: sheer walls into the dark, a floor far below (visual only)
    blk("Srf Ravine W", 362, 366, -26, -2, -60, 60, "concrete", ROCK3)
    blk("Srf Ravine E", 378, 382, -26, -2, -60, 60, "concrete", ROCK3)
    blk("Srf Ravine Floor", 366, 378, -27, -26, -60, 60, "concrete", vec3(0.2, 0.2, 0.22), false)
    -- the cliff ring: three stepped tiers per side, none climbable (>= 8 m)
    local tiers = { { 4, 8, ROCK }, { 8, 14, ROCK2 }, { 14, 21, ROCK3 } }
    for i, t in ipairs(tiers) do
        local a, b = 44 + (i - 1) * 4, 44 + i * 4
        local top, tint = t[2] + (i == 3 and 0 or 0), t[3]
        blk("Srf Cliff N" .. i, 340 - 12, 460 + 12, -26, top, -b, -a, "concrete", tint)
        blk("Srf Cliff S" .. i, 340 - 12, 460 + 12, -26, top, a, b, "concrete", tint)
        blk("Srf Cliff W" .. i, 356 - i * 4, 356 - (i - 1) * 4, -26, top, -a, a, "concrete", tint)
        blk("Srf Cliff E" .. i, 444 + (i - 1) * 4, 444 + i * 4, -26, top, -a, a, "concrete", tint)
    end
end

local function buildHeadHouse()
    local T = vec3(0.62, 0.62, 0.66)
    blk("Srf HH W", 395.6, 396.0, 0, 4.6, 31.6, 41.4, "panel", T)
    blk("Srf HH E", 404.0, 404.4, 0, 4.6, 31.6, 41.4, "panel", T)
    blk("Srf HH S", 396.0, 404.0, 0, 4.6, 41.0, 41.4, "panel", T)
    blk("Srf HH N1", 396.0, 398.0, 0, 4.6, 31.6, 32.0, "panel", T)
    blk("Srf HH N2", 402.0, 404.0, 0, 4.6, 31.6, 32.0, "panel", T)
    blk("Srf HH Lintel", 398.0, 402.0, 3.4, 4.6, 31.6, 32.0, "panel", T)
    blk("Srf HH Roof", 395.4, 404.6, 4.6, 5.0, 31.4, 41.6, "metal_dark")
    blk("Srf HH Sign", 398.4, 401.6, 3.55, 3.75, 31.5, 31.56, "metal_yellow", nil, false)
    forge.scene.setEmissive("Srf HH Sign", vec3(1.4, 1.0, 0.15))
    -- the pit the freight lift rises out of
    blk("Srf Pit W", 398.5, 398.9, -3.8, 0, 34.0, 39.0, "metal_dark")
    blk("Srf Pit E", 401.1, 401.5, -3.8, 0, 34.0, 39.0, "metal_dark")
    blk("Srf Pit N", 398.9, 401.1, -3.8, 0, 34.0, 34.4, "metal_dark")
    blk("Srf Pit S", 398.9, 401.1, -3.8, 0, 38.6, 39.0, "metal_dark")
    blk("Srf Pit Floor", 398.5, 401.5, -4.0, -3.8, 34.0, 39.0, "metal_dark")
    forge.scene.spawn("Srf Lift", vec3(400, M().ride.sy, 36.5), vec3(2.1, 0.2, 4.1),
                      vec3(1.05, 0.1, 2.05), "metal_yellow", false, nil, { kinematic = true })
    forge.scene.setEmissive("Srf Lift", K.glow.metal_yellow)
    count = count + 1
end

local function buildRelay()
    local F = vec3(0.85, 0.83, 0.78)
    blk("Srf Fence N", 391, 409, 0, 2.6, -9.15, -8.85, "metal_white", F)
    blk("Srf Fence W", 390.85, 391.15, 0, 2.6, -9, 9, "metal_white", F)
    blk("Srf Fence E", 408.85, 409.15, 0, 2.6, -9, 9, "metal_white", F)
    blk("Srf Fence SW", 391, 398.2, 0, 2.6, 8.85, 9.15, "metal_white", F)
    blk("Srf Fence SE", 401.8, 409, 0, 2.6, 8.85, 9.15, "metal_white", F)
    blk("Srf Gate Post W", 398.0, 398.4, 0, 5.4, 8.8, 9.2, "metal_dark")
    blk("Srf Gate Post E", 401.6, 402.0, 0, 5.4, 8.8, 9.2, "metal_dark")
    blk("Srf Gate Lintel", 398.0, 402.0, 5.4, 5.8, 8.8, 9.2, "metal_dark")
    prop("Srf Gate", vec3(400, 1.25, 9.0), vec3(3.2, 2.5, 0.25), "metal_red", true, "door")
    -- the tower: four legs, decks, an antenna and a beacon (seen from anywhere)
    for i, c in ipairs({ { 398.2, -4.8 }, { 401.8, -4.8 }, { 398.2, -1.2 }, { 401.8, -1.2 } }) do
        blk("Srf Tower Leg " .. i, c[1] - 0.25, c[1] + 0.25, 0, 24, c[2] - 0.25, c[2] + 0.25, "metal", vec3(0.75, 0.72, 0.70))
        blk("Srf Tower Strip " .. i, c[1] - 0.06, c[1] + 0.06, 0.5, 23.5, c[2] - 0.29, c[2] - 0.25,
            "metal_white", nil, false)
    end
    for i, y in ipairs({ 8, 16 }) do
        blk("Srf Tower Deck " .. i, 397.9, 402.1, y, y + 0.3, -5.1, -0.9, "grate", vec3(0.7, 0.7, 0.7))
    end
    blk("Srf Tower Top", 397.6, 402.4, 24, 24.5, -5.4, -0.6, "metal_dark")
    blk("Srf Tower Mast", 399.85, 400.15, 24.5, 30.5, -3.15, -2.85, "metal")
    blk("Srf Tower Dish", 398.6, 401.4, 20.0, 22.8, -0.95, -0.75, "metal_white", nil, false)
    prop("Srf Beacon", vec3(400, 31.0, -3.0), vec3(0.9, 0.9, 0.9), "metal_red", false)
    -- the transmitter (inside the yard) and the junction (outside the gate)
    prop("Srf Transmitter", vec3(400, 0.6, 1.5), vec3(1.4, 1.2, 0.8), "metal_red", true, "console")
    blk("Srf Junction", 403.2, 408.8, 0, 0.8, 10.7, 11.7, "metal_dark")
    for i, s in ipairs(SEATS) do
        prop(s.name, vec3(s.seat.x, s.seat.y + 0.1, s.seat.z), vec3(1.0, 0.9, 1.0), "metal_yellow",
             false, "socket")
        blk("Srf Cable " .. i, s.seat.x - 0.08, s.seat.x + 0.08, 0.0, 0.06, 9.2, 10.7, "metal_dark",
            nil, false)
    end
    blk("Srf Cable In", 405.92, 406.08, 0.0, 0.06, -1.2, 8.8, "metal_dark", nil, false)
    blk("Srf Cable Cross", 401.8, 406.08, 0.0, 0.06, -1.28, -1.12, "metal_dark", nil, false)
    -- Wren's camp, west of the gate
    blk("Srf Tent", 388.6, 391.8, 0, 1.9, 14.2, 17.4, "concrete", vec3(0.85, 0.62, 0.38))
    prop("Srf Camp Crate 1", vec3(392.6, 0.35, 15.6), vec3(0.7, 0.7, 0.7), "crate", true, "crate")
    prop("Srf Camp Crate 2", vec3(393.2, 0.25, 16.5), vec3(0.5, 0.5, 0.5), "crate", true, "crate")
    prop("Srf Camp Lamp", vec3(392.6, 0.95, 15.6), vec3(0.3, 0.5, 0.3), "metal_yellow", false, "lamp")
    forge.scene.setEmissive("Srf Camp Lamp", vec3(2.0, 1.4, 0.4))
end

local function buildRavine()
    -- the far rock (mesa) with SPARK's pad on top + a blue beacon you CAN see
    -- (wide top: a ray from the ground that clears the rim passes OVER the pad)
    blk("Srf Mesa", 356.5, 363.1, -0.5, 4.5 + TOPS(), -33.5, -26.9, "concrete", ROCK)
    blk("Srf Mesa Beacon", 362.6, 362.85, 4.5, 6.6, -27.4, -27.15, "metal_blue", nil, false)
    forge.scene.setEmissive("Srf Mesa Beacon", vec3(0.25, 0.8, 2.6))
    prop("Srf Bridge", vec3(359.9, -0.3, -22.0), vec3(12.0, 0.3, 2.4), "metal_blue", true)
    -- the lookout: 1 m ledges up to a 6 m top, set back 6 m from the edge
    blk("Srf Lookout", 384.0, 388.0, -0.5, 6.0 + TOPS(), -30.0, -26.0, "concrete", ROCK)
    for i = 1, 5 do
        local x0 = 388.0 + (5 - i) * 1.6
        blk("Srf Lookout L" .. i, x0, x0 + 1.6, -0.5, i * 1.0, -29.6, -26.4, "concrete",
            i % 2 == 0 and ROCK2 or ROCK)
    end
end

local function buildLedge()
    -- terrace B (4.5 m) + 0.3 m stairs from the west
    blk("Srf Ledge", 418, 444, -0.5, 4.5 + TOPS(), -28, -16, "concrete", ROCK2)
    for i = 1, 14 do
        local x0 = 418 - 0.6 * (15 - i)
        blk("Srf Ledge Step " .. i, x0, x0 + 0.6, -0.5, 0.3 * i, -20, -17.6, "concrete",
            i % 2 == 0 and DIRT or ROCK)
    end
    -- the shed (door on its west face) and the cell inside
    local W = vec3(0.66, 0.56, 0.48)
    blk("Srf Shed W1", 437.85, 438.15, 4.5, 7.5, -26.0, -23.8, "panel", W)
    blk("Srf Shed W2", 437.85, 438.15, 4.5, 7.5, -22.2, -20.0, "panel", W)
    blk("Srf Shed Lintel", 437.85, 438.15, 6.9, 7.5, -23.8, -22.2, "panel", W)
    blk("Srf Shed E", 442.85, 443.15, 4.5, 7.5, -26.0, -20.0, "panel", W)
    blk("Srf Shed N", 438.15, 442.85, 4.5, 7.5, -26.15, -25.85, "panel", W)
    blk("Srf Shed S", 438.15, 442.85, 4.5, 7.5, -20.15, -19.85, "panel", W)
    blk("Srf Shed Roof", 437.6, 443.4, 7.5, 7.8, -26.4, -19.6, "metal_dark")
    prop("Srf Shed Door", vec3(438.0, 5.7, -23.0), vec3(0.2, 2.4, 1.6), "metal_red", true)
    -- the bell on the north cliff face, on a bracket
    prop("Srf Bell", vec3(431.0, 7.0, -43.2), vec3(2.0, 2.0, 1.4), "metal_orange", true, "bell")
    blk("Srf Bell Bracket", 430.7, 431.3, 8.0, 8.4, -44.0, -42.6, "metal_dark")
    blk("Srf Bell Plate", 429.8, 432.2, 5.6, 8.6, -44.05, -43.95, "metal_dark", nil, false)
end

local function buildBelt()
    -- terrace C (2.5 m) + 0.25 m stairs from the west
    blk("Srf Terrace C", 414, 424, -0.5, 2.5 + TOPS(), 8, 28, "concrete", ROCK)
    for i = 1, 9 do
        local x0 = 414 - 0.6 * (10 - i)
        blk("Srf Terrace Step " .. i, x0, x0 + 0.6, -0.5, 0.25 * i, 22, 24.4, "concrete",
            i % 2 == 0 and DIRT or ROCK2)
    end
    -- the arc yard: electrified floor (red), fenced north and south
    blk("Srf Arc Floor", 424, 433, -0.1, 0.05, 6, 33, "hazard", vec3(1.0, 0.32, 0.28))
    blk("Srf Arc Fence N", 424, 433, 0, 2.2, 5.85, 6.15, "hazard")
    blk("Srf Arc Fence S", 424, 433, 0, 2.2, 32.85, 33.15, "hazard")
    blk("Srf Arc Fence W1", 423.85, 424.15, 0, 2.2, 6.15, 8.0, "hazard")
    blk("Srf Arc Fence W2", 423.85, 424.15, 0, 2.2, 28.0, 32.85, "hazard")
    blk("Srf Ore Shelf", 433, 444, -0.5, 6.0 + TOPS(), 6, 33, "concrete", ROCK2)
    -- the belt on its gantry: a hopper tower at the south end, legs east
    blk("Srf Hopper", 424.8, 427.2, 0, 8.44, 29.4, 32.6, "metal_dark")
    prop("Srf Belt", vec3(426.0, 8.47, 25.0), vec3(1.1, 0.06, 12.0), "belt", true)
    for i, z in ipairs({ 25.0, 21.0 }) do
        blk("Srf Gantry Leg " .. i, 426.65, 426.95, 0.05, 8.44, z - 0.15, z + 0.15, "metal_dark")
    end
    blk("Srf Gantry Beam", 425.3, 426.95, 8.2, 8.44, 20.85, 25.15, "metal_dark")
    for i = 1, 6 do
        prop("Srf Pip " .. i, vec3(426.0, 8.515, 30.0), vec3(0.8, 0.02, 0.1), "metal_white", false)
    end
    -- where the cell lands if nothing catches it: SPARK's blue drop mark
    blk("Srf Drop Mark", 425.4, 426.6, 0.05, 0.08, 15.4, 16.6, "metal_blue", nil, false)
    -- the cradle on the terrace edge (SPARK's deposit point)
    prop("Srf Cradle", vec3(423.5, 2.95, 16.0), vec3(1.0, 0.9, 1.0), "metal_orange", false, "socket")
    for i, c in ipairs({ { 424.8, 7.2 }, { 432.0, 7.2 }, { 432.0, 31.6 }, { 424.8, 27.0 } }) do
        prop("Srf Tesla " .. i, vec3(c[1], 1.15, c[2]), vec3(0.7, 2.4, 0.7), "metal_white", false, "tesla")
    end
end

local function spawnMovables()
    for _, c in ipairs(CELLS) do
        forge.scene.spawn(c.name, c.pos, vec3(0.5, 0.7, 0.5), vec3(0.25, 0.35, 0.25), "metal_yellow",
                          true, "battery", { lockRotation = true })
        forge.scene.setEmissive(c.name, vec3(1.8, 1.4, 0.12))
        count = count + 1
    end
    K.box(CRATE_S2.name, CRATE_S2.pos, vec3(0.5, 0.5, 0.5), "crate", true, true, "crate")
    -- Wren: the quest-giver (a relay-keeper robot), with a box collider of her own
    -- wren_bot: an original robot (tools/gen_wren.py); the knight is the fallback
    local wrenModel = forge.scene.has_model("wren_bot") and "wren_bot" or "knight"
    forge.scene.spawn("Wren", vec3(394.5, 0.0, 13.0), vec3(1, 1, 1), vec3(0, 0, 0), "", false, wrenModel)
    forge.scene.spawn("Wren Body", vec3(394.5, 0.9, 13.0), vec3(0.001, 0.001, 0.001),
                      vec3(0.35, 0.9, 0.35), "metal", false)
    forge.anim.play("Wren", "Idle", { fade = 0 })
    count = count + 3
end

local function build()
    if SURFACE_BUILT then return end
    SURFACE_BUILT = true
    buildGround()
    buildHeadHouse()
    buildRelay()
    buildRavine()
    buildLedge()
    buildBelt()
    spawnMovables()
    if SURFACE_DRESS then count = count + SURFACE_DRESS(K) end
    SURFACE_ENTITIES = count + 3 -- + the cores spawned by buildRooms
    forge.log("surface built: " .. SURFACE_ENTITIES .. " entities")
end

-- ---- Cells, the junction, the tower --------------------------------------
local function cellAtSeat(cellName)
    local at = forge.scene.getPosition(cellName)
    for _, s in ipairs(SEATS) do
        local dx, dy, dz = at.x - s.seat.x, at.y - s.seat.y, at.z - s.seat.z
        if dx * dx + dz * dz < 0.12 and math.abs(dy) < 0.35 and K.state.sockets[s.id] == true then
            return s
        end
    end
    return nil
end
S.cellAtSeat = cellAtSeat

local function seatedCount()
    local n = 0
    for _, c in ipairs(CELLS) do if M().pinned[c.name] then n = n + 1 end end
    return n
end
S.seatedCount = seatedCount

local function setPowerLook(on)
    local col = on and vec3(0.3, 2.2, 0.6) or vec3(0, 0, 0)
    for i = 1, 4 do forge.scene.setEmissive("Srf Tower Strip " .. i, on and vec3(1.6, 1.3, 0.2) or col) end
    forge.scene.setTexture("Srf Beacon", on and "metal_green" or "metal_red")
    forge.scene.setEmissive("Srf Beacon", on and vec3(0.6, 4.0, 1.0) or vec3(0.5, 0.05, 0.03))
    forge.scene.setTexture("Srf Transmitter", on and "metal_orange" or "metal_red")
    forge.scene.setEmissive("Srf Cable In", on and vec3(1.4, 1.1, 0.15) or col)
    forge.scene.setEmissive("Srf Cable Cross", on and vec3(1.4, 1.1, 0.15) or col)
    if on then
        forge.render.light("srf_tower1", vec3(400, 9, -3), vec3(3.0, 2.4, 0.5), 9.0)
        forge.render.light("srf_tower2", vec3(400, 25, -3), vec3(3.0, 2.4, 0.5), 9.0)
        forge.render.light("srf_beacon", vec3(400, 31, -3), vec3(0.6, 4.0, 1.0), 12.0)
    else
        forge.render.clear_light("srf_tower1")
        forge.render.clear_light("srf_tower2")
        forge.render.light("srf_beacon", vec3(400, 31, -3), vec3(1.6, 0.15, 0.1), 6.0)
    end
end

local function powerUp(quiet)
    local m = M()
    if m.powered then return end
    m.powered = true
    setPowerLook(true)
    forge.log("relay: POWERED (three cells seated)")
    if not quiet then
        m.powerT = 2.5
        forge.audio.play("assets/sounds/power_up.wav", 1.0, 0.8)
        forge.audio.play("assets/sounds/secret.wav", 0.8)
        forge.audio.play_at("assets/sounds/gong.wav", vec3(400, 10, -3), 1.0, 0.6)
        forge.fx.burst(vec3(400, 24.5, -3), 40, vec3(2.0, 1.6, 0.3))
        forge.fx.burst(vec3(400, 31, -3), 30, vec3(0.4, 2.4, 0.8))
        forge.camera.shake(0.3)
        forge.hud.toast("THE RELAY IS LIVE", 3.5)
        K.drone.celebrateT = 6.0 -- SPARK's reunion orbit
        K.mood("happy", 1.2)
    end
end

local function pinCell(cellName, quiet)
    local s = cellAtSeat(cellName)
    if not s then -- restored from a save: take the first free seat
        for _, cand in ipairs(SEATS) do
            local taken = false
            for _, seat in pairs(M().pinned) do if seat == cand.id then taken = true end end
            if not taken then s = cand break end
        end
    end
    if not s then return end
    M().pinned[cellName] = s.id
    forge.scene.setPosition(cellName, s.seat)
    local i = tonumber(s.id:sub(-1))
    forge.scene.setEmissive("Srf Cable " .. i, vec3(1.4, 1.1, 0.15))
    if not quiet then
        forge.fx.burst(s.seat, 18, vec3(2.0, 1.6, 0.3))
        forge.audio.play_at("assets/sounds/clunk.wav", s.seat, 1.0, 0.8)
    end
    forge.log("relay: " .. cellName .. " locked into " .. s.id)
    if seatedCount() >= 3 then powerUp(quiet) end
end

local function seatOf(id)
    for _, s in ipairs(SEATS) do if s.id == id then return s end end
end

-- A locked cell stays seated: pinned every frame, and the glove lets go.
local function updatePins(dt)
    local m = M()
    m.lockBeepT = math.max(m.lockBeepT - dt, 0)
    for name, sid in pairs(m.pinned) do
        local s = seatOf(sid)
        if forge.player.holding() == name then
            forge.player.drop()
            if m.lockBeepT <= 0 then
                m.lockBeepT = 1.0
                forge.audio.play("assets/sounds/beep_no.wav", 0.7)
                forge.hud.toast("The junction has locked that cell in.", 2.0)
            end
        end
        local at = forge.scene.getPosition(name)
        local dx, dy, dz = at.x - s.seat.x, at.y - s.seat.y, at.z - s.seat.z
        if dx * dx + dy * dy + dz * dz > 0.0004 then
            forge.scene.setPosition(name, s.seat)
            local id = forge.scene.bodyId(name)
            if id ~= 0 then forge.physics.set_velocity(id, vec3(0, 0, 0)) end
        end
    end
end

-- ---- SIGNAL ----------------------------------------------------------------
local function surfaceCores()
    local n = 0
    for _, i in ipairs(S.coreIdx) do if K.cores.got[i] then n = n + 1 end end
    return n
end
local function cellObjective(id, cellName, text)
    return { id = id, text = text, done = function() return cellAtSeat(cellName) ~= nil end }
end
Missions.define({
    id = "signal", title = "SIGNAL", sub = "power the relay", ordered = false,
    objectives = {
        cellObjective("ravine", "Cell A", "Cell: across the ravine  (west)"),
        cellObjective("shed", "Cell B", "Cell: the shed on the north ledge"),
        cellObjective("belt", "Cell C", "Cell: the ore belt  (east)"),
        { id = "transmit", text = "Send the signal from the tower",
          hidden = function() return not M().powered end },
        { id = "cores", optional = true,
          text = function() return "Optional: surface data cores  " .. surfaceCores() .. "/3" end,
          done = function() return surfaceCores() >= 3 end },
    },
    onObjective = function(oid, quiet)
        for _, c in ipairs(CELLS) do
            if c.obj == oid then pinCell(c.name, quiet) end
        end
    end,
    onComplete = function(quiet)
        local m = M()
        m.transmitted = true
        if not quiet then m.winT = 2.2 end -- beacon flash, Wren cheers, then the win screen
    end,
})

-- ---- Wren: the quest-giver --------------------------------------------------
local WREN = vec3(394.5, 0.0, 13.0)
local OFFER = {
    "A maintenance unit? Up from the vault? Huh. I'm Wren.",
    "I keep this relay. It is how the valley calls the caravan.",
    "It's dead. It needs three power cells.",
    "One past the ravine. One in the shed up north. One on the old ore belt.",
    "Seat them in the junction by the gate. Bring your drone.",
}
local TIPS = {
    ravine = "The ravine bridge answers to a pad on the far rock. You'd see it from up high.",
    shed = "The shed opens when the cliff bell rings. It wants a hard hit.",
    belt = "The belt drops its cell into the arcs. Unless something is there to catch it.",
}

local function wrenPlay(clip, loop)
    local m = M()
    if forge.anim.play("Wren", clip, { loop = loop, fade = 0.25 }) then
        m.wrenAnim = clip
    end
end

local function startTalk(lines, onDone)
    local m = M()
    m.talk = { lines = lines, i = 0, t = 0, onDone = onDone }
    wrenPlay("Interact", false)
    forge.audio.play_at("assets/sounds/chirp_hint.wav", vec3(WREN.x, 1.5, WREN.z), 0.5, 0.8)
end

local function talkNext()
    local m = M()
    local tk = m.talk
    if not tk then return end
    tk.i = tk.i + 1
    local line = tk.lines[tk.i]
    if not line then
        m.talk = nil
        if tk.onDone then tk.onDone() end
        return
    end
    tk.t = 2.0 + #line * 0.045
    forge.hud.say("WREN", line, tk.t)
    forge.log("wren: " .. line)
end

-- One E press on Wren: start a conversation (or step to the next line).
function S.talkToWren()
    local m = M()
    if m.talk then talkNext() return end
    local st = Missions.status("signal")
    if st == 0 then
        Missions.start("signal")
        startTalk(OFFER, nil)
    elseif st == 1 then
        if m.powered then
            startTalk({ "It's humming. The gate is open. Use the transmitter." })
        else
            local n = seatedCount()
            local first = ({ [0] = "Three cells. Ravine, north shed, ore belt.",
                             [1] = "One down. Two to go.", [2] = "One more cell." })[n]
            local missing = {}
            for _, c in ipairs(CELLS) do
                if not Missions.isDone("signal", c.obj) then missing[#missing + 1] = c.obj end
            end
            if #missing == 0 then missing = { "belt" } end
            m.tipIdx = m.tipIdx % #missing + 1
            startTalk({ first, TIPS[missing[m.tipIdx]] })
        end
    else
        startTalk({ "They heard us. Thank you, UNIT-7." })
    end
    talkNext()
end

local function updateWren(dt, pp)
    local m = M()
    -- face the player when near; otherwise watch the arrival path
    local dx, dz = pp.x - WREN.x, pp.z - WREN.z
    local want = (dx * dx + dz * dz < 64) and math.deg((math.atan2 or math.atan)(dx, dz)) or 200
    local turn = (want - m.wrenYaw + 180) % 360 - 180
    local step = 150 * dt
    if turn > step then turn = step elseif turn < -step then turn = -step end
    m.wrenYaw = m.wrenYaw + turn
    forge.scene.setRotation("Wren", vec3(0, m.wrenYaw, 0))
    -- one-shot clips fade back to the idle
    if m.wrenAnim ~= "Idle" then
        local t, len = forge.anim.time("Wren")
        if t and len and t >= len - 0.3 then wrenPlay("Idle", true) end
    end
    local tk = m.talk
    if tk then
        tk.t = tk.t - dt
        if tk.t <= 0 then talkNext() end
    end
end

-- ---- The freight-lift ride (facility -> Surface) -----------------------------
local FL = { x = X, z = -269.65, y0 = 0.1, y1 = 8.6 } -- facility lift centre / top of the ride
local SL = { x = 400.0, z = 36.5, y0 = -3.4, y1 = -0.1 } -- the Surface lift
local function onFreightLift(p)
    return math.abs(p.x - FL.x) < 1.0 and p.z < -267.9 and p.z > -271.5 and p.y < FL.y0 + 1.6
end

local function updateRide(dt, pp)
    local m = M()
    local r = m.ride
    if r.phase == "idle" then
        local d17 = K.doors["Door 17"]
        if d17 and d17.want and onFreightLift(pp) and forge.player.grounded() then
            r.phase, r.t = "close", 1.2
            K.state.ascending = true -- Door 17 shuts behind you
            forge.audio.play("assets/sounds/clunk.wav", 0.9)
            forge.audio.play("assets/sounds/door_hiss.wav", 0.7)
            forge.hud.set_hint("")
            forge.log("freight lift: boarding")
        end
    elseif r.phase == "close" then
        r.t = r.t - dt
        if r.t <= 0 then
            r.phase, r.v = "rise", 0
            forge.camera.shake(0.25)
            forge.audio.play("assets/sounds/power_up.wav", 0.6, 0.7)
            forge.log("freight lift: ascending")
        end
    elseif r.phase == "rise" then
        r.v = math.min(r.v + 1.2 * dt, 1.8)
        r.y = math.min(r.y + r.v * dt, FL.y1)
        forge.scene.setPosition("Freight Lift", vec3(FL.x, r.y, FL.z))
        if HUM.freight then forge.audio.set_volume(HUM.freight, 0.45) end
        if r.y >= FL.y1 - 1.2 then -- start the fade a beat before the top
            r.phase, r.t = "fade", 0
        end
    elseif r.phase == "fade" then
        r.y = math.min(r.y + r.v * dt, FL.y1)
        forge.scene.setPosition("Freight Lift", vec3(FL.x, r.y, FL.z))
        r.t = r.t + dt
        forge.hud.set_fade(math.min(r.t / 0.6, 1.0))
        if r.t >= 0.7 then
            -- the cut: same spot on the Surface lift, facing the valley
            local top = r.y + 0.1
            local rel = vec3(pp.x - FL.x, pp.y - top, pp.z - FL.z)
            r.sy = SL.y0
            forge.scene.setPosition("Srf Lift", vec3(SL.x, SL.y0, SL.z))
            forge.player.teleport(vec3(SL.x + rel.x, SL.y0 + 0.1 + rel.y + 0.05, SL.z + rel.z))
            forge.player.set_yaw(0)
            local dr = K.drone
            dr.station, dr.parkedAt, dr.carrying, dr.deposit = nil, nil, nil, nil
            forge.scene.setPosition(dr.name, vec3(SL.x + 1.0, SL.y0 + 3.0, SL.z - 1.0))
            r.y = FL.y0
            forge.scene.setPosition("Freight Lift", vec3(FL.x, FL.y0, FL.z)) -- back down (warp)
            r.phase, r.t = "arrive", 0
            forge.log("freight lift: arrived on the Surface")
        end
    elseif r.phase == "arrive" then
        r.t = r.t + dt
        forge.hud.set_fade(math.max(1.0 - r.t / 0.9, 0.0))
        r.sy = math.min(r.sy + 1.8 * dt, SL.y1)
        forge.scene.setPosition("Srf Lift", vec3(SL.x, r.sy, SL.z))
        if HUM.freight then forge.audio.set_volume(HUM.freight, r.sy < SL.y1 and 0.35 or 0.0) end
        if r.sy >= SL.y1 and r.t >= 0.9 then
            r.phase = "done"
            forge.hud.set_fade(0)
            forge.audio.play("assets/sounds/clunk.wav", 0.8)
            forge.audio.play("assets/sounds/chime.wav", 0.8)
            m.visited.arrival = true
            K.setCp(CP.arrival, true)
            forge.hud.toast("THE SURFACE", 3.0)
        end
    end
end

-- ---- The arc yard (hazard) + the ledge crate's way home --------------------
local YARD = { x0 = 424.0, x1 = 433.0, z0 = 6.0, z1 = 33.0 }
local COILS = { vec3(424.8, 2.0, 7.2), vec3(432.0, 2.0, 7.2), vec3(432.0, 2.0, 31.6), vec3(424.8, 2.0, 27.0) }
local ZAP = vec3(0.7, 1.0, 3.0)
local function inYard(p) return p.x > YARD.x0 and p.x < YARD.x1 and p.z > YARD.z0 and p.z < YARD.z1 end

local function updateHazards(dt, pp)
    local m = M()
    m.zapCool = m.zapCool - dt
    if inYard(pp) and pp.y < 1.7 and m.zapCool <= 0 then
        m.zapCool = 1.0
        forge.audio.play("assets/sounds/zap.wav", 1.0)
        forge.camera.shake(0.7)
        forge.fx.burst(pp, 36, ZAP)
        forge.log("arc yard: ZAP player")
        K.respawn()
    end
    for _, c in ipairs(K.crates) do
        if c.pos.x > 200 and K.drone.carrying ~= c.name then
            local at = forge.scene.getPosition(c.name)
            if inYard(at) and at.y < 0.75 then
                forge.audio.play_at("assets/sounds/zap.wav", at, 1.0)
                forge.fx.burst(at, 24, ZAP)
                forge.log("arc yard: ZAP " .. c.name)
                K.sendHome(c)
            end
        end
    end
    -- coils: charge whine, then an arc + flash (only within earshot)
    local dx, dz = pp.x - 428.5, pp.z - 19.5
    if dx * dx + dz * dz < 45 * 45 then
        for i, cp in ipairs(COILS) do
            local prev = m.coil[i]
            m.coil[i] = (m.coil[i] + dt) % 3.2
            if prev < 1.0 and m.coil[i] >= 1.0 then
                forge.audio.play_at("assets/sounds/zap.wav", cp, 0.6)
                forge.fx.burst(cp, 12, ZAP)
                m.coilFlash[i] = 0.18
            end
            if m.coilFlash[i] > 0 then
                m.coilFlash[i] = m.coilFlash[i] - dt
                forge.render.light("srf_tesla" .. i, cp, vec3(0.8, 1.6, 6.0), 8.0)
                if m.coilFlash[i] <= 0 then forge.render.clear_light("srf_tesla" .. i) end
            end
        end
    end
    -- the ledge crate: a missed throw falls under the bell -> it comes home
    local at = forge.scene.getPosition(CRATE_S2.name)
    if at.z < -28.3 and at.y < 3.5 and forge.player.holding() ~= CRATE_S2.name then
        m.s2T = m.s2T + dt
        if m.s2T > 1.2 then
            m.s2T = 0
            K.sendHome(CRATE_S2)
            forge.audio.play_at("assets/sounds/beep_ok.wav", CRATE_S2.pos, 0.6)
            forge.fx.burst(CRATE_S2.pos, 10)
        end
    else
        m.s2T = 0
    end
end

-- ---- Areas: respawn points + the hint line ------------------------------
local AREAS = {
    { id = "ledge", test = function(p) return p.x > 405 and p.z > -28 and p.z < -16 and p.y > 4.0 end },
    { id = "belt", test = function(p) return p.x > 405 and p.x < 424 and p.z > 8 and p.z < 30 and p.y > 2.3 end },
    { id = "ravine", test = function(p) return p.x < 366 or (p.x < 398 and p.z < -12) end },
    { id = "arrival", test = function(p) return p.x > 395 and p.x < 405 and p.z > 30 end },
    { id = "relay", test = function(p) return p.x > 384 and p.x < 412 and p.z > -12 and p.z < 22 end },
}
local function areaAt(p)
    for _, a in ipairs(AREAS) do if a.test(p) then return a.id end end
    return nil
end

local function hintFor(area)
    local m = M()
    local st = Missions.status("signal")
    if st == 0 then return "Daylight. Someone is camped by the relay tower." end
    if st == 2 then return "Signal sent." end
    if m.powered then return "The relay is live - use the transmitter inside the gate" end
    if area == "ravine" and not Missions.isDone("signal", "ravine") then
        return "THE RAVINE: something on the far rock glows blue"
    elseif area == "ledge" and not Missions.isDone("signal", "shed") then
        return "THE LEDGE: a locked shed, and a bell across the drop"
    elseif area == "belt" and not Missions.isDone("signal", "belt") then
        return "THE ORE BELT: the belt still runs. Mind where it drops things"
    end
    return "Bring the cells to the junction by the relay gate  (" .. seatedCount() .. "/3)"
end

-- One soft hint per challenge, after ~40 s in the area without progress (a
-- plate/bell/grab/cell change resets it). It points, it does not solve.
local LINGER = {
    ravine = "THE RAVINE: from up high, the glow looks closer",
    ledge = "THE LEDGE: a tap will not carry that far",
    belt = "THE ORE BELT: nothing survives the yard floor. Be there first",
}
local function lingerHint(area, dt)
    local m = M()
    local key = tostring(area) .. tostring(K.plateOn("ps_pad")) .. tostring(K.bellRung("bS"))
        .. tostring(K.plateOn("ps_belt")) .. tostring(forge.player.holding()) .. seatedCount()
        .. tostring(K.drone.station ~= nil)
    if key ~= m.lingerKey then m.lingerKey, m.lingerT = key, 0 end
    m.lingerT = (m.lingerT or 0) + dt
    return m.lingerT >= 40.0 and LINGER[area] or nil
end

local function updateAreas(pp, arrived, dt)
    local m = M()
    local a = areaAt(pp)
    if arrived and a and a ~= m.area then -- respawn points only once really here
        m.area = a
        K.setCp(CP[a], m.visited[a] == true) -- chime on the first visit only
        m.visited[a] = true
    end
    local h = hintFor(m.area)
    if Missions.status("signal") == 1 and not m.powered and not Missions.isDone("signal", ({
        ravine = "ravine", ledge = "shed", belt = "belt" })[m.area or ""] or "-") then
        h = lingerHint(m.area, dt) or h
    end
    if h ~= m.hint then
        m.hint = h
        forge.hud.set_hint(h)
    end
end

-- ---- Hooks called by scene.lua ---------------------------------------------
function S.update(dt, pp)
    local m = M()
    local want = locAt(pp)
    if want ~= m.loc then setLocation(want, false) end
    if m.loc == "surface" and m.blendT < 1.5 then
        m.blendT = math.min(m.blendT + dt, 1.5)
        applyMood(m.blendT / 1.5, m.keyFrom)
    end
    updateRide(dt, pp)
    if m.loc ~= "surface" then return end
    updatePins(dt)
    updateWren(dt, pp)
    updateHazards(dt, pp)
    updateAreas(pp, m.ride.phase == "done", dt)
    if m.powerT > 0 then -- the tower answers: a flicker into full power
        m.powerT = m.powerT - dt
        local k = 0.6 + 0.4 * math.sin(m.powerT * 30.0)
        forge.scene.setEmissive("Srf Beacon", vec3(0.6 * k, 4.0 * k, 1.0 * k))
    end
    if m.winT then
        m.winT = m.winT - dt
        if m.winT <= 0 then
            m.winT = nil
            K.win()
        end
    end
end

function S.onStart()
    build()
    if not HUM.belt_s then HUM.belt_s = forge.audio.loop("assets/sounds/hum_belt.wav", 0.0) end
    if not HUM.freight then HUM.freight = forge.audio.loop("assets/sounds/hum_platform.wav", 0.0) end
    local m = M()
    setPowerLook(m.powered)
    -- two ceiling lamps: one at 5.0 left the 8 x 9 m room near-black (playtest)
    forge.render.light("srf_hh", vec3(400, 3.9, 34.0), vec3(26, 22, 16), 11.0)
    forge.render.light("srf_hh2", vec3(400, 3.9, 38.5), vec3(26, 22, 16), 11.0)
    forge.render.light("srf_camp", vec3(392.6, 1.4, 15.6), vec3(2.0, 1.3, 0.4), 4.0)
    forge.render.light("srf_mesa", vec3(362.7, 6.8, -27.3), vec3(0.4, 1.2, 4.0), 5.0)
    if m.loc == "surface" then setLocation("surface", true) end -- onStart reset the mood
    m.hint = nil
    -- the E verb: Wren and the transmitter
    Interact.add({ id = "wren", pos = vec3(WREN.x, 1.2, WREN.z), radius = 2.8, body = "Wren Body",
        prompt = function() return M().talk and "E  Next" or "E  Talk" end,
        enabled = function() return M().loc == "surface" end,
        use = function() S.talkToWren() end })
    Interact.add({ id = "transmitter", pos = vec3(400, 1.0, 2.2), radius = 2.2, body = "Srf Transmitter",
        prompt = "E  Transmit",
        enabled = function() return M().powered and not M().transmitted end,
        use = function()
            forge.audio.play("assets/sounds/prism.wav", 1.0)
            forge.fx.burst(vec3(400, 1.4, 1.5), 30, vec3(0.4, 2.4, 0.8))
            forge.fx.burst(vec3(400, 31, -3), 40, vec3(0.4, 2.4, 0.8))
            forge.scene.setTexture("Srf Transmitter", "metal_green")
            wrenPlay("Cheer", false)
            forge.hud.say("WREN", "They heard us. Thank you, UNIT-7.", 4.0)
            Missions.complete("signal", "transmit")
        end })
end

-- Continue onto the Surface (resumeAt, cp past the facility's): restore the
-- mission from the save and re-derive the world from it.
function S.resume(cp)
    local m = M()
    m.ride.phase, m.ride.sy = "done", SL.y1
    forge.scene.setPosition("Srf Lift", vec3(SL.x, SL.y1, SL.z))
    K.state.ascending = true
    setLocation("surface", true)
    Missions.load()
    m.area, m.hint = nil, nil
    for _, a in ipairs(AREAS) do m.visited[a.id] = true end
    for _, c in ipairs(CELLS) do
        if Missions.isDone("signal", c.obj) then pinCell(c.name, true) end
    end
    -- the crossings already earned stay earned
    if Missions.isDone("signal", "ravine") then K.state.plates.ps_pad = true end
    if Missions.isDone("signal", "shed") then
        K.state.bells.bS = true
        forge.scene.setTexture("Srf Bell", "metal_green")
    end
    if Missions.status("signal") == 2 then m.transmitted = true end
    forge.log("surface: resumed at checkpoint " .. cp .. " (" .. seatedCount() .. " cells seated)")
end

-- R on the Surface: this area's loose props go home.
function S.resetArea(cp)
    local area = K.checkpoints[cp] and K.checkpoints[cp].area
    local n = 0
    for _, c in ipairs(CELLS) do
        if c.area == area and not M().pinned[c.name] then
            K.sendHome({ name = c.name, pos = c.pos })
            n = n + 1
        end
    end
    if area == "ledge" then
        K.sendHome(CRATE_S2)
        n = n + 1
    end
    forge.log("surface area reset (" .. tostring(area) .. "): " .. n .. " props returned")
    return n
end

-- resetGame: lifts home, cells loose, the relay dark, the facility's mood.
function S.reset()
    local old = M()
    SURFACE_MEM = freshMem()
    local m = M()
    m.loc = old.loc
    K.state.ascending = nil
    forge.scene.setPosition("Freight Lift", vec3(FL.x, FL.y0, FL.z))
    forge.scene.setPosition("Srf Lift", vec3(SL.x, SL.y0, SL.z))
    setPowerLook(false)
    for i = 1, 3 do forge.scene.setEmissive("Srf Cable " .. i, vec3(0, 0, 0)) end
    for i = 1, 4 do forge.render.clear_light("srf_tesla" .. i) end
    forge.scene.setRotation("Srf Bell", vec3(0, 0, 0))
    wrenPlay("Idle", true)
    forge.hud.set_fade(0)
    forge.hud.set_prompt("")
    if HUM.freight then forge.audio.set_volume(HUM.freight, 0.0) end
    setLocation("facility", true)
end

SURFACE = S
