# cmake/Dependencies.cmake
#
# All third-party deps live here, pinned to exact tags. Rule (ADR-003):
# a dependency is added in the same commit as its first consumer, never before.

include(FetchContent)

# ---- GLFW 3.4 (windowing + input) — consumed by: engine/src/platform/Window.cpp
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)
FetchContent_Declare(glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.4
    GIT_SHALLOW    TRUE
)

# ---- GLM 1.0.1 (math) — header-only; consumed by: engine public API (P1+)
FetchContent_Declare(glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG        1.0.1
    GIT_SHALLOW    TRUE
)

FetchContent_MakeAvailable(glfw glm)
