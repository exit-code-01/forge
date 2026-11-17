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
local function tp(x, y, z) act(function() forge.player.teleport(vec3(x, y, z)) end) end
local function put(name, x, y, z) act(function() forge.scene.setPosition(name, vec3(x, y, z)) end) end
local function pos(name) return forge.scene.getPosition(name) end
local function P() return forge.player.position() end
local S = DBG.state
local function near(a, b) return (a - b):length() end

-- ---- cores
wait(1.0)
for i, c in ipairs(CORE_POS) do tp(c.x, c.y, c.z); wait(0.25) end
chk("cores all " .. #CORE_POS .. " collected (8 facility + 3 Surface)", function() return DBG.cores.n == #CORE_POS, DBG.cores.n end)
chk("cores_best saved", function() return forge.game.load("cores_best") >= 8 end)

-- ---- R12 fix
tp(0, 1.5, -170.9); wait(1.2)
chk("p12 ignores player body", function() return S.plates.p12 ~= true, P().y end)

-- ---- R15
tp(-3, 1.5, -215); wait(0.4)
put("Crate R15", -6.0, 4.5, -214.5); wait(1.5)
chk("impact counted", function() return (DBG.crateTrack["Crate R15"].impacts or 0) >= 1, DBG.crateTrack["Crate R15"].impacts end)
put("Crate R15", 1.0, 0.4, -219.0); wait(3.0)
chk("belt default south: crate rests at backstop", function() return math.abs(pos("Crate R15").z + 216.1) < 0.4, pos("Crate R15").z end)
tp(1.0, 1.3, -219.5); wait(0.3)
local z0
act(function() z0 = P().z end); wait(1.2)
chk("player carried south by belt", function() return P().z - z0 > 1.5, P().z - z0 end)
tp(-6, 1.5, -217.4)
untilC("p15r pressed by player", function() return S.plates.p15r == true end, 3)
untilC("crate rides north through slot onto p15a", function() return S.plates.p15a == true end, 16)
chk("   crate z at p15a", function() return true, pos("Crate R15").z end)
untilC("Gate 15 opens", function() return DBG.doors["Gate 15"].want == true end, 2)
tp(-3, 1.5, -219.0); wait(3.0)
chk("p15a holds after belt reverses", function() return S.plates.p15a == true, pos("Crate R15").z end)
chk("Gate 15 stays open", function() return DBG.doors["Gate 15"].want == true end)
put("Crate R15", -6.0, 0.4, -226.5)
untilC("p15b opens Door 15", function() return DBG.doors["Door 15"].want == true end, 3)
tp(-3, 1.5, -228.5); wait(0.3)

-- ---- R16
tp(-3, 1.5, -233.0); wait(0.8)
chk("lift idle at south dock", function() return math.abs(DBG.lift.z + 237.25) < 0.01 and not DBG.lift.moving end)
put("Crate R16", 0.5, 0.4, -233.5)
untilC("p16s weighed by crate", function() return S.plates.p16s == true end, 3)
tp(-3, 1.2, -237.25)
untilC("lift reaches north dock", function() return DBG.lift.z <= -244.7 end, 9)
chk("player rode with lift", function() return P().z < -243, P().z end)
chk("player on lift (y)", function() return P().y > 0.3, P().y end)
wait(2.2)
untilC("lift heading south again", function() return DBG.lift.z > -242 and DBG.lift.moving end, 6)
local zb
act(function() zb = DBG.lift.z; forge.scene.setPosition("Crate R16", vec3(-6.0, 0.4, -233.0)) end)
wait(0.5)
chk("unpowered lift keeps heading south", function() return DBG.lift.z >= zb, DBG.lift.z end)
untilC("unpowered lift parks at south dock", function() return math.abs(DBG.lift.z + 237.25) < 0.01 end, 9)
wait(1.0)
chk("lift stays parked", function() return not DBG.lift.moving and math.abs(DBG.lift.z + 237.25) < 0.01 end)
tp(-6, -1.0, -240.0)
wait(0.6)
chk("player zapped + respawned", function() return P().y > 0.3 and math.abs(P().x + 3) < 0.1, string.format("%.1f %.1f %.1f", P().x, P().y, P().z) end)
put("Crate R16", -3.0, -1.5, -240.0); wait(0.5)
chk("crate in pit returns home", function() return pos("Crate R16").z > -234, pos("Crate R16").z end)
tp(-6.5, 1.5, -247.9); wait(1.5)
chk("p16n ignores player", function() return S.plates.p16n ~= true end)
put("Crate R16", -6.5, 0.4, -247.9)
untilC("crate on p16n opens Door 16", function() return DBG.doors["Door 16"].want == true end, 3)

-- ---- R17
tp(-3, 1.5, -253.0); wait(0.5)
put("Prism 17", -6.0, 0.36, -258.0); wait(1.2)
chk("prism on marker connects", function() return S.circuit == true end)
chk("Door 17 shut without battery", function() return DBG.doors["Door 17"].want ~= true end)
put("Prism 17", -3.0, 0.36, -258.0); wait(0.8)
chk("prism off marker disconnects", function() return S.circuit == false end)
put("Prism 17", -6.4, 0.36, -258.1)
untilC("forgiveness eases prism onto marker", function() return S.circuit == true end, 4)
chk("   prism x", function() return true, pos("Prism 17").x end)
put("Battery 17", 3.5, 1.05, -207.4); wait(1.2)
chk("socket 14 rejects battery", function() return S.sockets.s14 ~= true end)
put("Battery 17", -8.9, 0.36, -252.6)
put("Prism 17", 3.5, 1.05, -207.4); wait(1.2)
chk("socket 14 rejects prism", function() return S.sockets.s14 ~= true end)
put("Prism 17", -6.0, 0.36, -258.0)
put("Crate R15", 1.0, 0.36, -264.0); wait(1.5)
chk("socket 17 rejects crate", function() return S.sockets.s17 ~= true end)
put("Crate R15", -6.0, 0.4, -216.0)
put("Battery 17", 1.0, 2.56, -264.0)
untilC("battery seats in socket 17", function() return S.sockets.s17 == true end, 3)
untilC("Door 17 opens with circuit + socket", function() return DBG.doors["Door 17"].want == true end, 3)
put("Prism 17", -3.0, 0.36, -256.0); wait(0.8)
chk("Door 17 closes when circuit breaks", function() return DBG.doors["Door 17"].want == false end)
put("Prism 17", -5.0, 0.5, -253.1); wait(0.3)
act(function() forge.physics.set_velocity(forge.scene.bodyId("Prism 17"), vec3(-11, 0, 0)) end)
untilC("Glass 17 shatters", function() return DBG.glasses[#DBG.glasses].broken == true end, 2)
tp(-3, 1.5, -253.0); wait(0.3)
put("Prism 17", -2.0, 0.36, -260.0); put("Battery 17", 2.0, 0.36, -255.0)
act(function() DBG.resetRoom() end); wait(0.3)
chk("room reset returns prism", function() return near(pos("Prism 17"), vec3(0, 0.36, -254)) < 0.2 end)
chk("room reset returns battery", function() return near(pos("Battery 17"), vec3(-8.9, 0.36, -252.6)) < 0.2 end)
put("Battery 17", 1.0, 2.56, -264.0); put("Prism 17", -6.0, 0.36, -258.0); wait(1.5)
chk("Door 17 open again", function() return DBG.doors["Door 17"].want == true end)
-- ---- reset + second pass
act(function() DBG.reset() end); wait(0.6)
chk("reset clears state", function() return S.circuit == false and not S.plates.p15a and DBG.cores.n == 0 and not DBG.isWon() end)
tp(-3, 1.5, -253); wait(0.5)
put("Prism 17", -6.0, 0.36, -258.0); wait(1.0)
chk("pass2 circuit connects", function() return S.circuit == true end)
put("Prism 17", -6.0, 0.36, -255.0); wait(0.6)
chk("pass2 circuit drops", function() return S.circuit == false end)
chk("pass2 Glass 17 exists", function() return forge.scene.exists("Glass 17") end)
tp(-3, 1.5, -215); wait(0.3)
put("Crate R15", 1.0, 0.4, -218.0); wait(2.5)
chk("pass2 belt carries crate south", function() return pos("Crate R15").z > -216.8, pos("Crate R15").z end)
tp(-6, 1.5, -217.4)
untilC("pass2 crate rides north onto p15a", function() return S.plates.p15a == true end, 16)
-- pass 2 finished: re-solve R17 fully, then win (the win screen halts scripts)
put("Battery 17", 1.0, 2.56, -264.0); put("Prism 17", -6.0, 0.36, -258.0); wait(1.5)
chk("pass2 Door 17 opens", function() return DBG.doors["Door 17"].want == true end)
-- ADR-031: the end pad is the freight lift now; the win moved to the Surface
-- (tools/dev/test_surface.lua plays it). Board, ride, arrive, not yet won.
tp(-3, 1.5, -270.5)
untilC("end pad boards the freight lift", function() return SURFACE.mem().ride.phase ~= "idle" end, 3)
untilC("freight lift reaches the Surface", function() return SURFACE.mem().ride.phase == "done" and P().x > 390 end, 16)
chk("arrival is not the win (the run ends on the Surface)", function() return DBG.isWon() == false end)
act(function() log("run time " .. string.format("%.1f", DBG.run.t) .. " best=" .. forge.game.load("best_time")) end)

act(function() log("DONE") end)

-- ---- scheduler
local qi, waitLeft, ucur = 1, 0, nil
DBG.tick = function(dt)
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
            qi = qi + 1
            return
        else -- until
            st.e = (st.e or 0) + dt
            if st.fn() then
                log("PASS " .. st.label .. string.format(" [%.1fs]", st.e))
                qi = qi + 1
            elseif st.e >= st.timeout then
                log("FAIL " .. st.label .. " (timeout)")
                qi = qi + 1
            else
                return
            end
        end
    end
end
