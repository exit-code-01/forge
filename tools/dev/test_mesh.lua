-- tools/dev/test_mesh.lua — static triangle-mesh colliders (ADR-032), by script.
-- cp tools/dev/test_mesh.lua build/_start.lua && bash tools/dev/rundev.sh 30
-- Spawns srf_cliff_slope with collider = "mesh" far from every room and probes
-- it with raycasts, the player capsule and a dynamic crate. Expect 16 PASS.
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

local MODEL = "srf_cliff_slope" -- 7.2 x 6 x 6 m after import scale, base at y = 0
local A = vec3(-300, 100, 300)  -- yaw 0
local B = vec3(-300, 100, 330)  -- yaw 90
local OFF = { -2.0, -1.0, 0.0, 1.0, 2.0 }
local DOWN = vec3(0, -1, 0)

-- surface height above the model base at a local offset, and the body hit
local function height(base, ox, oz)
    local id, dist = forge.physics.raycast(vec3(base.x + ox, base.y + 10, base.z + oz), DOWN, 12)
    if not id then return nil end
    return 10 - dist, id
end

local hA = {}
local lowX, lowZ, lowH -- lowest sampled spot on A: where things are dropped

chk("model is registered", function() return forge.scene.has_model(MODEL) end)
act(function()
    forge.scene.spawn("MT Slope A", A, vec3(1, 1, 1), vec3(3.6, 3, 3), "", false, MODEL, { collider = "mesh" })
    forge.scene.spawn("MT Slope B", B, vec3(1, 1, 1), vec3(3.6, 3, 3), "", false, MODEL,
        { collider = "mesh", yaw = 90 })
end)
wait(0.3)

chk("mesh spawn has a body", function() return forge.scene.bodyId("MT Slope A") ~= 0 end)
chk("rays from above hit the mesh body everywhere", function()
    local want, n = forge.scene.bodyId("MT Slope A"), 0
    for _, ox in ipairs(OFF) do
        hA[ox] = {}
        for _, oz in ipairs(OFF) do
            local h, id = height(A, ox, oz)
            if not h or id ~= want then return false, ox .. "," .. oz end
            hA[ox][oz] = h
            n = n + 1
            if not lowH or h < lowH then lowX, lowZ, lowH = ox, oz, h end
        end
    end
    return n == 25, n
end)
chk("surface follows the triangles, not a box", function()
    local lo, hi = 1e9, -1e9
    for _, ox in ipairs(OFF) do
        for _, oz in ipairs(OFF) do
            lo, hi = math.min(lo, hA[ox][oz]), math.max(hi, hA[ox][oz])
        end
    end
    return hi - lo > 1.0 and lo > -0.05 and hi < 6.05, string.format("%.2f..%.2f", lo, hi)
end)
chk("yaw = 90 turns the collider with the visual", function()
    -- R_y(90): local (x, z) lands at world offset (z, -x)
    local worst, asym = 0, 0
    for _, ox in ipairs(OFF) do
        for _, oz in ipairs(OFF) do
            local h = height(B, oz, -ox)
            if not h then return false, "miss " .. ox .. "," .. oz end
            worst = math.max(worst, math.abs(h - hA[ox][oz]))
            asym = math.max(asym, math.abs(hA[oz][-ox] - hA[ox][oz]))
        end
    end
    -- asym > 0.5: the shape is not 90-degree symmetric, so the match means something
    return worst < 0.08 and asym > 0.5, string.format("err %.3f asym %.2f", worst, asym)
end)

-- the player capsule stands on it
act(function() forge.player.teleport(vec3(A.x + lowX, A.y + lowH + 1.6, A.z + lowZ)) end)
untilC("player lands on the mesh", function() return forge.player.grounded() end, 3)
wait(1.0)
chk("player rests on the surface (no fall-through)", function()
    local p = forge.player.position()
    local feet = p.y - (A.y + lowH)
    return forge.player.grounded() and feet > 0.2 and feet < 1.8, string.format("%.2f above", feet)
end)

-- a dynamic body collides with it (dropped where the player just stood)
act(function()
    forge.player.teleport(vec3(A.x, A.y + 40, A.z - 40))
    forge.scene.spawn("MT Crate", vec3(A.x + lowX, A.y + lowH + 2.0, A.z + lowZ), vec3(0.4, 0.4, 0.4),
        vec3(0.2, 0.2, 0.2), "", true, nil, { friction = 0.9 })
end)
-- the surface is a 45 degree slope: a falling crate can only gain sideways
-- speed by hitting the triangles (a box top would stop it dead)
untilC("dynamic crate lands on the mesh and is deflected downhill", function()
    local p = forge.scene.getPosition("MT Crate")
    local v = forge.physics.velocity(forge.scene.bodyId("MT Crate"))
    return math.sqrt(v.x * v.x + v.z * v.z) > 1.0 and p.y > A.y + lowH - 0.5
end, 2)

