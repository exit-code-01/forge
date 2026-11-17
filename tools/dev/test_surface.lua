-- tools/dev/test_surface.lua — the Surface, played by script (ADR-031).
-- cp tools/dev/test_surface.lua build/_start.lua && bash tools/dev/rundev.sh 200
-- Teleports/setPosition move things; the PUZZLE LOGIC runs for real: Door 17,
-- the freight lift, plates (SPARK as weight), the bell, sockets, the belt,
-- SPARK's catch + deposit, the junction, the mission layer and E (Interact).
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
local function vel(name, x, y, z)
    act(function() forge.physics.set_velocity(forge.scene.bodyId(name), vec3(x, y, z)) end)
end
local function pos(name) return forge.scene.getPosition(name) end
local function P() return forge.player.position() end
local S = DBG.state
local SU = SURFACE
local function M() return SURFACE.mem() end
local function near(a, b) return (a - b):length() end
local function fmt(v) return string.format("%.1f %.1f %.1f", v.x, v.y, v.z) end
local function belt()
    for _, b in ipairs(DBG.belts) do if b.id == "bs" then return b end end
end
local SEAT = { vec3(404.2, 1.15, 11.2), vec3(406.0, 1.15, 11.2), vec3(407.8, 1.15, 11.2) }

-- the host's win screen halts scripts: record the call until the last step
local realWin = forge.game.win
local winCalls = 0
forge.game.win = function() winCalls = winCalls + 1 end

local function ride(tag)
    -- R17 solved (as in _test_all): Door 17 opens; board the freight lift
    tp(-3, 1.5, -253.0); wait(0.3)
    put("Battery 17", 1.0, 2.56, -264.0); put("Prism 17", -6.0, 0.36, -258.0)
    untilC(tag .. "Door 17 opens", function() return DBG.doors["Door 17"].want == true end, 4)
    tp(-3, 1.3, -269.6, 0)
    untilC(tag .. "boarding starts the ride", function() return M().ride.phase ~= "idle" end, 3)
    untilC(tag .. "Door 17 shuts behind the lift", function() return DBG.doors["Door 17"].want == false end, 2)
    untilC(tag .. "lift rises with the player", function() return M().ride.phase == "rise" and P().y > 3.0 end, 6)
    untilC(tag .. "arrives on the Surface", function() return M().ride.phase == "done" end, 16)
    chk(tag .. "player on the Surface, on the lift top", function()
        return P().x > 390 and P().y > 0.5 and P().y < 1.5, fmt(P()) end)
    chk(tag .. "location mood is the Surface", function() return M().loc == "surface" end)
    chk(tag .. "facility lift back down", function() return pos("Freight Lift").y < 0.3 end)
    chk(tag .. "SPARK came along", function() return pos("Drone").x > 390, fmt(pos("Drone")) end)
    chk(tag .. "checkpoint = arrival", function() return DBG.cp() == SU.CP.arrival, DBG.cp() end)
    untilC(tag .. "ASCEND complete", function() return Missions.status("ascend") == 2 end, 2)
end

local function talk(tag, wantStatus)
    tp(394.5, 1.0, 15.0, 0); wait(0.3)
    chk(tag .. "E prompt is Wren", function() return Interact.cur ~= nil and Interact.cur.id == "wren" end)
    -- line of sight (O3): a static "Srf " wall between the eye and Wren hides the prompt
    act(function()
        forge.scene.spawn("Srf Test Wall", vec3(394.5, 1.5, 14.0), vec3(1.2, 3.0, 0.2), vec3(0.6, 1.5, 0.1),
            "metal", false)
    end); wait(0.3)
    chk(tag .. "a wall in the way hides the E prompt", function()
        return Interact.cur == nil, Interact.cur and Interact.cur.id end)
    act(function() forge.scene.destroy("Srf Test Wall") end); wait(0.3)
    chk(tag .. "E prompt returns when the wall is gone", function()
        return Interact.cur ~= nil and Interact.cur.id == "wren" end)
    act(function() Interact.press() end); wait(0.2)
    chk(tag .. "Wren talks (SIGNAL " .. wantStatus .. ")", function()
        return M().talk ~= nil and Missions.status("signal") == wantStatus, Missions.status("signal") end)
end

