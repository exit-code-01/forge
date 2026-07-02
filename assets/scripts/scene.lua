-- assets/scripts/scene.lua — Forge sandbox scene logic.
-- Gameplay decisions live HERE now; C++ owns the machinery underneath.
-- Edit this file while the sandbox runs: it hot-reloads in ~half a second.
-- (A syntax error won't crash anything — the engine keeps the last good
-- version and logs the problem until you save a fix.)
--
-- Globals provided by the host before load:
--   cube  — BodyId of the falling checker cube

local spawnCount = 0

function onStart()
    forge.log("scene.lua loaded (" .. _VERSION .. ")")
    forge.log("SPACE kicks the cube, E rains boxes")
end

function onUpdate(dt)
    -- The kick that used to be C++ in main.cpp, now one line of gameplay:
    if forge.input.pressed("space") then
        forge.physics.kick(cube, vec3(0.6, 6.0, 0.4))
        local p = forge.physics.position(cube)
        forge.log(string.format("kicked cube at (%.1f, %.1f, %.1f)", p.x, p.y, p.z))
    end

    -- E: rain three small boxes above the scene.
    if forge.input.pressed("e") then
        for i = 1, 3 do
            forge.physics.spawnBox(vec3(0.25, 0.25, 0.25),
                                   vec3((i - 2) * 0.9, 4.0 + i * 0.8, 0.6),
                                   true, 0.5)
        end
        spawnCount = spawnCount + 3
        forge.log("box rain! total spawned: " .. spawnCount)
    end
end
