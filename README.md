# Forge

![CI](https://img.shields.io/badge/CI-pending-lightgrey)

A from-scratch(ish) C++20 3D game engine, built solo and in public. Development
is documented as a YouTube devlog — every phase ships something that compiles,
runs, and is worth talking about on camera.

## Build

```sh
cmake -B build
cmake --build build
./build/bin/forge_sandbox
```

**Linux build prerequisites** (GLFW's X11/Wayland backends need these; GLFW/GLM
themselves are fetched by CMake, and macOS/Windows need nothing extra):

```sh
sudo apt install xorg-dev libwayland-dev libxkbcommon-dev wayland-protocols
```

## Layout

- `engine/` — the engine itself: a static library. Public API lives under
  `engine/include/forge/`; private implementation under `engine/src/`.
  Consumers only ever include `forge/…` headers.
- `sandbox/` — a thin consumer executable that links the engine. Proving ground
  for the public API.
- `editor/` — arrives at P7.
- `cmake/` — shared build modules (warning flags, etc.).

## Roadmap

- **P0** Scope, repo, CMake build system — ✅ done
- **P1** Platform layer: window, input, logging, math, ECS core — ✅ done
- **P2** Renderer core: Vulkan init, swapchain, first triangle — ✅ done
- **P3** PBR forward renderer: texturing, basic lighting/shadows — ✅ done
- **P4** Physics: Jolt integration, collision, rigidbodies — ✅ done
- **P5** Asset pipeline: model/texture import, hot reload — ✅ done
- **P6** Scripting: Lua bindings — ✅ done
- **P7** Editor: scene hierarchy, inspector, gizmos (Dear ImGui) — ✅ done
- **P8** Animation (keyframe; skinning deferred), particles, audio — ✅ done (lean, ADR-020)
- **P9** Networking basics (optional)
- **P10** Polish, docs, sample game, public release

## Status

VAULT (the sample game) is playable start to finish: tutorial + **fourteen**
puzzle rooms, menus, settings, and save/continue. Weeks 1–11 built the
plates/lasers/glass vocabulary, the parkable companion drone, the art and
character pass (rigid-rig SPARK + first-person glove, ADR-027). Week 12
(ADR-028) is the **launch pass — PITCH & CATCH**: hold LMB to CHARGE a held
crate (whine rises, plateaus, clicks) and release to launch at up to
25 m/s; resonance bells that RING green when struck hard enough; and SPARK's
new F command — point anywhere and the drone goes there, presses plates,
and CATCHES your launches out of the air, sagging under the momentum before
depositing into snap-sockets. Heavy cubes are too much for SPARK (taught as
comedy, enforced as constraint). Four new rooms teach it: the Range, the
Errand, the Alley-Oop, and the two-lane Duet finale. The niche this stakes
out: the only first-person puzzler where the shooting loop ends in a game
of catch with your companion — the trigger builds trust, not damage.
Engine seams that landed on the way: `forge.input.down`,
`forge.player.look/charge/set_throw_speed` (release-edge throws),
`forge.physics.set_velocity`, `forge.hud.set_reticle`, and live physics
positions from `forge.scene.getPosition` for dynamic bodies.

## Vulkan SDK

Building Forge does **not** require the Vulkan SDK — volk loads the Vulkan
loader at runtime, and the Vulkan headers are fetched by CMake. Installing the
[LunarG Vulkan SDK](https://vulkan.lunarg.com/sdk/home) is recommended for
development, though: it provides the `VK_LAYER_KHRONOS_validation` layer, which
debug builds enable automatically when present (see ADR-009). Without it the
engine still runs — it just logs that validation is off.
