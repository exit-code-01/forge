# COLLAB.md — two-sided joint session plan

Binding rules: CLAUDE.md. Channel: docs/COLLAB_LOG.md.

## 1. What this project is
- **Forge**: C++20 / Vulkan 1.3 engine, static lib `forge::engine` plus a thin `sandbox` exe.
- Modules: platform/input (GLFW), ECS, Vulkan renderer (HDR, bloom, one directional shadow map, point lights, skinning), Jolt physics, Lua 5.4 scripting (`forge.*`), audio, asset import (glTF/OBJ).
- **VAULT**: first-person puzzle game scripted in Lua (`assets/scripts/`): tutorial + 17 underground rooms, a glove/SPARK companion mechanic.
- New: an outdoor **Surface** location with a mission system, an NPC quest-giver and 42 CC0 props (ADR-031).
- Decisions are in docs/DECISIONS.md (ADR-001..032); capability gaps in docs/ENGINE_GAPS.md.
- **Known weak spot**: the Surface looks like slabs, because the engine only has box colliders (real terrain needs triangle meshes) and outdoor shadows end about 16 m from the camera.
- The engine has so far only ever been built on one machine. Lane C0 exists to fix that.

## 2. Setup on a fresh Windows machine
**Prerequisites**: Visual Studio 2022+ or Build Tools with the "Desktop development with C++" workload; CMake and Ninja (the VS-bundled ones are fine); Git; Python 3 (screenshots and tools only); a Vulkan 1.3 GPU with current drivers.
Vulkan SDK 1.3+ (LunarG): not needed to build (volk loads the loader at runtime, headers are fetched), but needed for `glslc` (shader edits, lane C1) and the validation layer (the gate requires it).
Confirm with `glslc --version` and `echo %VULKAN_SDK%`.

**Dependencies**: all fetched by CMake `FetchContent` from GitHub, pinned (cmake/Dependencies.cmake): GLFW, GLM, Vulkan-Headers, volk, JoltPhysics, stb, cgltf, assimp, Lua. Nothing is vendored, so the **first configure needs network access and takes a while** (assimp and Jolt are big). Later configures are offline.
SPIR-V (`engine/shaders/*.spv`) is checked in and embedded by pure CMake (ADR-011), so building needs no shader compiler.

