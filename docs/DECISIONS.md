# Architecture Decisions

An ADR-lite log. Each entry captures a decision, why it was made, and enough
context that future-me doesn't relitigate it without new information.

## ADR-001 — Engine as a static library, executables stay thin

The engine is a static library (`forge::engine`); `sandbox` (and later
`editor`) are thin executables that link it. This forces a real public/private
boundary from day one: anything a consumer needs must be deliberately exposed,
not reached into. It keeps build times honest, makes the API surface reviewable,
and means the same engine binary backs every tool without a shared-object
loader dance. Shared libraries and plugin loading are deferred until there's a
concrete need.

## ADR-002 — Public headers under `engine/include/forge/`, privates under `engine/src/`

Public API headers live in `engine/include/forge/`; implementation and private
headers live in `engine/src/`. CMake exposes `include/` as PUBLIC and `src/` as
PRIVATE on the engine target, so consumers physically cannot include an
internal header — the include path isn't there. If sandbox ever needs an
engine internal, that's a signal to design a public API for it, not to widen
the include path. The `forge/` prefix namespaces our headers against any
dependency we vendor later.

## ADR-003 — Dependencies via pinned FetchContent, added only when first consumed

Third-party libraries come in through CMake FetchContent, pinned to a specific
commit/tag, and are added only when code actually consumes them — not
speculatively. At P0 there are none. This keeps the dependency graph minimal
and auditable, makes builds reproducible without a separate package manager,
and ensures every dependency has a real, traceable reason for being present.
Warning flags stay PRIVATE to our targets so third-party warnings never become
our problem.

## ADR-004 — GLFW over SDL2 for windowing and input

GLFW is our windowing/input library, chosen over SDL2. It has a smaller surface
area, a Vulkan-first design (`GLFW_NO_API` + `glfwCreateWindowSurface` fit our
renderer exactly), and no baggage from audio/gamepad/networking subsystems we
already cover with dedicated libraries. The choice is trivially reversible
because GLFW is fully encapsulated behind `forge::Window` — only `Window.cpp`
includes GLFW headers, and it does so under `#define GLFW_INCLUDE_NONE` (GLFW's
default of pulling in system OpenGL headers breaks on Vulkan-only machines that
lack `GL/gl.h`; the define is mandatory anywhere GLFW is included). Swapping to
SDL2 later would touch exactly one translation unit, not the whole codebase.

## ADR-005 — No `AlignConsecutive*` formatting; code conforms to the tool

Our canonical `.clang-format` deliberately leaves `AlignConsecutiveAssignments`
and `AlignConsecutiveDeclarations` off. Column-aligning consecutive `=` signs or
declarations looks tidy, but editing one line in an aligned block re-spaces its
neighbours, turning a one-line change into a multi-line diff that pollutes
`git blame` — a real cost over an 18–30 month project where blame is a debugging
tool, not decoration. So the formatter stays boring and the code conforms to the
tool rather than the reverse: no hand-aligned columns for clang-format to fight.
The rule and this rationale are cross-referenced in the header comment of
`.clang-format` itself, which is the canonical source. (Process note: an ADR
number only exists once it is a row in this file — referencing a number
elsewhere without logging it here is the violation that created this entry's
retroactive gap.)

## ADR-006 — Input: per-window state, latched edges, GLFW-mirrored keycodes

Input lives as per-window `forge::Input` state that GLFW callbacks write into
(via the window user-pointer) and game/editor code reads through `window.input()`.
Edge detection uses per-frame *latches* rather than the classic current-vs-previous
array diff: a press/release sets a flag that survives until the next `newFrame()`,
so a key tapped and released within a single frame is never lost — the diff
approach silently drops those sub-frame taps. `forge::Key` values numerically
mirror GLFW's keycodes 1:1, eliminating a translation table; the pairing is
enforced by `static_assert`s in `Input.cpp`, so a GLFW renumbering becomes a
compile error instead of a runtime bug. `GLFW_REPEAT` is deliberately ignored:
OS key-repeat is a text-input concern (a P7 char-callback matter), not gameplay
input — which is exactly why holding a key logs once through `wasKeyPressed` but
continuously through `isKeyDown`.

