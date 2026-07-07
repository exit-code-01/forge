# ENGINE_GAPS.md — VAULT's engine gap ledger

Rule 6 of the master prompt: every gap gets capability needed, minimal API,
estimated days, and the week it lands. Created week 9; earlier gaps back-
filled from the ADR log so the ledger is complete.

## Landed

| Week | Capability needed | Minimal API shipped | Cost |
|------|-------------------|---------------------|------|
| P8   | Prop animation (doors, elevator) | `anim::Clip` keyframe position tracks, Lua-triggerable via scene names | 1 d |
| 5    | Per-voice audio (beds + SFX volumes) | `Audio::playLoop/setVolume/stop`, `SoundHandle`; `forge.audio.loop/set_volume/stop` | 1 d |
| 5    | Per-room mood lighting | `Renderer::setLighting(color, dir)`; `forge.render.set_light` | 0.5 d |
| 6    | Player warp (checkpoints/respawn) | `PhysicsWorld::setCharacterPosition` (warp + zero velocity); `forge.player.teleport` | 0.5 d |
| 6    | Win signal from script | `forge.game.win()` -> host raises the menu | 0.1 d |
| 7    | Objective hints on the HUD | `forge.hud.set_hint(text)`; host draws top-centred | 0.25 d |
| 9    | Colour language (orange/red/green law) | `forge.scene.setTexture(name, texture)` + host texture registry | 0.25 d |
| 11   | Real prop/character meshes from Lua | model registry (every `assets/models/*.obj` by stem) + optional `mesh` arg on `forge.scene.spawn` | 0.5 d |
| 11   | Character orientation (SPARK banking, viewmodel) | `forge.scene.setRotation(name, eulerDeg)` — visual-only, colliders stay axis-aligned | 0.25 d |
| 11   | Rotation curves for rigid-rig animation | `anim::Clip::eulerKeys` + `sampleEuler` (glove throw/grab clips consume them) | 0.25 d |

## Closed in Lua (no engine change needed)

- **Player-as-weight on plates (wk 8):** `CharacterVirtual` is not a
  broadphase body, so `physics.overlap` never sees the player. Solved with a
  Lua-side position test (`playerNear`). An engine-side fix (include the
  character in `overlapBox`) remains available if a future mechanic needs
  overlap parity, but nothing does yet.

## Open (filed, not scheduled)

- **Kinematic platform body** — platforms that PUSH riders instead of
  teleport-following colliders. Needed by: the Carousel room
  (docs/PUZZLE_IDEAS.md), any fast-moving platform. Proposal:
  `BodyType::Kinematic` in `PhysicsWorld::addBox` + `moveKinematic(id, pos,
  dt)` so Jolt sweeps it. Est: 1–2 d. (First noted ADR-020.)
- **HDR + tone mapping + emissive materials** — the master prompt's graphics
  bar and the REAL carrier of the colour language (emissive light strips
  instead of tinted albedo). Texture swaps are the stand-in until this
  lands. Est: 3–4 d, an ENGINE-week on its own.
- **Skeletal animation + blending** — the expanded prompt's UNIT-7 arms /
  SPARK rig / finale character driver. The week-11 art pass (ADR-027)
  answered all three needs with RIGID rigs (multi-part entities +
  setRotation + euler clips) because every VAULT character is a machine —
  so this gap now needs an ORGANIC character to justify it, and none is
  planned. Deferred with evidence (was: deferred on a hunch, ADR-020).
  Est: 5+ d.