-- walking: the same model squashed to 30 % height is a 17 degree ramp (rising
-- toward +z) made of welded triangles; this also covers a non-uniform scale.
local C = vec3(-300, 100, 360)
local startY
act(function()
    forge.scene.spawn("MT Ramp", C, vec3(1, 0.3, 1), vec3(0, 0, 0), "", false, MODEL, { collider = "mesh" })
end)
wait(0.2)
chk("non-uniform scale is baked into the collider", function()
    local lo, hi = height(C, 0, -2.0), height(C, 0, 2.0)
    return lo and hi and math.abs(lo - 0.3) < 0.03 and math.abs(hi - 1.5) < 0.03,
        string.format("%.2f %.2f", lo or -1, hi or -1)
end)
act(function() forge.player.teleport(vec3(C.x, C.y + 0.3 + 1.3, C.z - 2.0)) end)
untilC("player lands on the ramp", function() return forge.player.grounded() end, 3)
wait(0.4)
act(function() startY = forge.player.position().y end)
local walked, airborne = 0, 0
Q[#Q + 1] = { k = "u", timeout = 3, label = "player is carried 3 m up the ramp without snagging", fn = function()
    forge.player.push(vec3(0, 0, 2.5))
    walked = walked + 1
    if not forge.player.grounded() then airborne = airborne + 1 end
    return forge.player.position().z > C.z + 1.0
end }
chk("the climb followed the surface", function()
    local p = forge.player.position()
    local rise = p.y - startY -- 3 m along a 0.3 slope = 0.9 m
    return rise > 0.7 and rise < 1.2 and airborne <= walked * 0.2,
        string.format("rise %.2f, %d/%d frames airborne", rise, airborne, walked)
end)
act(function() forge.scene.destroy("MT Ramp") end)

-- fallbacks and removal
act(function()
    forge.scene.spawn("MT Dyn", vec3(A.x, A.y + 30, A.z + 60), vec3(1, 1, 1), vec3(0.3, 0.3, 0.3), "", true,
        MODEL, { collider = "mesh" })
    forge.scene.spawn("MT Cube", vec3(A.x + 30, A.y, A.z), vec3(1, 1, 1), vec3(0.5, 0.5, 0.5), "", false, nil,
        { collider = "mesh" })
end)
wait(0.2)
chk("dynamic + mesh falls back to a (moving) box", function()
    return forge.scene.bodyId("MT Dyn") ~= 0 and forge.scene.getPosition("MT Dyn").y < A.y + 29.9
end)
chk("model without CPU triangles falls back to a box", function()
    local h, id = height(vec3(A.x + 30, A.y, A.z), 0, 0)
    return h ~= nil and id == forge.scene.bodyId("MT Cube") and math.abs(h - 0.5) < 0.05, h
end)
act(function()
    forge.player.teleport(vec3(A.x, A.y + 40, A.z - 40)) -- off the mesh before it goes
    forge.scene.destroy("MT Slope A")
end)
wait(0.2)
chk("destroy removes the mesh collider", function()
    local h = height(A, 0, 0)
    return h == nil, h
end)
chk("the other mesh is untouched", function() return height(B, 0, 0) ~= nil end)
act(function()
    for _, n in ipairs({ "MT Slope B", "MT Crate", "MT Dyn", "MT Cube" }) do forge.scene.destroy(n) end
    log("DONE")
end)

-- ---- scheduler (same as test_surface.lua)
local qi = 1
DBG.tick = function(dt)
    local guard = 0
    while qi <= #Q and guard < 200 do
        guard = guard + 1
        local st = Q[qi]
        if st.k == "a" then
            local ok, err = pcall(st.fn)
            if not ok then log("FAIL TEST ERROR " .. tostring(err)) end
            qi = qi + 1
        elseif st.k == "w" then
            if not st.left then st.left = st.t end
            st.left = st.left - dt
            if st.left > 0 then return end
            st.left = nil
            qi = qi + 1
            return
        else
            st.e = (st.e or 0) + dt
            local ok, res = pcall(st.fn)
            if ok and res then
                log("PASS " .. st.label .. string.format(" [%.1fs]", st.e))
                qi = qi + 1
            elseif not ok or st.e >= st.timeout then
                log("FAIL " .. st.label .. (ok and " (timeout)" or (" (error " .. tostring(res) .. ")")))
                qi = qi + 1
            else
                return
            end
        end
    end
end
log("mesh sequencer queued: " .. #Q .. " steps")