## ADR-007 — Logging: stable `FORGE_*` macros over a single `write()` choke point

Logging is a thin `forge::log::message()` template behind four macros
(`FORGE_TRACE/INFO/WARN/ERROR`) that all funnel through one `write(Level,
string_view)` function. The call-site contract — `FORGE_INFO("x = {}", x)` — has
not changed since P0, yet the backend has since grown timestamps, per-level ANSI
color, a runtime min-level filter, and a mutex: none of that touched a single
call site, which is the entire payoff of the choke point. Three deliberate
properties: (1) the level check happens *before* `std::format` runs, so a
filtered-out message costs one branch and never formats its arguments; (2) colors
and the TTY check are cached per-stream and auto-disabled when output is
redirected, so log files stay plain text; (3) the mutex wraps only the final
`fprintf`, because interleaved half-lines from two threads are worse than the
tiny cost of serializing the write. `std::format` (C++20) means no `printf`
format-string/argument mismatches — the compiler checks them. A file/async sink
is a P5+ concern; it will be another `write()` implementation, not an API change.

## ADR-008 — ECS: sparse-set storage with generational entity handles

The ECS uses sparse-set storage (the EnTT family, sized for readability over
raw speed): each component type `T` gets a `Pool<T>` holding a big holey `sparse`
array (entity index → dense slot) and two small packed arrays (`dense` entity
indices + parallel `data`). Iterating "every Transform" therefore walks one
contiguous array — the cache behavior that is the whole reason ECS exists.
Removal is O(1) swap-and-pop, which is *why* iteration order is unspecified and
why you must never remove a component from inside an `each()` over that same
pool. An `Entity` is not an object but a handle: a 32-bit slot index plus a 32-bit
generation counter. Destroying an entity bumps its slot's generation, so every
outstanding handle to that slot becomes detectably stale (`alive()` returns
false) instead of silently aliasing the slot's next occupant — the ABA bug made
impossible by construction. Freed indices are recycled to keep the arrays dense.
Threading is intentionally absent: the registry is single-threaded until systems
exist (P4+), at which point parallelism happens *across* systems, not inside the
registry. The two-component `each<A,B>()` walks A and filters by B; picking the
smaller pool to drive is a P4 optimization the API is already shaped to absorb.

## ADR-009 — Vulkan via volk; `VkInstance` ownership hidden behind `VulkanContext`

Vulkan entry points are loaded with volk (`volkInitialize` → `volkLoadInstance`),
not linked against `vulkan-1`. This is the decision that lets the engine *build*
on any machine — including CI — with only the pinned Vulkan-Headers and no LunarG
SDK installed: the loader is `dlopen`'d at runtime, so a driver-less box fails
loudly at construction (`"Vulkan loader not found"`) rather than failing to link.
The `VkInstance` lives inside `forge::VulkanContext`, RAII in the same shape as
`Window` (ctor acquires loader → instance → GPU enumeration; dtor destroys). Two
encapsulation choices matter: (1) the public header does *not* include any Vulkan
header — it forward-declares `VkInstance` via the same `typedef struct
VkInstance_T*` that Vulkan itself uses, which is byte-identical and standard-legal,
so `<volk.h>` stays confined to `VulkanContext.cpp`; (2) validation layers are
enabled only in debug builds and only when `VK_LAYER_KHRONOS_validation` is
actually present, so dev builds get the single highest-value Vulkan debugging
tool for free while ship builds and SDK-less machines simply log that it is off.
At P2.0 the context stops at enumeration — it lists GPUs and logs each one;
physical-device selection and queue families are P2.1.

## ADR-010 — Vulkan 1.3 minimum: dynamic rendering + sync2; scored device selection

