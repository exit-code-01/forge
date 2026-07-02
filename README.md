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
- **P6** Scripting: Lua bindings
- **P7** Editor: scene hierarchy, inspector, gizmos (Dear ImGui)
- **P8** Animation (skeletal), particles, audio
- **P9** Networking basics (optional)
- **P10** Polish, docs, sample game, public release

## Status

P5 complete — assets flow from disk to screen. Assimp (OBJ/glTF) and
stb_image import into plain CPU data; the renderer serves real resource
handles (meshes, textures, per-material descriptor sets); and hot reload
watches the files — edit `assets/textures/crate.png` or regenerate
`assets/models/torus.obj` while the sandbox runs and the change lands in
about half a second, no restart. The demo scene: a crate-textured imported
torus beside the physics cube, all PCF-shadowed, zero validation complaints
(validation layer now active via the LunarG SDK). Next: P6 — Lua scripting.

## Vulkan SDK

Building Forge does **not** require the Vulkan SDK — volk loads the Vulkan
loader at runtime, and the Vulkan headers are fetched by CMake. Installing the
[LunarG Vulkan SDK](https://vulkan.lunarg.com/sdk/home) is recommended for
development, though: it provides the `VK_LAYER_KHRONOS_validation` layer, which
debug builds enable automatically when present (see ADR-009). Without it the
engine still runs — it just logs that validation is off.
