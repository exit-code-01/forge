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
- **P7** Editor: scene hierarchy, inspector, gizmos (Dear ImGui)
- **P8** Animation (skeletal), particles, audio
- **P9** Networking basics (optional)
- **P10** Polish, docs, sample game, public release

## Status

P6 complete — gameplay logic lives in Lua. `forge::ScriptEngine` (Lua 5.4.8
via sol2) runs `assets/scripts/scene.lua` with onStart/onUpdate hooks and
bindings shaped by the sample game's needs: spawn boxes, kick bodies, read
positions, read input. Scripts hot-reload through the same watcher as
textures and meshes — and a broken script never crashes the engine; the
last good version keeps running while the error is on screen. The SPACE
kick migrated from C++ to one line of Lua, and E rains crate boxes into
the physics scene. Zero validation complaints. Next: P7 — the Dear ImGui
editor (scene hierarchy, inspector, gizmos), where the engine starts
building VAULT's rooms.

## Vulkan SDK

Building Forge does **not** require the Vulkan SDK — volk loads the Vulkan
loader at runtime, and the Vulkan headers are fetched by CMake. Installing the
[LunarG Vulkan SDK](https://vulkan.lunarg.com/sdk/home) is recommended for
development, though: it provides the `VK_LAYER_KHRONOS_validation` layer, which
debug builds enable automatically when present (see ADR-009). Without it the
engine still runs — it just logs that validation is off.