The engine requires Vulkan 1.3 and renders with *dynamic rendering* plus
*synchronization2* — no `VkRenderPass`, no `VkFramebuffer`, ever. Render-pass
objects exist for tiler GPUs and add ~150 lines of ceremony per pass
configuration; dynamic rendering is the API shape modern engines (and our P3
PBR pass) actually want. Raising the floor to 1.3 is what buys this: hardware
that can't do 1.3 (2015-era GPUs) is not our audience. Device selection is a
scored filter, not a "pick first": a GPU is eligible only if it offers 1.3, a
graphics queue, present support *for our actual surface*, `VK_KHR_swapchain`,
and the two features above — then discrete beats integrated by a wide margin.
Ineligible hardware is skipped with a logged reason, never silently. Queue
policy: prefer one combined graphics+present family (virtually universal, lets
the swapchain use exclusive sharing); separate families are accepted as a
fallback. Process note: `Renderer.cpp` is the second and last TU that includes
GLFW — `glfwCreateWindowSurface` and nothing else — a deliberate, bounded
exception to ADR-004's "exactly one translation unit."

## ADR-011 — Shaders: checked-in SPIR-V, embedded into the binary by pure CMake

GLSL sources live in `engine/shaders/` next to their compiled `.spv`, and the
`.spv` is the *committed* artifact. At configure time, `cmake/EmbedShaders.cmake`
hex-dumps each `.spv` into a generated header of `uint32_t` arrays
(endian-corrected 32-bit words — exactly what `vkCreateShaderModule` wants), so
shaders are compiled into the executable. Three problems die at once: no
runtime shader paths to resolve (the classic "works from my IDE, breaks from
the terminal" bug), no Vulkan SDK required to *build* (CI and fresh clones stay
green — the same philosophy as volk in ADR-009), and no shader/binary version
skew. Committing a build artifact is normally a smell; here it is the
deliberate escape hatch that keeps `glslc` a dev-machine-only tool — the
`forge_shaders` CMake target (which only exists when the SDK is found)
recompiles GLSL → SPIR-V into the source tree, and the result gets committed
like any other change. This scheme is for the engine's own built-in shaders;
P5's asset pipeline owns user/game shaders and hot reload, and will replace
none of it.

## ADR-012 — Shader data policy and clip-space conventions live in ONE place

How data reaches shaders is a policy, not an ad-hoc choice per call site:
*per-frame* data (camera matrix, lights) goes in one persistently-mapped
uniform buffer per frame-in-flight, bound as descriptor set 0 — the fence that
guards the frame slot also makes its UBO safely writable, no extra sync;
*per-draw* data (model matrix) goes in push constants, staying within the
128-byte floor Vulkan guarantees (a mat4 is 64 — relying on the common 256 is
how you ship something that breaks on someone else's GPU); *per-mesh* data
(vertices/indices) is device-local, filled once through a staging buffer at
load time. Vulkan's clip-space differences from OpenGL are centralized in the
Renderer and nowhere else: `GLM_FORCE_DEPTH_ZERO_TO_ONE` is defined on the glm
*target* (a partial define is an ODR bug that renders as subtly-wrong depth),
the projection Y-flip (`proj[1][1] *= -1`) happens inside `drawFrame`, and the
resulting front-face winding (COUNTER_CLOCKWISE for CCW-authored geometry —
*empirically verified by painting normals as colors*, after a confident paper
derivation of CLOCKWISE shipped an inside-out cube) is set in exactly one
pipeline. Apps hand the engine a `Camera` and never learn Vulkan's axes.
Allocation stays raw vkAllocateMemory per buffer; VMA is the documented
trigger point when P5 asset streaming multiplies allocation counts.

## ADR-013 — Texturing lands before image loading: procedural placeholder, stb deferred

P3.2 builds the complete texture pipeline — `VkImage` + staging upload, a
GPU-side `vkCmdBlitImage` mip cascade, trilinear `VkSampler`, combined-image-
sampler descriptor — but feeds it a *procedurally generated* checkerboard, the
engine's built-in placeholder albedo. stb_image is deliberately NOT added yet:
ADR-003 says a dependency arrives with its first real consumer, and decoding
PNGs is P5's job (asset import), not P3's. The lesson of P3.2 is the
*machinery*, which is byte-identical whether texels come from a generator or a
file; deferring stb also keeps CI free of file-path/asset questions. Format
policy: albedo is `R8G8B8A8_SRGB`, so the hardware linearizes on sample and
the shader never hand-rolls `pow(2.2)` — the same "sRGB at the edges, linear
math inside" rule the swapchain already follows. Mips are generated at upload
(blit each level from the one above) rather than shipped, fine for generated
and P5-era imported textures alike until a real asset cooker exists. Sampler
anisotropy is off because it is a device *feature* we have not requested;
that request belongs with P5's oblique-floor test scenes, where it earns its
cost visibly.

## ADR-014 — Shadows: one directional 2048² map, PCF, rasterizer depth bias

Shadow mapping is the classic two-pass scheme: render scene depth from the
light's point of view into an offscreen D32 map (a depth-only pipeline — one
vertex shader, zero fragment stages, zero color attachments), then at shading
time reproject each fragment into light space and hardware-compare its depth
against the map. Policy decisions, each with its reason: (1) rasterizer-level
*depth bias* (constant 1.25, slope 1.75) fixes acne at render time — it
slope-scales per triangle, which no shader-side epsilon can do; (2) the
comparison sampler uses LINEAR filtering (free hardware 2×2 PCF per tap) under
a 3×3 kernel, and CLAMP_TO_BORDER with a *white* border so "beyond the map"
reads as lit, never as a wall of shadow; (3) the shadow pass culls *nothing* —
front-face culling (the usual peter-panning trick) leaks light through thin
receivers like our ground slab, and the bias already covers acne; (4) the
light's ortho box is fixed (±6, 0.1–20) because the demo scene is fixed —
fitting it to a real scene's bounds, and cascades for large worlds, are
documented follow-ups for when such scenes exist (P5+), not speculative code
today; (5) NO Y-flip on the light matrix: the map is rendered and sampled
through the same matrix, so the convention cancels — only the swapchain path
flips (ADR-012). Shadows attenuate *direct* light only; ambient survives,
because a shadow is the absence of one light, not the absence of light.

