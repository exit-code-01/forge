-- assets/scripts/surface_dress.lua — the Surface's dressing (ADR-031), from
-- docs/SURFACE_ASSETS.md (Kenney Nature/Space/Survival kits, CC0, srf_*).
-- Colliderless visuals over the box terrain; a box collider only where the
-- manifest suggests one AND the spot is walkable (trunks, boulders, crates,
-- machines, the ravine fence). Puzzle sightlines and paths are kept clear:
-- nothing stands between the lookout and the mesa, on the stairs, in the arc
-- yard or in the bell's throwing lane.
-- Included by surface.lua; its build calls SURFACE_DRESS(K) once.

local function lcg(seed) -- deterministic scatter
    local s = seed
    return function(a, b)
        s = (s * 1103515245 + 12345) % 2147483648
        return a + (b - a) * (s / 2147483648)
    end
end

function SURFACE_DRESS(K)
    local n = 0
    local missing = {}
    -- col = { hx, hy, hz, cy } in WORLD axes (centre y above the base), or
    -- "mesh" for a triangle collider from the model itself.
    -- A model that failed to import is SKIPPED (spawn would draw the cube
    -- fallback): the placeholder terrain stays and the gap is logged once.
    local function put(model, x, y, z, yaw, s, col, sy, fill)
        if not forge.scene.has_model(model) then
            missing[model] = (missing[model] or 0) + 1
            return
        end
        n = n + 1
        if col == "mesh" then
            -- ADR-032: the rock's own triangles are the collider, turned with
            -- the visual. Named "Dress Col" so the E prompt's line of sight
            -- (missions.lua) treats it as world geometry.
            forge.scene.spawn("Dress Col " .. n, vec3(x, y, z), vec3(s, sy or s, s), vec3(0, 0, 0), "", false,
                              model, { collider = "mesh", yaw = yaw })
            if fill then forge.scene.setEmissive("Dress Col " .. n, fill) end
            return
        end
        local name = "Dress " .. n
        forge.scene.spawn(name, vec3(x, y, z), vec3(s, sy or s, s), vec3(0, 0, 0), "", false, model)
        if yaw ~= 0 then forge.scene.setRotation(name, vec3(0, yaw, 0)) end
        if col then
            n = n + 1
            forge.scene.spawn("Dress Col " .. n, vec3(x, y + col[4], z), vec3(0.001, 0.001, 0.001),
                              vec3(col[1], col[2], col[3]), "metal", false)
        end
    end

    -- ---- rock skins (tools/gen_terrain.py): faceted meshes that sit just in
    -- front of the box terrain and lean back over the cliff tiers, so the
    -- valley stops reading as slabs. Authored in WORLD space around
    -- (400, 0, 0): spawn there, scale 1, no yaw. No colliders: the boxes
    -- stay the colliders, and below 2.7 m the skins keep within 0.21 m of them.
    local skinned = forge.scene.has_model("srf_terr_cliffs")
    for _, m in ipairs({ "srf_terr_cliffs", "srf_terr_ravine", "srf_terr_outcrops" }) do
        put(m, 400, 0, 0, 0, 1, "mesh", nil, vec3(0.115, 0.103, 0.091)) -- shade fill, as surface.lua
    end

    -- ---- cliff faces: one 6 m wall tile per cell on the innermost tier, a
    -- hand's width proud of the box face (the boxes stay the colliders).
    -- Only the fallback now: the cliff skin covers the same faces.
    for i = 0, (skinned and -1 or 14) do
        local c = 359 + i * 6
        put("srf_cliff_wall", c, 0, -46.75, 0, 1, nil, 1.36)    -- north, faces +z
        put("srf_cliff_wall", c, 0, 46.75, 180, 1, nil, 1.36)   -- south, faces -z
        local z = -42 + i * 6
        put("srf_cliff_wall", 353.25, 0, z, 90, 1, nil, 1.36)   -- west, faces +x
        put("srf_cliff_wall", 446.75, 0, z, -90, 1, nil, 1.36)  -- east, faces -x
    end

    -- ---- trees (trunk colliders where you can walk up to them)
    local TRUNK = { 0.3, 3.0, 0.3, 3.0 }
    for _, t in ipairs({
        { "srf_tree_pine_b", 384, -40, 0, 1.0, false }, { "srf_tree_pine_a", 391, -41, 40, 1.1, false },
        { "srf_tree_pine_b", 399, -40.5, 80, 0.9, false }, { "srf_tree_pine_a", 406, -41, 10, 1.0, false },
        { "srf_tree_pine_a", 413, -39.5, 60, 1.2, true },
        { "srf_tree_pine_b", 358, -14, 0, 1.0, false }, { "srf_tree_pine_a", 361, 4, 30, 1.1, true },
        { "srf_tree_pine_b", 358, 22, 70, 1.2, false }, { "srf_tree_tall", 362, 36, 0, 1.0, true },
        { "srf_tree_oak", 386.5, 9.5, 20, 1.0, true }, { "srf_tree_oak", 383, 27, 110, 1.15, true },
        { "srf_tree_pine_a", 387, 38, 0, 1.0, true }, { "srf_tree_tall", 381, 34, 45, 0.95, false },
        { "srf_tree_pine_b", 411, 39, 15, 1.1, true }, { "srf_tree_oak", 418, 41, 200, 1.0, false },
        { "srf_tree_dead", 427, 2, 30, 1.0, true }, { "srf_tree_dead", 437, 1.5, 140, 0.9, false },
        { "srf_tree_tall", 395, -18, 0, 1.0, true },
    }) do
        put(t[1], t[2], 0, t[3], t[4], t[5], t[6] and TRUNK or nil)
    end

    -- ---- rocks: mesh colliders (ADR-032), you bump into the rock you see
    put("srf_stone_tall_a", 398.5, 0, -33.5, 20, 1.0, "mesh")
    put("srf_rock_large_a", 382, 0, -37, 0, 1.0, "mesh")
    put("srf_rock_large_c", 381.5, 0, 12, 0, 1.0, "mesh")
    put("srf_rock_tall_b", 411.5, 0, 5.5, 30, 1.0, "mesh")
    put("srf_stone_large_b", 390.5, 0, 31, 0, 1.0, "mesh")
    put("srf_rock_large_c", 358.5, 0, 12, 90, 1.0, "mesh")
    put("srf_rock_tall_b", 388.5, 0, -6, 0, 1.0, "mesh")
    put("srf_stone_tall_a", 440, 4.5, -18, 180, 0.8, nil)       -- on the ledge, past the shed
    put("srf_rock_tall_b", 360, 4.5, -32.6, 0, 0.6, nil)        -- mesa top, far corner (not in sight of the pad)
    put("srf_stone_small_c", 384.5, 0, 22, 0, 1.0, nil)
    put("srf_stone_small_c", 416, 0, 4.5, 50, 1.2, nil)
    put("srf_stone_small_c", 400, 0, -38, 10, 1.0, nil)

    -- ---- the relay station and the camp
    put("srf_relay_unit", 405.5, 0, -6.4, 180, 0.8, { 1.2, 0.96, 0.8, 0.96 })
    put("srf_dish", 401.4, 24.5, -4.3, 160, 0.55, nil)
    put("srf_generator", 410.8, 0, 13.6, 90, 0.8, { 0.84, 0.64, 1.12, 0.64 })
    put("srf_barrels", 390.6, 0, 12.4, 30, 1.0, { 0.82, 0.53, 0.74, 0.53 })
    put("srf_barrel", 388.8, 0, 13.6, 0, 1.0, { 0.32, 0.47, 0.32, 0.47 })
    put("srf_sign", 396.2, 0, 18.6, 200, 1.0, nil)
    put("srf_crate_large", 393.0, 0, 18.4, 90, 1.0, { 0.75, 0.38, 0.38, 0.38 })
    put("srf_log_large", 385.5, 0, 16.5, 70, 1.0, nil)
    put("srf_stump_old", 384, 0, 5.5, 0, 1.0, nil)
    -- the head-house yard: freight pods and a crate stack
    put("srf_container", 407.8, 0, 37.5, 0, 1.0, { 0.88, 1.4, 1.75, 1.4 })
    put("srf_container", 411.2, 0, 34.8, 90, 1.0, { 1.75, 1.4, 0.88, 1.4 })
    put("srf_crate", 393.2, 0, 34.5, 15, 1.0, { 0.38, 0.38, 0.38, 0.38 })
    put("srf_crate", 393.0, 0.75, 34.4, 40, 1.0, nil)
    put("srf_stump_tall", 418.5, 0, 30, 0, 1.0, { 0.54, 1.0, 0.56, 1.0 })
    -- a fence along the ravine's near edge, either side of the bridge landing
    for _, z in ipairs({ -28.25, -26.75, -25.25, -18.75, -17.25, -15.75 }) do
        put("srf_fence", 378.6, 0, z, 90, 1.0, { 0.1, 0.78, 0.75, 0.78 })
    end
    -- the ore shelf: a mast and a generator up where the belt starts
    put("srf_mast", 438, 6.0, 30, 0, 1.0, nil)
    put("srf_generator", 439.5, 6.0, 22, 0, 0.8, nil)

    -- ---- ground cover: clustered bushes, tufts and plants (no colliders)
    local R = lcg(31)
    local COVER = { "srf_bush_a", "srf_bush_large", "srf_bush_detailed", "srf_grass_tuft",
                    "srf_grass_tuft", "srf_grass_leafs", "srf_plant_flat" }
    for _, c in ipairs({ -- cluster centre x, z, radius, count
        { 383, -40, 4, 6 }, { 402, -36, 4, 5 }, { 412, -36, 3, 4 }, { 360, -4, 3, 5 },
        { 360, 28, 3, 5 }, { 384, 31, 4, 6 }, { 414, 37, 4, 5 }, { 386, 2, 3, 5 },
        { 428, 40, 3, 4 }, { 435, -6, 4, 5 }, { 389, -18, 3, 4 }, { 432, -19, 3, 3 },
    }) do
        for _ = 1, c[4] do
            local a, r = R(0, 6.283), R(0.5, c[3])
            local m = COVER[math.floor(R(1, #COVER + 0.999))]
            local y = (c[2] < -16 and c[2] > -28 and c[1] > 418) and 4.5 or 0 -- the ledge top
            put(m, c[1] + math.cos(a) * r, y, c[2] + math.sin(a) * r, R(0, 360), R(0.8, 1.25), nil)
        end
    end
    local list, skipped = {}, 0
    for m, k in pairs(missing) do
        list[#list + 1] = m
        skipped = skipped + k
    end
    table.sort(list)
    if #list > 0 then
        forge.log("surface dressing: " .. skipped .. " placements skipped, models not loaded: "
                  .. table.concat(list, ", "))
    end
    forge.log("surface dressing: " .. n .. " entities placed")
    return n
end
