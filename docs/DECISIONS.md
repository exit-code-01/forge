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