## ADR-015 — Jolt behind a pimpl seam; fixed 60 Hz steps through an accumulator

Jolt v5.2.0 comes in as pinned FetchContent (`SOURCE_SUBDIR Build` — its CMake
project is not at repo root), with three build-option overrides that each
prevent a real failure mode: `USE_STATIC_MSVC_RUNTIME_LIBRARY OFF` (Jolt
defaults to /MT while the rest of our world is /MD — mixed CRTs are a
link-time bomb), `INTERPROCEDURAL_OPTIMIZATION OFF` (LTO makes dev links
crawl; revisit for P10 packaging), and all sample/test/viewer targets off.
Jolt's headers are SYSTEM includes and the library links PRIVATE: exactly one
translation unit (`src/physics/PhysicsWorld.cpp`) includes Jolt, the same seam
discipline as GLFW behind Window and Vulkan behind Renderer — consumers see
glm types and opaque `BodyId` handles (Jolt's index+sequence packing rides
inside, so stale handles stay detectable). The collision taxonomy is the
canonical two-layer minimum (NonMoving/Moving; static-vs-static never reaches
the narrow phase) — more layers arrive when gameplay needs them, not before.
Simulation advances in FIXED 1/60 s ticks fed by an accumulator with a 0.1 s
frame clamp: variable steps make contacts springy and non-deterministic, and
the clamp turns a debugger pause into one slow frame instead of a 40-tick
catch-up stampede. Documented consequence (learned the honest way): feeding
`update()` bulk time simulates LESS than asked — callers step in frame-sized
slices. Units are meters/kilograms/seconds, Y-up, matching the renderer 1:1.
Render-side interpolation between ticks is deferred to P8 (invisible at
60/60); body-capacity caps (1024) are demo-sized and revisited at P10.

## ADR-016 — Asset import is a border crossing: tool formats in, plain data out

