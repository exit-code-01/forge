-- tools/dev/test_spillway.lua - the Spillway, played by script (ADR-034).
-- cp tools/dev/test_spillway.lua build/_start.lua && bash tools/dev/rundev.sh 260
-- WHAT IS REAL: the puzzle logic and the physics. Crates fall, float, lodge and
-- stand under the player; the plate weighs real bodies; the beam is real
-- raycasts against the real prism, receiver and walls; water takes the player
-- by the same feet-below-surface rule; the E verb, the missions and the kit's
-- sockets and doors run as in play.
-- WHAT IS FAKED (the sequencer cannot press keys): the player is teleported
-- (forge.player.teleport) to where a walk or a jump would end, and held props
-- are carried by forge.scene.setPosition (keys to the winch seats, a prism to
-- its marker, crates onto the plate); the glove, F (SPARK is sent by setting
-- drone.station, as the F handler does), the freight lift and the Surface's
-- cell fetches are not re-played here (test_surface covers them); two
-- timers are shortened by writing the module's own counters (sluice hold, the
-- hint clock). The win screen is counted, not raised.
local Q = {}
local function log(s) forge.log("T> " .. s) end
local function act(fn) Q[#Q + 1] = { k = "a", fn = fn } end
local function wait(sec) Q[#Q + 1] = { k = "w", t = sec } end
local function untilC(label, fn, timeout)
    Q[#Q + 1] = { k = "u", fn = fn, timeout = timeout, label = label }
end
local function chk(label, fn)
    act(function()
        local ok, extra = fn()
        log((ok and "PASS " or "FAIL ") .. label .. (extra ~= nil and (" [" .. tostring(extra) .. "]") or ""))
    end)
end
local function tp(x, y, z, yaw)
    act(function()
        forge.player.teleport(vec3(x, y, z))
        if yaw then forge.player.set_yaw(yaw) end
    end)
end
local function put(name, x, y, z) act(function() forge.scene.setPosition(name, vec3(x, y, z)) end) end
local function pos(name) return forge.scene.getPosition(name) end
local function P() return forge.player.position() end
local S = DBG.state
local SU = SURFACE
local SPL = SPILLWAY
local function M() return SPILLWAY.mem() end
local function fmt(v) return string.format("%.1f %.1f %.1f", v.x, v.y, v.z) end
local function near(a, b) return (a - b):length() end
local function topAt(x, z) -- real collider height under (x, z), by a ray from 14 m up
    local id, d = forge.physics.raycast(vec3(x, 14.0, z), vec3(0, -1, 0), 24.0)
    return id and (14.0 - d) or nil
end
local SEAT = { vec3(404.2, 1.15, 11.2), vec3(406.0, 1.15, 11.2), vec3(407.8, 1.15, 11.2) }
local KEYSEAT = { vec3(761.6, 2.15, -3.6), vec3(761.6, 2.15, -2.4), vec3(761.6, 2.15, -1.2) }

-- the win screen halts scripts: count the call instead
local realWin = forge.game.win
local winCalls = 0
forge.game.win = function() winCalls = winCalls + 1 end

local perf = { n = 0, sum = 0, max = 0 }
local function perfLog(label)
    act(function()
        perf.on = false
        log(string.format("PERF %s frame avg %.2f ms, max %.2f ms over %d frames", label,
            perf.sum / math.max(perf.n, 1) * 1000, perf.max * 1000, perf.n))
        perf.n, perf.sum, perf.max = 0, 0, 0
    end)
end

-- ======================= A. SIGNAL ends in CROSSING, not in a win =======================
wait(1.0)
chk("a0 Spillway module published, 14 data cores in the run", function()
    return SPILLWAY ~= nil and #CORE_POS == 14, #CORE_POS end)
chk("a0 six Spillway checkpoints appended after the Surface's", function()
    return SPL.CP.land == SU.CP.belt + 1 and SPL.CP.crest == SPL.CP.land + 5, SPL.CP.land end)
chk("a0 CROSSING defined and idle", function() return Missions.byId.crossing ~= nil and Missions.status("crossing") == 0 end)
tp(404.0, 1.0, 14.0, 0); wait(1.0)
act(function() Missions.start("signal", true) end)
for i, c in ipairs({ "Cell A", "Cell B", "Cell C" }) do
    act(function() forge.scene.setPosition(c, vec3(SEAT[i].x + 0.35, SEAT[i].y + 0.3, SEAT[i].z - 0.2)) end)
    untilC("a1 " .. c .. " seats in the junction", function() return S.sockets["s_j" .. i] == true end, 4)
end
untilC("a1 relay powered", function() return SU.mem().powered == true end, 3)
tp(400.0, 1.0, 4.0, 0); wait(0.5)
chk("a1 E prompt is the transmitter", function() return Interact.cur ~= nil and Interact.cur.id == "transmitter" end)
act(function() Interact.press() end)
untilC("a1 SIGNAL complete", function() return Missions.status("signal") == 2 end, 3)
untilC("a1 CROSSING starts", function() return Missions.status("crossing") == 1 end, 3)
chk("a1 CROSSING is the tracked mission", function() return Missions.tracked == "crossing", Missions.tracked end)
wait(4.0)
chk("a1 the transmit no longer ends the run", function() return winCalls == 0 and not DBG.isWon(), winCalls end)

-- ======================= B. the rail cart ride =======================
tp(406.2, 1.4, 4.5, 0); wait(0.5)
chk("b1 E prompt is the rail cart", function() return Interact.cur ~= nil and Interact.cur.id == "spl_cart" end)
act(function() Interact.press() end)
untilC("b1 the cart starts (fade)", function() return M().ride.phase == "fade" end, 2)
untilC("b1 teleport to the Spillway", function() return P().x > 700 end, 4)
chk("b1 landing on the rail car", function() return P().y > 1.2 and P().y < 3.0 and math.abs(P().z - 31.5) < 2, fmt(P()) end)
chk("b1 SPARK came along", function() return pos("Drone").x > 700, fmt(pos("Drone")) end)
untilC("b1 the car reaches the station", function() return M().ride.phase == "done" end, 8)
chk("b1 checkpoint = landing", function() return DBG.cp() == SPL.CP.land, DBG.cp() end)
chk("b1 on the Spillway (module active)", function() return M().here == true and M().arrived == true end)
-- frame time on the Spillway: the landing view, then the plaza facing the dam
act(function() perf.on = true end); wait(4.0)
tp(800.0, 1.95, 14.0, 0); wait(3.0)
perfLog("spillway")

-- ======================= C. geometry that matters (real colliders) =======================
chk("c0 plaza floor at y = 1.0", function() local t = topAt(800, 14); return t and math.abs(t - 1.0) < 0.03, t end)
chk("c0 stone S0 top 0.3", function() local t = topAt(774.2, -10.8); return t and math.abs(t - 0.3) < 0.03, t end)
chk("c0 stone S1 top -0.5", function() local t = topAt(774.6, -13.4); return t and math.abs(t + 0.5) < 0.03, t end)
chk("c0 stone S2 top -1.3", function() local t = topAt(773.8, -16.0); return t and math.abs(t + 1.3) < 0.03, t end)
chk("c0 stone S3 top -2.2", function() local t = topAt(774.2, -18.6); return t and math.abs(t + 2.2) < 0.03, t end)
chk("c0 chamber top step reaches the shore (y 1.0)", function() local t = topAt(774.0, -23.75); return t and math.abs(t - 1.0) < 0.03, t end)
chk("c0 chamber floor under the water (-4)", function() local t = topAt(768.0, -15.0); return t and math.abs(t + 4.0) < 0.03, t end)
chk("c0 gorge platform y = 1.0", function() local t = topAt(800.0, -20.0); return t and math.abs(t - 1.0) < 0.03, t end)
chk("c0 gorge floor (-4)", function() local t = topAt(797.0, -10.0); return t and math.abs(t + 4.0) < 0.03, t end)
chk("c0 crest at y = 9", function() local t = topAt(800.0, -32.0); return t and math.abs(t - 9.0) < 0.03, t end)
chk("c0 stairs landing at y = 9", function() local t = topAt(762.0, -23.4); return t and math.abs(t - 9.0) < 0.05, t end)
chk("c0 first stair step 0.3 above the plaza", function() local t = topAt(762.0, -7.2); return t and math.abs(t - 1.296) < 0.05, t end)

-- ======================= D. THE SLUICE (west) =======================
tp(776.0, 1.95, -4.4, 0); wait(0.8)
chk("d1 checkpoint = west", function() return DBG.cp() == SPL.CP.w, DBG.cp() end)
chk("d1 water at rest (0)", function() return math.abs(M().sl.level) < 0.01, M().sl.level end)
chk("d1 plate reads 0 with nothing on it", function() return SPL.plateLoad(P()) == 0 end)
-- the hint clock (shortened by writing the module's counter): vague first, plainer later, never before the clock
chk("d1 no hint before the clock runs", function() return (M().hintLv.w or 0) == 0 end)
act(function() M().noProg.w = 39.8 end)
untilC("d1 the first hint arrives after ~40 s without progress", function() return M().hintLv.w == 1 end, 3)
act(function() M().noProg.w = 99.8 end)
untilC("d1 a plainer second hint follows", function() return M().hintLv.w == 2 end, 3)
-- one weight: the player alone drops the water a notch and nothing more
tp(771.0, 2.1, -2.0, 0); wait(0.6)
chk("d1 the player is one weight", function() return SPL.plateLoad(P()) == 1, SPL.plateLoad(P()) end)
untilC("d1 water drops to -0.7 (one notch)", function() return M().sl.level <= -0.69 and M().sl.level > -0.8 end, 4)
wait(2.0)
chk("d1 the player alone never opens the sluice (still -0.7, not latched)", function()
    return M().sl.level > -0.8 and not M().sl.latched, M().sl.level end)
tp(776.0, 1.95, -4.4, 0)
untilC("d1 water returns when the plate is left", function() return M().sl.level > -0.05 end, 6)
-- three crates (the player stays off the plate)
for i, n in ipairs({ "Spl Crate W1", "Spl Crate W2", "Spl Crate W3" }) do put(n, 770.0 + (i - 1) * 1.0, 1.9, -2.0) end
untilC("d2 three crates read as three weights", function() return SPL.plateLoad(P()) == 3 end, 4)
untilC("d2 the sluice latches and the water falls to -2.8", function()
    return M().sl.latched and M().sl.level <= -2.78 end, 12)
chk("d2 the water slab followed", function() return math.abs(pos("Spl Water W").y - (M().sl.level - 0.05)) < 0.01, pos("Spl Water W").y end)
tp(774.2, -2.2 + 0.95, -18.6, 0); wait(0.8)
chk("d2 standing on the last stone, drained: not drowned", function() return P().y < -0.9 and P().y > -1.6 and math.abs(P().z + 18.6) < 0.8, fmt(P()) end)
tp(778.4, -1.3 + 0.95, -16.0, 0); wait(0.5)
chk("d2 core A (the side stone) collected", function() return DBG.cores.got[SPL.coreIdx[1]] == true end)
tp(774.0, 1.95, -24.6, 0); wait(0.5)
chk("d2 the key stands at the far end", function() return near(pos("Spl Key W"), vec3(774.0, 2.06, -26.0)) < 0.6, fmt(pos("Spl Key W")) end)
-- the crates leave: the latch holds while the chain runs
for i, n in ipairs({ "Spl Crate W1", "Spl Crate W2", "Spl Crate W3" }) do put(n, 766.8, 1.5, -6.4 + 1.6 * i) end
wait(4.0)
chk("d3 the sluice holds open after the plate is left", function() return M().sl.latched and M().sl.level < -2.7 end)
act(function() log("INFO sluice hold timer shortened from " .. string.format("%.0f", M().sl.hold) .. " s (the module's own counter)"); M().sl.hold = 0.4 end)
untilC("d3 the latch lets go", function() return not M().sl.latched end, 3)
tp(774.6, -0.5 + 0.95, -13.4, 0) -- stand on stone S1 while the water comes back
untilC("d3 deep water returns the player to the west checkpoint", function()
    return P().x > 770 and P().x < 782 and P().z > -8 and P().y > 1.5 end, 12)
chk("d3 the water is back at rest", function() return M().sl.level > -0.3, M().sl.level end)
-- player + two crates: also three weights (the other solution)
put("Spl Crate W1", 770.4, 1.9, -2.0); put("Spl Crate W2", 771.6, 1.9, -2.0); wait(1.0)
tp(771.0, 2.1, -3.0, 0)
untilC("d4 player + 2 crates latch the sluice", function() return M().sl.latched == true end, 5)
tp(776.0, 1.95, -4.4, 0)
untilC("d4 ...and it stays open with the player off the plate", function() return M().sl.latched and M().sl.level < -2.0 end, 8)
act(function() M().sl.hold = 0.2 end)
put("Spl Crate W1", 766.8, 1.5, -4.8); put("Spl Crate W2", 766.8, 1.5, -3.2)
untilC("d4 sluice closes again", function() return not M().sl.latched and M().sl.level > -0.1 end, 14)

-- ======================= E. THE CURRENT (centre) =======================
tp(800.0, 1.95, -3.2, 0); wait(0.8)
chk("e1 checkpoint = centre", function() return DBG.cp() == SPL.CP.c, DBG.cp() end)
-- the current pushes whoever is above the water (a real fall from 4 m, drift measured)
act(function() forge.player.teleport(vec3(797.0, 3.0, -12.0)) end)
wait(0.35)
chk("e1 the current pushes the player toward the drop", function() return P().z - (-12.0) > 0.3, fmt(P()) end)
untilC("e1 deep water returns the player to the centre checkpoint", function()
    return P().z > -6 and P().y > 1.5 end, 6)
chk("e1 the player lost nothing but a few seconds", function() return DBG.cp() == SPL.CP.c end)
-- throw crates in: they float, lodge in a row
put("Spl Crate C1", 800.0, 2.4, -13.0)
untilC("e2 crate 1 floats and lodges at the grate (slot 1)", function() return M().lane.lodged["Spl Crate C1"] == 1 end, 12)
chk("e2 it floats half-submerged", function() return math.abs(pos("Spl Crate C1").y) < 0.2, fmt(pos("Spl Crate C1")) end)
put("Spl Crate C2", 799.0, 2.4, -14.2)
untilC("e2 crate 2 lodges in slot 2", function() return M().lane.lodged["Spl Crate C2"] == 2 end, 14)
put("Spl Crate C3", 801.0, 2.4, -14.8)
untilC("e2 crate 3 lodges in slot 3", function() return M().lane.lodged["Spl Crate C3"] == 3 end, 16)
chk("e2 three stepping stones in a row", function() return SPL.lodgedCount() == 3, SPL.lodgedCount() end)
-- a 4th crate has nowhere to lodge: round the row and over the drop, then home
put("Spl Crate W1", 797.0, 2.4, -14.5)
untilC("e3 a spare crate goes over the drop and comes home", function()
    return near(pos("Spl Crate W1"), vec3(766.8, 1.36, -4.8)) < 1.0 end, 16)
-- stand on each lodged crate (a real body under a real player)
for i, z in ipairs({ -7.7, -9.9, -12.1 }) do
    tp(800.0, 0.35 + 0.97, z, 0); wait(0.8)
    chk("e4 stands on lodged crate " .. i, function() return P().y > 1.0 and P().y < 1.8 and math.abs(P().z - z) < 1.2, fmt(P()) end)
end
tp(800.0, 1.95, -17.0, 0); wait(0.6)
chk("e4 the far platform: key C waits at the upstream end", function()
    return P().y > 1.5 and near(pos("Spl Key C"), vec3(800.0, 2.06, -21.0)) < 0.6, fmt(P()) end)
-- the glove takes a crate off its slot: the slot opens again
act(function() forge.scene.setPosition("Spl Crate C1", vec3(796.0, 1.5, -4.4)) end)
untilC("e4 a crate taken away frees its slot", function() return SPL.lodgedCount() == 2 end, 4)
put("Spl Crate C1", 800.0, 2.4, -13.0)
untilC("e4 and it lodges again", function() return SPL.lodgedCount() == 3 end, 14)

-- ======================= F. THE TURBINE HALL (east) =======================
tp(825.0, 1.95, -8.5, 0); wait(1.5)
chk("f1 checkpoint = east", function() return DBG.cp() == SPL.CP.e, DBG.cp() end)
chk("f1 the hall is dark (mood blended)", function() return M().hallK > 0.9, M().hallK end)
act(function() DBG.drone.station = vec3(826.0, 2.6, -24.0); DBG.drone.parkedAt = nil end) -- what F does
untilC("f1 SPARK flies ahead into the hall and lights it", function() return M().sparkLit == true and pos("Drone").z < -12 end, 10)
act(function() DBG.drone.station = nil end)
chk("f1 the beam starts at the emitter and no prism turns it", function()
    return M().tb.on == false and M().tb.prismHit ~= true end)
put("Spl Prism", 823.0, 1.5, -25.5); wait(0.8) -- on the beam, but a metre off the marker
chk("f2 a prism on the beam but off the marker does not connect", function()
    return M().tb.prismHit == true and M().tb.connected == false end)
put("Spl Prism", 822.0, 1.5, -25.5)
untilC("f2 on the marker the beam reaches the receiver", function() return M().tb.connected == true end, 4)
untilC("f2 the turbine restarts", function() return M().tb.on == true end, 4)
untilC("f2 the cage door lifts", function() return DBG.doors["Spl Cage Door"].want == true and pos("Spl Cage Door").y > 5.4 end, 6)
untilC("f2 the runner spins", function() return M().tb.omega > 30 end, 4)
tp(835.5, 1.95, -17.0, 0); wait(0.5)
chk("f2 the cage is open: key E reachable", function() return near(pos("Spl Key E"), vec3(837.0, 2.06, -17.0)) < 0.6 end)
tp(826.0, -0.2, -17.0, 0)
untilC("f3 the bay's water returns the player to the east checkpoint", function()
    return P().z > -10 and P().y > 1.5 end, 6)
tp(815.6, 1.9, -27.2, 0); wait(0.5)
chk("f3 core C (the dark corner) collected", function() return DBG.cores.got[SPL.coreIdx[3]] == true end)

-- ======================= G. the keys, the crown winch, the gate =======================
tp(765.0, 1.95, -4.0, 0); wait(0.5)
chk("g1 the crown gate is down and shuts the stairs", function() return pos("Spl Crown").y < 3.0 and M().crownOpen == false end)
for i, k in ipairs({ "Spl Key W", "Spl Key C", "Spl Key E" }) do
    act(function() forge.scene.setPosition(k, vec3(KEYSEAT[i].x + 0.3, KEYSEAT[i].y + 0.3, KEYSEAT[i].z - 0.15)) end)
    untilC("g1 " .. k .. " eases into the winch", function() return SPL.seatedCount() >= i end, 5)
    if i < 3 then chk("g1 the crown gate stays down at " .. i .. "/3", function() return M().crownOpen == false end) end
end
untilC("g1 sluice, current and turbine objectives done", function()
    return Missions.isDone("crossing", "sluice") and Missions.isDone("crossing", "current")
       and Missions.isDone("crossing", "turbine") end, 3)
untilC("g1 the crown gate lifts", function() return M().crownOpen == true and pos("Spl Crown").y > 8.0 end, 8)
chk("g1 CROWN objective done", function() return Missions.isDone("crossing", "crown") end)
chk("g1 a seated key cannot be taken back", function() return near(pos("Spl Key W"), KEYSEAT[1]) < 0.15 or near(pos("Spl Key W"), KEYSEAT[2]) < 0.15 or near(pos("Spl Key W"), KEYSEAT[3]) < 0.15 end)
tp(762.0, 3.0, -8.5, 0); wait(0.5)
chk("g2 the way to the stairs is open (the gate is up)", function() return P().y > 1.5 and pos("Spl Crown").y > 8.0 end)
tp(762.0, 9.95, -26.0, 0); wait(0.8)
chk("g2 the stairs' landing: a real floor at the crest", function() return P().y > 9.3 and P().y < 10.6, fmt(P()) end)
chk("g2 checkpoint = crest", function() return DBG.cp() == SPL.CP.crest, DBG.cp() end)
for _, x in ipairs({ 790.0, 820.0, 846.0 }) do
    tp(x, 9.95, -32.5, 90); wait(0.4)
    chk("g2 the crest holds the player at x " .. x, function() return P().y > 9.3 and P().y < 10.6, fmt(P()) end)
end

-- ======================= H. hints, cores, the caravan, the win =======================
tp(838.0, 9.95, -32.0, 90); wait(0.5)
tp(849.2, 9.95, -31.5, 90); wait(0.6)
chk("h2 E prompt is Wren at the caravan", function() return Interact.cur ~= nil and Interact.cur.id == "caravan" end)
chk("h2 the Spillway hint line names the crest", function() return M().area == "crest" end)
chk("h2 two data cores found so far in the Spillway", function() return DBG.cores.got[SPL.coreIdx[1]] and DBG.cores.got[SPL.coreIdx[3]] end)
tp(761.3, 3.7, 34.5, 0); wait(0.5)
chk("h3 core B (the stepped hide) collected", function() return DBG.cores.got[SPL.coreIdx[2]] == true end)
chk("h3 CROSSING's optional core objective done", function() return Missions.isDone("crossing", "cores") end)
tp(849.2, 9.95, -31.5, 90); wait(0.6)
act(function() Interact.press() end)
untilC("h4 CROSSING complete", function() return Missions.status("crossing") == 2 end, 3)
chk("h4 Wren talks", function() return M().talk ~= nil end)
untilC("h4 the run ends once (win screen)", function() return winCalls == 1 end, 8)
act(function() forge.game.win = realWin end)

-- ======================= I. Continue lands on a Spillway checkpoint =======================
act(function()
    -- lose the keys' places and the gate, as a fresh process would, then Continue at the crest
    SPILLWAY_MEM.pinned, SPILLWAY_MEM.crownOpen = {}, false
    forge.scene.setPosition("Spl Key W", vec3(774.0, 2.06, -26.0))
    DBG.resume(SPL.CP.crest)
end)
wait(1.0)
chk("i1 Continue: the player is at the crest checkpoint", function() return P().y > 9.3 and math.abs(P().x - 762.0) < 3, fmt(P()) end)
chk("i1 Continue: keys re-seated from the saved mission", function() return SPL.seatedCount() == 3, SPL.seatedCount() end)
chk("i1 Continue: the crown gate is open again", function() return M().crownOpen == true end)
chk("i1 Continue: the module is active (no stale Surface state)", function() return M().here and M().arrived end)
act(function() log("DONE") end)

-- ---- scheduler (the same sequencer as test_surface)
local qi, waitLeft = 1, 0
DBG.tick = function(dt)
    if perf.on then
        perf.n, perf.sum = perf.n + 1, perf.sum + dt
        if dt > perf.max then perf.max = dt end
    end
    local guard = 0
    while qi <= #Q and guard < 200 do
        guard = guard + 1
        local st = Q[qi]
        if st.k == "a" then
            local ok, err = pcall(st.fn)
            if not ok then log("TEST ERROR " .. tostring(err)) end
            qi = qi + 1
        elseif st.k == "w" then
            if not st.left then st.left = st.t end
            st.left = st.left - dt
            if st.left > 0 then return end
            st.left = nil
            qi = qi + 1
            return
        else -- until
            st.e = (st.e or 0) + dt
            local ok, res = pcall(st.fn)
            if ok and res then
                log("PASS " .. st.label .. string.format(" [%.1fs]", st.e))
                st.e = nil
                qi = qi + 1
            elseif not ok or st.e >= st.timeout then
                log("FAIL " .. st.label .. (ok and " (timeout)" or (" (error " .. tostring(res) .. ")")))
                st.e = nil
                qi = qi + 1
            else
                return
            end
        end
    end
end
log("spillway sequencer queued: " .. #Q .. " steps")