local function ravine(tag)
    -- the pad is visible from the lookout, not from the ground
    tp(379.0, 1.0, -28.5); wait(0.4)
    chk(tag .. "pad hidden from the near ground (standing + jump eye, 5 aim points)", function()
        for _, lift in ipairs({ 0.75, 2.3 }) do -- eye = capsule centre + 0.75; a jump adds ~1.5
            local eye = P() + vec3(0, lift, 0)
            for _, t in ipairs({ vec3(359.8, 4.6, -30.2), vec3(360.6, 4.55, -30.2), vec3(359.0, 4.55, -30.2),
                                 vec3(359.8, 4.55, -29.4), vec3(359.8, 4.55, -31.0) }) do
                local d = t - eye
                local id = forge.physics.raycast(eye, d * (1 / d:length()), d:length() + 0.5)
                if id and forge.scene.bodyName(id) == "Plate ps_pad" then return false, lift end
            end
        end
        return true
    end)
    tp(386.0, 7.0, -28.0); wait(0.5)
    chk(tag .. "pad visible from the lookout", function()
        local eye = P() + vec3(0, 0.75, 0)
        local to = vec3(359.8, 4.65, -30.2)
        local d = to - eye
        local id = forge.physics.raycast(eye, d * (1 / d:length()), 30.0)
        return id ~= nil and forge.scene.bodyName(id) == "Plate ps_pad", id and forge.scene.bodyName(id)
    end)
    chk(tag .. "bridge retracted", function() return pos("Srf Bridge").x < 361 end)
    -- what F does after aiming at the pad: station SPARK on it
    act(function() DBG.drone.station = vec3(360.0, 5.6, -30.0); DBG.drone.parkedAt = nil end)
    untilC(tag .. "SPARK on the far pad presses it", function() return S.plates.ps_pad == true end, 8)
    untilC(tag .. "bridge extends across the ravine", function() return pos("Srf Bridge").x > 371.9 end, 7)
    act(function() DBG.drone.station = nil end)
    tp(372.0, 1.2, -22.0); wait(0.8)
    chk(tag .. "bridge holds the player", function() return P().y > 0.5, P().y end)
    -- soft failure: a cell dropped in the ravine comes home
    put("Cell A", 372.0, -2.0, -30.0); wait(1.6)
    chk(tag .. "fallen cell returns to its spawn", function()
        return near(pos("Cell A"), vec3(359.0, 0.36, -38.0)) < 0.6, fmt(pos("Cell A")) end)
    tp(360.0, 1.0, -36.5); wait(0.3)
    chk(tag .. "far side reached (cp ravine)", function() return DBG.cp() == SU.CP.ravine, DBG.cp() end)
end

local function ledge(tag)
    tp(431.0, 5.4, -26.0, 0); wait(0.4)
    chk(tag .. "cp ledge", function() return DBG.cp() == SU.CP.ledge, DBG.cp() end)
    chk(tag .. "shed door locked", function() return DBG.doors["Srf Shed Door"].want == false end)
    -- a tap (11 m/s) cannot reach the bell
    put("Crate S2", 431.0, 6.4, -28.6); vel("Crate S2", 0, 7.07, -8.43)
    wait(2.2)
    chk(tag .. "tap throw does not ring the bell", function() return S.bells.bS ~= true end)
    untilC(tag .. "missed crate comes home to the ledge", function()
        return near(pos("Crate S2"), vec3(430.0, 4.75, -24.5)) < 0.6 end, 5)
    -- too soft up close: thunk, no ring
    put("Crate S2", 431.0, 7.0, -39.5); vel("Crate S2", 0, 0, -9.0); wait(0.8)
    chk(tag .. "9 m/s hit thunks (no ring)", function() return S.bells.bS ~= true end)
    untilC(tag .. "crate home again", function()
        return near(pos("Crate S2"), vec3(430.0, 4.75, -24.5)) < 0.6 end, 5)
    -- a full charge (25 m/s) from the ledge edge rings it
    put("Crate S2", 431.0, 6.4, -28.6); vel("Crate S2", 0, 4.34, -24.6)
    wait(0.15)
    act(function()
        local v = forge.physics.velocity(forge.scene.bodyId("Crate S2"))
        log("INFO crate S2 after launch " .. fmt(pos("Crate S2")) .. " v " .. fmt(v)
            .. " held=" .. tostring(forge.player.holding()) .. " drone=" .. tostring(DBG.drone.carrying))
    end)
    untilC(tag .. "charged throw rings the cliff bell", function() return S.bells.bS == true end, 2)
    untilC(tag .. "shed door opens", function() return pos("Srf Shed Door").z < -24.7 end, 3)
    tp(440.0, 5.4, -23.0); wait(0.3)
    chk(tag .. "inside the shed with the cell", function() return near(P(), pos("Cell B")) < 2.0 end)
