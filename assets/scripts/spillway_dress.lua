-- assets/scripts/spillway_dress.lua - the Spillway's dressing (Location 3, ADR-034).
-- Reuses the Surface's CC0 Kenney props (srf_*, tools/srf_build.py) and the
-- three generated spl_* models (tools/gen_spillway.py). Everything is
-- colliderless unless a box or "mesh" collider is asked for, and a model that
-- failed to import is SKIPPED (the placeholder boxes in spillway.lua stay).
-- Included by spillway.lua; its build calls SPILLWAY_DRESS(K) once.
-- Puzzle lanes stay clear: nothing stands in the bank strips beside the three
-- sub-areas, the plate square, the gorge lip or the hall door.

local function lcg(seed) -- deterministic scatter
    local s = seed
    return function(a, b)
        s = (s * 1103515245 + 12345) % 2147483648
        return a + (b - a) * (s / 2147483648)
    end
end

function SPILLWAY_DRESS(K)
    local n = 0
    local missing = {}
    local GY = 1.0
    local function rough(model)
        if model:find("rock") or model:find("stone") or model:find("cliff") then return 0.9 end
        if model:find("spl_dam") then return 0.85 end
    end
    -- col = { hx, hy, hz, cy } world axes, "mesh" for the model's own triangles
    local function put(model, x, y, z, yaw, s, col, sy)
        if not forge.scene.has_model(model) then
            missing[model] = (missing[model] or 0) + 1
            return
        end
        n = n + 1
        if col == "mesh" then
            forge.scene.spawn("Dress Col spl " .. n, vec3(x, y, z), vec3(s, sy or s, s), vec3(0, 0, 0), "", false,
                              model, { collider = "mesh", yaw = yaw, friction = 0.5, roughness = rough(model) })
            return
        end
        local name = "Dress spl " .. n
        forge.scene.spawn(name, vec3(x, y, z), vec3(s, sy or s, s), vec3(0, 0, 0), "", false, model,
                          rough(model) and { roughness = rough(model) } or nil)
        if yaw ~= 0 then forge.scene.setRotation(name, vec3(0, yaw, 0)) end
        if col then
            n = n + 1
            forge.scene.spawn("Dress Col spl " .. n, vec3(x, y + col[4], z), vec3(0.001, 0.001, 0.001),
                              vec3(col[1], col[2], col[3]), "metal", false)
        end
    end

    -- ---- the dam's face dressing and the hall's piers (generated models)
    put("spl_dam_face", 800, 0, 0, 0, 1)
    for _, p in ipairs({ { 816.0, -23.8 }, { 836.0, -23.8 }, { 816.0, -9.0 }, { 836.0, -9.0 } }) do
        put("spl_pier", p[1], GY, p[2], 0, 1.0, { 0.46, 3.0, 0.46, 3.0 })
    end
    -- the turbine runner: sunk in the bay so the hub and blades break the water
    put("spl_turbine", 828.0, -0.7, -17.0, 0, 1)

    -- ---- trees on the canyon rims (above the walls: seen from the plaza as a skyline)
    for _, t in ipairs({
        { "srf_tree_pine_a", 744, 14, 22, 1.3 }, { "srf_tree_pine_b", 750, 14, 38, 1.2 },
        { "srf_tree_dead", 746, 14, 4, 1.0 }, { "srf_tree_pine_a", 752, 14, -14, 1.4 },
        { "srf_tree_pine_b", 770, 16, 46, 1.3 }, { "srf_tree_pine_a", 790, 16, 49, 1.5 },
        { "srf_tree_tall", 812, 16, 47, 1.2 }, { "srf_tree_pine_b", 830, 16, 50, 1.3 },
        { "srf_tree_pine_a", 848, 14, 40, 1.2 }, { "srf_tree_dead", 852, 14, 20, 1.1 },
        { "srf_tree_pine_b", 846, 14, 2, 1.3 }, { "srf_tree_pine_a", 856, 14, 28, 1.4 },
    }) do
        put(t[1], t[2], t[3], t[4], 0, t[5])
    end

    -- ---- rocks at the canyon feet (mesh colliders: you bump into the rock you see)
    put("srf_rock_large_a", 766.5, GY, 20.5, 25, 1.0, "mesh")
    put("srf_rock_tall_b", 835.5, GY, 12.0, 200, 1.0, "mesh")
    put("srf_stone_large_b", 776.0, GY, 34.0, 0, 1.0, "mesh")
    put("srf_rock_large_c", 842.0, GY, 26.0, 70, 1.0, "mesh")
    put("srf_stone_tall_a", 826.0, GY, 34.5, 160, 1.0, "mesh")
    put("srf_stone_small_c", 788.0, GY, 3.5, 40, 1.2, nil)
    put("srf_stone_small_c", 812.5, GY, 6.0, 120, 1.0, nil)
    put("srf_stone_small_c", 770.0, GY, 9.0, 0, 1.1, nil)
    put("srf_rock_large_a", 744.0, GY, -10.0, 90, 1.0, nil) -- behind the stair wall, seen from the crest
    put("srf_rock_tall_b", 858.0, 9.0, -24.0, 40, 1.0, nil) -- the caravan terrace edge

    -- ---- the plaza: a salvage yard left by the caravan's last crew
    put("srf_container", 836.5, GY, 29.0, 0, 1.0, { 0.88, 1.4, 1.75, 1.4 })
    put("srf_container", 833.0, GY, 33.5, 90, 1.0, { 1.75, 1.4, 0.88, 1.4 })
    put("srf_barrels", 765.5, GY, 28.0, 20, 1.0, { 0.82, 0.53, 0.74, 0.53 })
    put("srf_barrel", 767.4, GY, 29.6, 0, 1.0, { 0.32, 0.47, 0.32, 0.47 })
    put("srf_crate_large", 838.2, GY, 24.0, 90, 1.0, { 0.75, 0.38, 0.38, 0.38 })
    put("srf_crate", 836.8, GY, 23.2, 15, 1.0, { 0.38, 0.38, 0.38, 0.38 })
    put("srf_generator", 812.0, GY, 33.8, 0, 0.8, { 0.84, 0.64, 1.12, 0.64 })
    put("srf_sign", 806.5, GY, 28.0, 180, 1.0, nil)
    put("srf_mast", 761.5, GY, 14.0, 0, 1.0, nil)
    put("srf_log_large", 783.0, GY, 35.0, 80, 1.0, nil)
    put("srf_stump_old", 822.0, GY, 18.0, 0, 1.0, nil)
    -- the gorge lip and the chamber mouth: fences so the edges read as edges
    for _, x in ipairs({ 788.0, 790.4 }) do put("srf_fence", x, GY, -4.5, 0, 1.0, nil) end
    for _, x in ipairs({ 809.6, 812.0 }) do put("srf_fence", x, GY, -4.5, 0, 1.0, nil) end

    -- ---- the caravan, on the terrace at the dam's far end (y = 9)
    put("srf_container", 847.0, 9.0, -34.0, 90, 1.0, { 1.75, 1.4, 0.88, 1.4 })
    put("srf_container", 847.0, 9.0, -30.2, 90, 1.0, { 1.75, 1.4, 0.88, 1.4 })
    put("srf_crate_large", 855.0, 9.0, -35.0, 20, 1.0, { 0.75, 0.38, 0.38, 0.38 })
    put("srf_barrels", 856.5, 9.0, -29.0, 60, 1.0, { 0.82, 0.53, 0.74, 0.53 })
    put("srf_generator", 853.5, 9.0, -35.5, 270, 0.8, nil)
    put("srf_sign", 843.4, 9.0, -27.6, 200, 1.0, nil)

    -- ---- ground cover along the plaza's south edge and the rims (no colliders)
    local R = lcg(47)
    local COVER = { "srf_bush_a", "srf_bush_large", "srf_bush_detailed", "srf_grass_tuft", "srf_grass_tuft",
                    "srf_grass_leafs", "srf_plant_flat" }
    for _, c in ipairs({ { 778, 32, 4, 5 }, { 820, 31, 4, 5 }, { 800, 35, 6, 6 }, { 764, 12, 3, 4 },
                         { 838, 6, 3, 4 }, { 786, 18, 3, 3 }, { 816, 14, 3, 3 } }) do
        for _ = 1, c[4] do
            local a, r = R(0, 6.283), R(0.5, c[3])
            local m = COVER[math.floor(R(1, #COVER + 0.999))]
            put(m, c[1] + math.cos(a) * r, GY, c[2] + math.sin(a) * r, R(0, 360), R(0.8, 1.25), nil)
        end
    end

    local list, skipped = {}, 0
    for m, k in pairs(missing) do
        list[#list + 1] = m
        skipped = skipped + k
    end
    table.sort(list)
    if #list > 0 then
        forge.log("spillway dressing: " .. skipped .. " placements skipped, models not loaded: "
                  .. table.concat(list, ", "))
    end
    forge.log("spillway dressing: " .. n .. " entities placed")
    return n
end
