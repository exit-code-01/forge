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
