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
| 12   | Held-state input polling (charge timers) | `forge.input.down(name)`; `pressed`/`down` accept `lmb`/`rmb` | 0.1 d |
| 12   | Aim ray for scripted verbs (SPARK's F) | `forge.player.look() -> eye, dir` — the glove's own ray | 0.1 d |
| 12   | Charged launch | throw moved to RELEASE-edge; `forge.player.charge()` (host owns the grab-press/charge-press disambiguation) + `forge.player.set_throw_speed(v)` (script owns the curve; host clamps 11–25, resets after every throw) | 0.25 d |
| 12   | Velocity assignment (SPARK's catch snap) | `forge.physics.set_velocity(body, vel)` — `kick` is additive and can never zero a body | 0.1 d |
| 12   | Reticle answer-back (charge bloom, F acknowledgment) | `forge.hud.set_reticle(mode, value)` — "default" blooms with charge01, "command" flashes SPARK-blue | 0.25 d |
| 12   | Live positions for dynamic bodies | `forge.scene.getPosition` now returns the PHYSICS transform for dynamic bodies (TransformC only holds their spawn pose) — found by the bell tests; also fixes the silently-dead week-6 crate-void respawn check | 0.1 d |
| 13   | HDR scene target + filmic tone map + bloom | Offscreen RGBA16F target; fullscreen composite applies exposure and ACES to the sRGB swapchain; down/up bloom chain; UI drawn un-tonemapped. `Renderer::setExposure/setBloom`; `forge.render.set_exposure/set_bloom` | — |
| 13   | Emissive and tinted materials (the colour language's real carrier) | `DrawItem::emissive` and `DrawItem::tint`; `forge.scene.setEmissive/setTint`; particles carry an emissive colour (cap 1500) | — |
| 13   | Room lighting beyond one directional light | Up to 16 unshadowed point lights (`Renderer::setPointLights`); exponential distance fog (`setFog`); hemisphere ambient replaces the flat term; `forge.render.light/clear_light/set_fog` | — |
| 13   | Shadows in every room | Directional shadow frustum follows the camera (before, only the area near the origin was shadowed) | — |
| 13   | Kinematic bodies (moving platforms) | `BodyType::Kinematic` + `PhysicsWorld::moveKinematic`: platforms sweep to a target with real velocity and carry riders; `overlapBox` ignores them. Closes the kinematic platform gap | — |
| 13   | Spawn-time body options | `BoxDesc` gains friction, density, lockRotation; `forge.scene.spawn` takes an options table (kinematic, lockRotation, friction, density, restitution) | — |
| 13   | Character on moving ground, forgiving jumps | Rides moving ground; coyote time 0.1 s and buffered jumps 0.12 s (host side); `pushCharacter` for conveyors; `forge.player.push/velocity/grounded/holding/drop` | — |
| 13   | Distance-attenuated, panned one-shots | `Audio::play` takes a pitch; `Audio::playAt` + `setListener`; `forge.audio.play` pitch arg and `forge.audio.play_at` | — |
| 13   | Feel and HUD feedback from script | `forge.camera.shake`; `forge.hud.toast` and `forge.hud.set_stat`; `forge.fx.burst` colour arg | — |
| 13   | Persisted numbers and script ergonomics | `forge.game.save/load` store numbers in settings.ini; name-to-entity index; `scene.exists/bodyName/bodyId`; vec3 `+`, `-`, `*` and `:length()` | — |
| 14   | Whole-file models with materials (asset packs) | `assets::loadModel` (every mesh, node transforms baked, base-colour texture + factor; glTF via cgltf, OBJ+MTL via Assimp) + `forge::Model` / `appendModelDraws` (one override rule, ADR-030); .obj/.gltf/.glb in assets/models registered by stem; `assets/models/import.ini` (`scale`, `rotate`, `hide`); bounds logged at load | — |
| 14   | A texture budget that degrades | Cap 64 -> 256; a full table logs once and draws white (`Renderer::whiteTexture()`); a pack's shared atlas uploads once (`TextureCache`) | — |
| 14   | Skeletal animation + blending | glTF skins and clips (`anim::Skeleton`, `SkeletalClip`; LINEAR/STEP); `anim::Animator` (loop, speed, timed cross-fade); GPU skinning via a second vertex stream + per-frame palette storage buffer (2048 joints); skinned shadow pass; rigid parts ride their nodes. `forge.anim.play/clips/time`. Closes the Open entry below | — |
| 14   | Sky behind open-topped rooms | `Renderer::setSky(Sky)`; `forge.render.set_sky{...}`: gradient + sun disc on the key light, optional equirect 8-bit panorama, fogged at the far plane; dark indoor default | — |

## Considered and cut (week 12 — cheaper answers existed)

- **`forge.physics.impacts()` (Jolt contact listener)** — the resonance
  bells use a Lua swept-segment test over per-crate physics velocities
  instead: zero engine days, no tunneling at 25 m/s.
- **`forge.audio.set_pitch`** — the charge whine is three pre-baked loops
  crossfaded with the existing per-voice `set_volume`; the pitch plateau is
  baked into the samples.
- **`spawnBox` density param** — the heavy cube's mass READ comes from a
  size-scaled throw speed host-side (throws are velocity assignments, so
  Jolt mass never enters); real density waits for a mechanic that needs it.

## Closed in Lua (no engine change needed)

- **Player-as-weight on plates (wk 8):** `CharacterVirtual` is not a
  broadphase body, so `physics.overlap` never sees the player. Solved with a
  Lua-side position test (`playerNear`). An engine-side fix (include the
  character in `overlapBox`) remains available if a future mechanic needs
  overlap parity, but nothing does yet.

## Open (filed, not scheduled)

- **Material maps beyond base colour** (found week 14) — glTF normal,
  metallic-roughness, occlusion and emissive maps/factors are not imported;
  every material uses mesh.frag's fixed metallic 0.15 / roughness 0.35.
  Normal maps also need tangents in the vertex layout. Unlocks ambientCG /
  Poly Haven PBR sets. Not yet costed.
- **Alpha modes** (week 14) — glTF `alphaMode` MASK/BLEND is ignored, so
  foliage cards and decals draw opaque. Alpha test is cheap; blending needs
  a sorted transparent pass. Not yet costed.
- **Vertex colours** (week 14) — COLOR_0 (glTF) is ignored; colour-only kits
  would need it multiplied into the tint. Not yet costed.
- **Animation fidelity and events** (week 14) — CUBICSPLINE keys are
  imported as LINEAR (tangents dropped); morph-target (weights) channels and
  influences beyond JOINTS_0/WEIGHTS_0 are dropped; no animation events or
  root motion (scripts poll `forge.anim.time`). Not yet costed.
- **Attachments and runtime part visibility** (week 14) — no Lua API to put
  a prop on a joint, and `hide` in import.ini is import-time only (the
  KayKit files carry every weapon variant on the hand slots). Not yet costed.
- **Bounds and colliders for animated entities** (week 14) — an animated
  entity keeps its spawn box; there are no per-pose bounds (no culling
  exists yet either). Not yet costed.
- **HDR environments** (week 14) — the sky takes 8-bit equirect PNGs only;
  .hdr needs stb's HDR path and a float texture format, and the sky does not
  feed ambient lighting (no IBL). Not yet costed.
- **Shadow texel snapping** — the directional shadow frustum follows the
  camera (ADR-029), so shadow texels swim as the camera moves. Snapping the
  light frustum to whole-texel steps would stop the shimmer. Not yet costed.
- **Renderer items still missing from the graphics bar** — SSAO and decals.
  Neither is in the engine yet, and neither is scheduled. Not yet costed.
