-- assets/scripts/scene.lua — VAULT tutorial-room scene logic.
-- Gameplay decisions live HERE; C++ owns the machinery underneath.
-- Edit this file while the game runs: it hot-reloads in ~half a second.
-- (A syntax error won't crash anything — the engine keeps the last good
-- version and logs the problem until you save a fix.)

local spawnCount = 0

function onStart()
    forge.log("scene.lua loaded (" .. _VERSION .. ")")
    forge.log("VAULT week 1: E spawns extra crates")
end

function onUpdate(dt)
    -- E: rain extra throwable crates into the room, with dust puffs.
    -- (SPACE is the player's jump now; the old demo kick retired with it.)
    if forge.input.pressed("e") then
        for i = 1, 3 do
            local at = vec3((i - 2) * 0.9, 3.0 + i * 0.6, -1.5)
            forge.physics.spawnBox(vec3(0.25, 0.25, 0.25), at, true, 0.4)
            forge.fx.burst(at, 8)
        end
        forge.audio.play("assets/sounds/kick.wav")
        spawnCount = spawnCount + 3
        forge.log("crate drop! total spawned: " .. spawnCount)
    end
end