end

local function oreBelt(tag)
    tp(417.0, 3.5, 18.0); wait(0.3)
    chk(tag .. "cp belt", function() return DBG.cp() == SU.CP.belt, DBG.cp() end)
    untilC(tag .. "plate runs the belt", function() return S.plates.ps_belt == true and belt().cur < -1.5 end, 2)
    untilC(tag .. "cell rides off the end and falls", function() return pos("Cell C").y < 6.0 end, 9)
    untilC(tag .. "arc yard zaps it home", function() return pos("Cell C").z > 29.5 and pos("Cell C").y > 8.0 end, 4)
    -- what F does after aiming at the blue drop mark under the belt end
    act(function() DBG.drone.station = vec3(426.0, 0.95, 16.0); DBG.drone.parkedAt = nil end)
    untilC(tag .. "SPARK catches the falling cell", function() return DBG.drone.carrying == "Cell C" end, 12)
    untilC(tag .. "SPARK sets it in the cradle", function() return S.sockets.s_cr == true end, 8)
    tp(415.0, 3.4, 25.0); wait(0.6)
    chk(tag .. "belt stops off the plate", function() return math.abs(belt().cur) < 0.1 end)
    act(function() DBG.drone.station = nil end)
    -- the player walks down: hazard check on the way
    tp(428.0, 1.0, 12.0); wait(0.4)
    chk(tag .. "arc yard zaps the player back", function() return P().x < 424.5 and P().y > 2.0, fmt(P()) end)
end

local function seat(tag, cell, i, objective)
    put(cell, SEAT[i].x + 0.35, SEAT[i].y + 0.3, SEAT[i].z - 0.2)
    untilC(tag .. cell .. " eases into junction seat " .. i, function() return S.sockets["s_j" .. i] == true end, 3)
    untilC(tag .. "objective " .. objective .. " done", function() return Missions.isDone("signal", objective) end, 2)
end

local function finale(tag)
    untilC(tag .. "relay powers up", function() return M().powered == true end, 2)
    chk(tag .. "SPARK celebrates", function() return DBG.drone.celebrateT > 0 end)
    untilC(tag .. "yard gate opens", function() return pos("Srf Gate").y > 3.6 end, 3)
    put("Cell A", 402.0, 1.0, 13.0); wait(0.3)
    chk(tag .. "seated cell stays locked in", function() return near(pos("Cell A"), SEAT[1]) < 0.1 end)
    tp(400.0, 1.0, 4.0, 0); wait(0.3)
    chk(tag .. "E prompt is the transmitter", function()
        return Interact.cur ~= nil and Interact.cur.id == "transmitter" end)
end

-- ======================= PASS 1 =======================
wait(1.0)
local perf = { n = 0, sum = 0, max = 0 }
local function perfLog(label)
    act(function()
        perf.on = false
        log(string.format("PERF %s frame avg %.2f ms, max %.2f ms over %d frames", label,
            perf.sum / math.max(perf.n, 1) * 1000, perf.max * 1000, perf.n))
        perf.n, perf.sum, perf.max = 0, 0, 0
    end)
end
-- facility baseline: the tutorial view, then R17
tp(0, 1.5, 4.0, 0); act(function() perf.on = true end); wait(3.0)
tp(-3, 1.5, -253.0, 0); wait(3.0)
perfLog("facility")
ride("p1 ")
act(function() log("INFO surface entities " .. tostring(SURFACE_ENTITIES)) end)
-- frame time on the Surface (arrival view, then the relay)
act(function() perf.on = true end); wait(4.0)
tp(396.0, 1.0, 22.0, 20); wait(3.0)
perfLog("surface")
talk("p1 ", 1)
wait(0.5)
act(function() Interact.press() end); wait(0.2)
chk("p1 E advances Wren's lines", function() return M().talk ~= nil and M().talk.i == 2, M().talk and M().talk.i end)
chk("p1 SIGNAL tracked on the HUD", function() return Missions.tracked == "signal" end)
ravine("p1 ")
seat("p1 ", "Cell A", 1, "ravine")
chk("p1 relay still dark at 1/3", function() return M().powered == false end)
ledge("p1 ")
seat("p1 ", "Cell B", 2, "shed")
oreBelt("p1 ")
seat("p1 ", "Cell C", 3, "belt")
finale("p1 ")
act(function() Interact.press() end)
untilC("p1 transmitter completes SIGNAL", function() return Missions.status("signal") == 2 end, 2)
untilC("p1 the transmit starts CROSSING and does not end the run (ADR-034)", function() return Missions.status("crossing") == 1 and winCalls == 0 and not DBG.isWon() end, 4)
act(function() log("run time " .. string.format("%.1f", DBG.run.t) .. " best=" .. forge.game.load("best_time")) end)

