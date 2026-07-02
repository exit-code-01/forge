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
# Our -Wall/-Wconversion arsenal audits OUR code, not vendored headers:
# mark glm's include dirs as SYSTEM for all consumers.
set_target_properties(glm PROPERTIES
    INTERFACE_SYSTEM_INCLUDE_DIRECTORIES
    "$<TARGET_PROPERTY:glm,INTERFACE_INCLUDE_DIRECTORIES>"
)
# GLM defaults to OpenGL's -1..1 clip-space depth; Vulkan uses 0..1. Set on
# the TARGET (not per-file) so every TU in every consumer agrees — a partial
# define here is an ODR bug that renders as "shadows/depth subtly wrong."
target_compile_definitions(glm INTERFACE GLM_FORCE_DEPTH_ZERO_TO_ONE)

# ---- Vulkan-Headers + volk (P2.0) — consumed by: engine/src/renderer
# volk dlopens the Vulkan loader at RUNTIME, so building the repo never
# requires the LunarG SDK; machines without Vulkan fail gracefully at startup.
FetchContent_Declare(vulkan_headers
    GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Headers.git
    GIT_TAG        v1.3.290
    GIT_SHALLOW    TRUE
)
set(VOLK_PULL_IN_VULKAN ON)
FetchContent_Declare(volk
    GIT_REPOSITORY https://github.com/zeux/volk.git
    GIT_TAG        vulkan-sdk-1.3.290.0
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(vulkan_headers volk)
# volk's auto-detection of the headers target is version-sensitive; wire it
# explicitly so a volk/headers version bump can't silently break the build.
if(TARGET volk AND TARGET Vulkan::Headers)
    target_link_libraries(volk PUBLIC Vulkan::Headers)
endif()