Importers convert tool formats (OBJ/glTF via Assimp v5.4.3, PNG/JPEG via
stb_image) into plain CPU data — `Vertex[]` + `uint32[]`, RGBA8 texels — and
that is the WHOLE contract: loaders never see a Vulkan type, the renderer
never sees a file, and hot reload falls out for free (reload file, walk the
same upload path). Each library owns exactly one TU under `src/assets/`.
Pinning: Assimp by release tag with importers OPT-IN per format (the
all-importers build is enormous; a format is enabled when assets actually
ship in it); stb by exact master commit SHA, dated in `Dependencies.cmake`,
because stb has no release tags. Two Assimp build overrides matter: static
CRT/`BUILD_SHARED_LIBS OFF` alignment, and `ASSIMP_INJECT_DEBUG_POSTFIX OFF`
— Assimp CACHE-writes `CMAKE_DEBUG_POSTFIX` globally, silently renaming OUR
libraries. Conventions crossing the border: `aiProcess_FlipUVs` maps OBJ/GL's
bottom-up V to our image-style UVs (v=0 top, matching stb rows and Vulkan),
and everything is forced to RGBA8 (one texel format engine-wide, ADR-013).
Committed test assets are procedurally GENERATED with provenance
(`tools/gen_torus.py`, scripted crate.png): no license questions, no
downloads, hermetic CI, and deterministic regeneration — a restored asset is
byte-identical to the committed one. Import failure throws and the headless
asset smoke fails CI loudly; a broken asset is a broken build.

## ADR-017 — Renderer resources: handle tables, descriptor sets split by frequency

GPU resources live in renderer-owned tables behind opaque `MeshHandle` /
`TextureHandle` indices, and a frame is `drawFrame(camera, span<DrawItem>)`
where a DrawItem is mesh + texture + model matrix — the scene stays the
app's (later the editor's) business; the renderer just consumes the list.
Descriptor sets are split by UPDATE FREQUENCY, the standard design this
renderer was always growing toward: set 0 is per-frame (UBO + shadow map,
bound once), set 1 is per-material (albedo, rebinds per draw; sorting draws
by material is the P8-era optimization the layout is already shaped for).
The built-in checker is texture 0, created through the same public path real
assets use, so "no assets" is always a renderable state. Hot reload is
`updateMesh`/`updateTexture`: waitIdle, rebuild the resource, rewrite the
SAME descriptor set — handles held by the app stay valid forever, which is
the entire point of handles over pointers. waitIdle is load-time quality by
declared contract; async streaming with per-frame retirement is P8+. The
file WATCHER lives in the app, not the engine: polling mtimes every 500 ms
is 3-platform-portable, imperceptibly latent, and treats a failed reload as
"retry next poll" because tools write files non-atomically.

## ADR-018 — Scripting: C++ owns machinery, Lua owns decisions; errors never kill the engine