-- ======================= RESET =======================
act(function() resetGame() end); wait(0.6)
chk("reset: back in the facility", function() return P().x < 50 and M().loc == "facility" end)
chk("reset: SIGNAL idle, ASCEND restarted", function()
    return Missions.status("signal") == 0 and Missions.status("ascend") == 1 and not Missions.isDone("ascend", "storage") end)
chk("reset: relay dark, no cells locked", function() return M().powered == false and next(M().pinned) == nil end)
chk("reset: cells home", function()
    return near(pos("Cell A"), vec3(359.0, 0.36, -38.0)) < 0.5 and near(pos("Cell C"), vec3(426.0, 8.86, 30.2)) < 0.5 end)
chk("reset: lifts home, Door 17 rule restored", function()
    return pos("Freight Lift").y < 0.3 and pos("Srf Lift").y < -3 and S.ascending == nil end)
chk("reset: bridge pad + bell cleared", function() return S.plates.ps_pad ~= true and S.bells.bS ~= true end)
chk("reset: not won", function() return DBG.isWon() == false end)

-- ======================= PASS 2 =======================
ride("p2 ")
talk("p2 ", 1)
ravine("p2 ")
seat("p2 ", "Cell A", 1, "ravine")
ledge("p2 ")
seat("p2 ", "Cell B", 2, "shed")
tp(417.0, 3.5, 22.0); wait(0.4)
chk("p2 cp belt before quitting", function() return DBG.cp() == SU.CP.belt, DBG.cp() end)

-- ============ CONTINUE ON THE SURFACE (a simulated fresh launch) ============
local savedCp
act(function()
    savedCp = DBG.cp()
    -- what a new process has: fresh module memory, props at spawn, a facility mood
    SURFACE.reset()
    for k in pairs(MISSION_MEM) do MISSION_MEM[k] = { st = 0, done = {} } end
    for _, c in ipairs(DBG.crates) do DBG.sendHome(c) end
    DBG.drone.station, DBG.drone.carrying = nil, nil
    S.plates.ps_pad, S.bells.bS = nil, nil
    forge.player.teleport(vec3(0, 1.5, 5.0))
end)
wait(0.4)
chk("boot-sim: memory empty", function() return Missions.status("signal") == 0 and M().loc == "facility" end)
act(function() resumeAt(savedCp) end); wait(0.8)
chk("continue: player at the belt respawn", function()
    return near(P(), DBG.checkpoints[SU.CP.belt].pos) < 1.0, fmt(P()) end)
chk("continue: Surface mood, instantly", function() return M().loc == "surface" and M().blendT >= 1.5 end)
chk("continue: SIGNAL active with 2 cells", function()
    return Missions.status("signal") == 1 and Missions.isDone("signal", "ravine")
       and Missions.isDone("signal", "shed") and not Missions.isDone("signal", "belt") end)
chk("continue: ASCEND re-derived complete", function() return Missions.status("ascend") == 2 end)
chk("continue: cells A+B back in the junction", function()
    return near(pos("Cell A"), SEAT[1]) < 0.2 and near(pos("Cell B"), SEAT[2]) < 0.2,
           fmt(pos("Cell A")) .. " / " .. fmt(pos("Cell B")) end)
untilC("continue: bridge out again", function() return pos("Srf Bridge").x > 371.9 end, 6)
untilC("continue: shed door open again", function() return pos("Srf Shed Door").z < -24.7 end, 3)
chk("continue: relay still dark (2/3)", function() return M().powered == false end)
oreBelt("p2 ")
seat("p2 ", "Cell C", 3, "belt")
finale("p2 ")
act(function() log("PASS-COUNT-MARK") end)
act(function() forge.game.win = realWin; Interact.press() end)
untilC("p2 the transmit starts CROSSING (the run ends on the Spillway now)", function() return Missions.status("crossing") == 1 and not DBG.isWon() end, 4)
act(function() log("DONE") end)

-- ---- scheduler
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
log("surface sequencer queued: " .. #Q .. " steps")
