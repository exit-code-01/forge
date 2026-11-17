-- tools/dev/shot_spillway.lua - sets up one Spillway view and idles, for screenshots (ADR-034).
-- Usage (prepend the view name as the first line):
--   (echo 'SPL_SHOT = "sluice"'; cat tools/dev/shot_spillway.lua) > build/_start.lua
--   bash tools/dev/rundev.sh 24 build/_shots/spl_sluice.png
-- Views: arrival | sluice | current | turbine | crest. Like test_spillway.lua it teleports and sets
-- positions where a walk or a glove would end; the module's own logic does the rest.
local Q = {}
local function act(fn) Q[#Q + 1] = { k = "a", fn = fn } end
local function wait(sec) Q[#Q + 1] = { k = "w", t = sec } end
local function put(name, x, y, z) act(function() forge.scene.setPosition(name, vec3(x, y, z)) end) end
local function tp(x, y, z, yaw, pitch)
    act(function()
        forge.player.teleport(vec3(x, y, z))
        if yaw then forge.player.set_yaw(yaw, pitch or 0) end
    end)
end
local shot = SPL_SHOT or "arrival"
local SPL = SPILLWAY
wait(1.0)
act(function() DBG.resume(SPL.CP.plaza) end) -- the Spillway's mood and state, as after Continue
wait(1.5)
if shot == "arrival" then
    tp(792.0, 1.95, 29.5, 8, 4)
elseif shot == "sluice" then
    for i, n in ipairs({ "Spl Crate W1", "Spl Crate W2", "Spl Crate W3" }) do put(n, 770.0 + (i - 1) * 1.0, 1.9, -2.0) end
    tp(776.0, 1.95, -4.4, 0, 0)
    wait(7.5)
    tp(774.0, 1.95, -6.6, 0, -14)
elseif shot == "current" then
    put("Spl Crate C1", 800.0, 2.4, -13.0)
    wait(3.0)
    put("Spl Crate C2", 799.0, 2.4, -14.2)
    wait(3.0)
    put("Spl Crate C3", 801.0, 2.4, -14.8)
    tp(800.0, 1.95, -4.6, 0, -20)
    wait(4.0)
elseif shot == "turbine" then
    tp(825.0, 1.95, -8.5, 0, 0)
    wait(2.0)
    act(function() DBG.drone.station = vec3(822.0, 2.8, -20.0); DBG.drone.parkedAt = nil end)
    wait(5.0)
    put("Spl Prism", 822.0, 1.5, -25.5)
    wait(2.0)
    tp(825.0, 1.95, -9.0, 0, -4)
elseif shot == "crest" then
    act(function() SPILLWAY_MEM.crownOpen = true; forge.scene.setPosition("Spl Crown", vec3(762.0, 8.6, -6.75)) end)
    tp(830.0, 9.95, -32.0, 90, -4)
end
wait(30)
local qi = 1
DBG.tick = function(dt)
    local guard = 0
    while qi <= #Q and guard < 100 do
        guard = guard + 1
        local st = Q[qi]
        if st.k == "a" then
            local ok, err = pcall(st.fn)
            if not ok then forge.log("SHOT ERROR " .. tostring(err)) end
            qi = qi + 1
        else
            st.left = (st.left or st.t) - dt
            if st.left > 0 then return end
            qi = qi + 1
            return
        end
    end
end
forge.log("shot sequencer queued: " .. shot)