**Commands** (in an "x64 Native Tools" prompt, or after calling `vcvars64.bat`; presets use Ninja):
```
git clone <repo-url> forge && cd forge
git checkout upgrade-pass
git checkout -b collab/<lane>-<short-name>
cmake --preset debug
cmake --build build\debug
build\debug\bin\forge_sandbox.exe        # MUST run from the repo root (assets/ are relative)
```
Debug builds enable Vulkan validation automatically when the SDK is present.
`tools\dev\build.bat` wraps the build step; override its machine paths with env vars `FORGE_VCVARS`, `FORGE_CMAKE_BIN`, `FORGE_NINJA_BIN`, `FORGE_BUILD_DIR` (defaults are the owner's paths).
The README says `build/bin`; the presets actually output to `build\debug\bin`.

**Dev harness** (`tools/dev/`, tracked copies of the owner's untracked `build/_*` scripts; use Git Bash for the .sh):
- Env vars read by the exe: `FORGE_DEV=1` (polls `build/_cmd.lua` as a file-drop Lua console, cursor stays free), `FORGE_AUTOPLAY=1` (skips the menu), `FORGE_TITLE` (window title, to run two instances).
- `tools/dev/rundev.sh <seconds> [cap.png]` runs the exe in dev mode for N seconds, logs to `build/_dev.log`, optionally screenshots via `tools/dev/gui.py cap`, kills the exe, and prints error/warn/vuid lines. `FORGE_EXE` overrides the exe path.
- Run the sequencers (`build/` must exist; it is gitignored):
  ```
  cp tools/dev/test_all.lua build/_start.lua     && bash tools/dev/rundev.sh 200   # expect 51 PASS
  cp tools/dev/test_surface.lua build/_start.lua && bash tools/dev/rundev.sh 400   # expect 126 PASS (slow machine: 400 s)
  cp tools/dev/test_spillway.lua build/_start.lua && bash tools/dev/rundev.sh 420   # expect 111 PASS (ADR-034)
  cp tools/dev/test_mesh.lua build/_start.lua    && bash tools/dev/rundev.sh 30    # expect 16 PASS (ADR-032)
  grep -c "T> PASS" build/_dev.log ; grep "T> FAIL" build/_dev.log
  ```
  Lines look like `[lua] T> PASS p2 the run ends (win screen) [2.3s]`; a finished run ends with `T> DONE`. Need zero `T> FAIL` and PASS counts of 51 / 126 / 16 (test_all / test_surface / test_mesh). If DONE never appears, raise the seconds argument (slower GPU) and report it.
- `tools/dev/gui.py` (Windows, ctypes): `cap <png>`, `key <name>`, `look dx dy`, `resize w h`, `rect`, for the window "Forge Sandbox".

**Gotchas**
- `cmake`/`ninja`/`cl` fail unless the MSVC environment (vcvars64) is loaded in that shell.
- The `timeout` command does not kill the exe; use `taskkill //F //IM forge_sandbox.exe`.
- A freshly built exe may be locked by antivirus for about 15 s; retry.
- `settings.ini` in the repo root is a real save/settings file (gitignored): back it up before automated runs, restore after.
- Autoplay tests teleport and run real puzzle logic; do not touch keyboard or mouse during them.
- Not confirmed from the repo: minimum CMake version, GPU requirements beyond Vulkan 1.3, CI status. (Lane C0 fixes this section where wrong.)

## 3. Lanes and file ownership
| Lane | Side | Goal | Owns (only this lane edits) | Done when |
|---|---|---|---|---|
| O1 | owner | Static triangle-mesh colliders so real terrain can replace slab walls | `engine/src/physics/*`, `ScriptEngine.*`, `sandbox/src/main.cpp` | Lua can spawn a mesh collider; character walks on it; tests pass **DONE** (d94ac43..e4c32e9) |
| O2 | owner (Blender MCP) | Surface art pass: terrain/cliff meshes, robot quest-giver model | `assets/models/*`, `assets/scripts/surface*.lua`, `missions.lua`, `scene.lua` | Surface no longer reads as slabs; tests pass **DONE** (d94ac43..e4c32e9) |
| O3 | owner | Playtest fixes on the Surface (E prompt through walls, dark head-house, over-explicit hints) | same Lua files as O2 | Issues fixed; 126/126 **DONE** (d94ac43..e4c32e9) |
| C0 | collab | **First 30-40 min, mandatory**: clone, build, run both sequencers; record results and every setup snag in COLLAB_LOG; fix section 2 of this file where wrong | `docs/COLLAB.md` section 2, `docs/COLLAB_LOG.md`, `tools/dev/*` | Log has counts (51/51, 126/126 or exact deviations) and the snag list |
| C1 | collab | **Main task: outdoor shadows.** Today one shadow map follows the camera and ends about 16 m out. Goal: shadows to >= 60 m outdoors with a soft distance fade, no visible regression indoors. Cascaded shadow maps (2-3 cascades) or a justified simpler scheme; texel-snap to stop swimming | `engine/src/renderer/*`, engine shaders (`mesh.frag`, `shadow*.vert`, plus recompiled `.spv`) | Before/after screenshots (Surface and one facility room) in PR/log; frame time no worse than +10 %; gate passes; ADR added. Must not change the Renderer public API used by main.cpp except by ADDING optional things; if a main.cpp change is unavoidable, keep it <= 20 lines and announce in the log first |
| C2 | collab (stretch, if C1 is done or blocked) | Release build + packaging: a `release` preset (one exists in CMakePresets.json; verify it works), `tools/package.py` producing a zip (exe + assets + licences/attribution) that runs on a machine without the SDK, fix/extend `ci/` and `.github/workflows/ci.yml` for a Windows build | `CMakePresets.json`, `ci/`, `.github/workflows/`, `tools/package.py` | Zip runs on a clean machine; CI Windows job builds |
| C3 | collab (fallback if the machine cannot build/run Vulkan at all) | No-build work: design doc + Lua for a third location as NEW files only (`assets/scripts/<name>.lua` + its mission, following surface.lua's pattern), tested later on the owner's machine; plus docs review | new files only | Files pushed; owner can load them |

**Hot files** (nobody edits without announcing in the log first): `sandbox/src/main.cpp`, `ScriptEngine.cpp/.hpp`, `assets/scripts/scene.lua`, `README.md`, `docs/DECISIONS.md` (append-only: take the next ADR number, announce it).

## 4. Git workflow
- Branch from `upgrade-pass` as `collab/<lane>-<short-name>` (example `collab/c1-csm-shadows`). Commit small and often.
- Commit style: one imperative or descriptive summary line with a topic prefix, details after a colon or in the body, as in `VAULT: rooms 15-17 (Assembly zone), glow language, data cores` and `Upgrade pass: HDR renderer, kinematic physics, ...`. Suggested: `Renderer: cascaded shadow maps for outdoor scenes (C1)`.
- Run `git pull --rebase origin upgrade-pass` at least every 30 min and before every push. Never force-push.
- Push your branch and open a PR into `upgrade-pass` (or announce the branch in the log). The owner side merges after running the gate on their machine.

## 5. Timeline (3 hours)
| Time | Activity |
|---|---|
| 0:00-0:40 | Setup + C0 report. Checkpoint log entry at 0:40 |
| 0:40-2:20 | Main lane (C1; O1-O3 on the owner side). Checkpoint at 1:30 |
| 2:20-2:45 | Gate + PR. Checkpoint at 2:20 |
| 2:45-3:00 | Merge, joint playtest, write "what's next" in the log |

## 6. Communication
- docs/COLLAB_LOG.md is the channel: append-only, newest at bottom, entry `### HH:MM side/lane — STATUS|BLOCKER|QUESTION|HANDOFF` plus at most 6 lines.
- Blockers from machine or permission settings go there: command, exact error, what you need (rule 10 in CLAUDE.md). Do not work around them destructively.
- Commit and push log entries promptly on your own branch AND tell your human so they can relay if urgent.

## 7. Kickoff prompt for the collaborator (paste into your Claude Code)
```
Read CLAUDE.md and docs/COLLAB.md in full. Ask me once whether I pre-authorise you
to commit and push to my lane branch. Then do lane C0 (build state, build, run both
test sequencers, log results and every setup snag in docs/COLLAB_LOG.md, correct
docs/COLLAB.md section 2), then lane C1 (outdoor shadows) on branch
collab/c1-csm-shadows off upgrade-pass. Follow the rules in CLAUDE.md exactly:
token discipline (grep, never read main.cpp/scene.lua whole, max 2 subagents, cheap
models for routine work), stay inside your lane's files, report test counts as run,
and if anything on this machine blocks you, write it in docs/COLLAB_LOG.md and tell
me instead of working around it. Checkpoint log entries at 0:40, 1:30 and 2:20.
```