Lua 5.4.8 + sol2 v3.5.0 behind `forge::ScriptEngine` (pimpl; one TU includes
Lua — the same seam as every subsystem before it). Lua is pinned to the
OFFICIAL repo and we write the 8-line static-lib target ourselves rather
than trusting a third-party CMake fork; sol2's template weather needs
`/bigobj` on MSVC for exactly that one TU. The division of labor is the
architecture: C++ owns everything real (memory, GPU, bodies), Lua owns
DECISIONS — and the binding surface is chosen by VAULT's future puzzle
vocabulary, not speculation: spawn a body, kick a body, read a body's
position, read input. (A pressure plate is "is that body's position inside
my box" — the primitives are already sufficient.) Error policy is the load-
bearing decision: every load and call is protected; a broken script logs
and the LAST GOOD functions stay live, runtime errors latch after one log
line instead of spamming at 60 Hz, and the P5 FileWatcher makes scripts
hot-swappable — save broken, read the error, save fixed, never restart.
Structure is ONE scene script with onStart/onUpdate hooks; per-entity script
components arrive with the editor (P7) when VAULT's rooms need them.
Rendering of script-spawned bodies stays the HOST's business via a single
onBoxSpawned hook — the engine does not decide what things look like.

## ADR-019 — Editor: embedded EditorUi over an internal hook; the scene IS the ECS

Dear ImGui v1.92.8 + ImGuizmo (master-SHA pinned; no usable release tag since
2021, and its upstream CMake project is deliberately skipped — it builds a
widget zoo without even wiring imgui includes). Architecture in three
decisions. (1) `forge::EditorUi` owns ImGui's lifecycle and GLFW/Vulkan
backends and draws INSIDE `Renderer::drawFrame` through a narrow
engine-internal hook (`src/renderer/RendererInternal.hpp`) — no Vulkan type
enters public headers, and the include-path rule (ADR-002) physically stops
apps from reaching the seam. The widget API itself is deliberately NOT
wrapped: apps include <imgui.h> and build their own panels, because a
wrapped ImGui is a worse ImGui. (2) The editor edits the ECS: entities carry
Name/Transform/MeshRenderer/Body components, the frame's DrawItems are built
by iterating the registry, and script-spawned bodies become entities — one
scene model serves gameplay, rendering, and tooling. Physics stays
authoritative for dynamic transforms; editor edits go through
`PhysicsWorld::teleport` so colliders never desync from meshes. (3) The
editor ships EMBEDDED in the app rather than as the separate `editor/`
executable ADR-001 sketched — a standalone editor earns its keep when scenes
serialize (P10-era); until then two copies of scene setup would be pure
drift risk. Convention traps recorded for posterity: ImGuizmo performs its
own Y-flip (expects GL-style NDC) so it receives the UNFLIPPED projection,
and imgui.h must precede ImGuizmo.h — an include-sorting formatter will
happily break that, hence the block separation. UI drawn under 125% DPI also
exposed that our capture tooling was silently cropping: fixed by making it
DPI-aware, closing the loop on the P3.1 "off-center cube" illusion for good.

## ADR-020 — Lean P8: audio, particles, and keyframe animation sized to VAULT

P8 ships deliberately lean, scoped by what the sample game actually consumes.
AUDIO: miniaudio 0.11.25 behind `forge::Audio` (one TU, warning-shielded
implementation — the stb/Assimp seam pattern). Backends are dlopen'd at
runtime, so building needs no audio SDK and a device-less machine (CI) gets
a disabled-but-valid engine: play() no-ops, nothing throws — volk's
philosophy applied to sound. Engine-level volume is the only knob; a real
mixer with per-voice control belongs to VAULT's week-5 audio pass.
PARTICLES: CPU burst emitter (`fx::ParticleEmitter`, header-only), rendered
as tiny mesh instances through the ordinary DrawItem path — a few hundred
draws is nothing at this scale, and GPU instancing/billboards arrive when a
profiler asks, not a feeling. Deterministic seed: same burst, same sparks,
filmable twice. ANIMATION: keyframe TRANSFORM clips (`anim::Clip`, position
tracks, lerp, looping) — doors, moving platforms, bobbing drones are VAULT's
ENTIRE animation vocabulary, and this sampling layer is the foundation GPU
skinning stands on later. SKINNED MESHES ARE EXPLICITLY DEFERRED: skinning
is the single most expensive renderer feature to do right (per the game
plan's own ranking), and a first-person game with no player character has
nothing to skin until the art pass proves otherwise. The animated platform's
collider follows via teleport; a proper KINEMATIC body type is the noted
follow-up when platforms must push bodies rather than pass through edge
cases. Lua reach: forge.audio.play and forge.fx.burst — sound and sparks
are gameplay decisions (ADR-018), so scripts own them.

## ADR-021 — VAULT week 5: a small mixer and a per-room light seam

The lean-P8 promise (ADR-020) came due: VAULT wanted an ambient bed, a music
underscore, and mood lighting that changes room to room, and the single
engine-volume knob could not carry it. Two minimal seams, both shaped by what
the game asked for, nothing more. AUDIO grew from one fire-and-forget play()
into a small self-owned mixer: every voice is now a `ma_sound` this engine
owns, carrying its OWN volume. This also fixed a latent bug — the old play()
set miniaudio's MASTER volume per call, so the last SFX silently re-leveled
every other voice. One-shots are decoded, played, and reaped by update()
(erase-remove on ma_sound_at_end); looping voices (ambient/music) are
STREAMED, not decoded into RAM, and return a `SoundHandle` for later
setVolume()/stop(). Still no 3D spatialization: VAULT is rooms and corridors,
and positional audio is weight a corridor game never spends. Lua reach:
forge.audio.loop/set_volume/stop. LIGHTING: the key light was a hardcoded
warm constant baked into drawFrame; week 5 lifts it to app-settable state on
the renderer (`Renderer::setLighting(color, dir)`) with the old warm value as
the default, so any scene that never calls it looks byte-identical to before.
A zero direction means "keep the current angle" — retint without re-stating
geometry. Because lighting mood is level data (ADR-018), scene.lua owns it:
`forge.render.set_light` driven by player z, easing between rooms with a
per-frame lerp so crossing a doorway fades colour over ~0.4 s instead of
snapping. The audio beds are two committed, deterministic generators
(tools/gen_music.py — stdlib wave, seamless equal-power crossfade loops), the
same procedural-provenance discipline as gen_sounds.py and gen_textures.py:
nothing in the tree that a script can't rebuild.


## ADR-022 — VAULT week 6: a menu FSM and checkpoint recovery

Week 6 closed the loop VAULT was missing: a way to START a run, PAUSE it, and
be TOLD you won — plus recovery when the corridor void eats you or a crate.
Two seams, both minimal, both shaped by the game and not speculation. MENU
STATE lives in the HOST, not Lua: a four-state machine (Title / Playing /
Paused / Won) layered over the existing play mode. Game/menu state is the
host's business (it owns the window, the cursor, the render loop); the script
only SIGNALS transitions it alone can know — `forge.game.win()` fires once the
finale clears, and the host raises the Won screen. Esc drives the machine in
play mode (Title/Won quit, Playing<->Paused toggle); the editor (Tab) keeps
its old Esc = quit, because the editor is a dev tool orthogonal to game state.
A single `simRun` boolean gates ALL gameplay simulation — movement, glove,
script.update, physics.update, animation, sparks — so the non-playing states
FREEZE the world behind their menu instead of running it blind under an
overlay. The menus are ImGui (already in the tree for the editor/HUD); no new
UI layer for three centred windows. CHECKPOINTS + VOID RECOVERY are Lua, since
they're level logic (ADR-018): scene.lua advances a checkpoint as the player
crosses each room's near wall, and respawns the player (or a fallen crate) to
the last safe spot when it drops below the void plane or on a manual R press —
this closes the flagged corridor-void crate-loss bug. The one engine gap it
needed: `PhysicsWorld::setCharacterPosition` (warp the CharacterVirtual and
zero its velocity — the controller owns position, so Lua can't teleport it
directly), exposed as `forge.player.teleport`. RESTART is a SOFT reset
(`resetGame()` in Lua): it returns every mutable thing — player, crates, plate
and laser latches, drone, and re-spawned shattered glass — to its start
WITHOUT rebuilding geometry, because destroyed entities can't otherwise
return and a full scene rebuild is a bigger hammer than a restart needs. A
hard `rebuild` remains the noted follow-up if a room's geometry ever needs to
change on restart.

## ADR-023 — VAULT week 7: coplanarity, plate feel, and a hint seam

Polish week, driven by playtest notes. Z-FIGHTING: doors were 0.4 m thick —
exactly the wall thickness — and 2.2 m wide — exactly the doorframe top
piece's width — sharing a z-centre, so an OPEN door slid up into perfectly
coplanar faces with its frame and shimmered. Doors are now 0.32 m, recessed
4 cm inside the frame: no coplanar pair exists in any door state, and the
recess reads as a door sitting IN a frame rather than a slab painted on the
wall. Geometry lesson logged: never give two touching boxes the same
face plane unless one hides the other. PLATE FEEL: pressure plates now
visibly sink ~8 cm when pressed and ease back up. The trick that keeps it
cheap: the DETECTION region is a fixed overlap box in space; only the visual
slab animates, so the puzzle logic never chases its own moving sensor. Pure
Lua, no engine change. TIMED PLATES: plates gain an optional holdTime — the
signal persists N seconds after the weight leaves. R4 was redesigned around
it: one crate (was two), two plates; weigh one with the crate, press the
other with your body, sprint the door before the hold expires. First
mechanic where TIME is the resource, built entirely in the generic plate
system. HINT SEAM: one new engine hook, forge.hud.set_hint(text) — the
script owns the WORDS (level knowledge, ADR-018), the host owns the pixels
(top-centred HUD line). Scripts push only on change. QoL alongside: the
crosshair flares amber when the glove's raycast lands on a grabbable body
(same test the click runs, so the affordance never lies), door motion plays
a motor thunk, and the key legend now lists Q/R. Looping-room designs for a
possible week 8 are banked in docs/PUZZLE_IDEAS.md — five candidates, two
picked, engine gaps per idea named up front.
