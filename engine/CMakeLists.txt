# engine/CMakeLists.txt

# Checked-in SPIR-V -> generated header (ADR-011). Building needs NO Vulkan
# SDK; the .spv artifacts are the committed source of truth for CI.
include(${CMAKE_SOURCE_DIR}/cmake/EmbedShaders.cmake)
forge_embed_spirv(${CMAKE_CURRENT_BINARY_DIR}/generated/forge_gen/MeshShaders.hpp
    kMeshVert ${CMAKE_CURRENT_SOURCE_DIR}/shaders/mesh.vert.spv
    kMeshFrag ${CMAKE_CURRENT_SOURCE_DIR}/shaders/mesh.frag.spv
    kShadowVert ${CMAKE_CURRENT_SOURCE_DIR}/shaders/shadow.vert.spv
)

add_library(forge_engine STATIC
    src/core/Log.cpp
    src/platform/Window.cpp
    src/platform/Input.cpp
    src/physics/PhysicsWorld.cpp
    src/assets/MeshLoader.cpp
    src/assets/ImageLoader.cpp
    src/scripting/ScriptEngine.cpp
    src/ui/EditorUi.cpp
    src/audio/Audio.cpp
    src/renderer/VulkanContext.cpp
    src/renderer/VulkanDevice.cpp
    src/renderer/VulkanSwapchain.cpp
    src/renderer/VulkanBuffer.cpp
    src/renderer/VulkanTexture.cpp
    src/renderer/Renderer.cpp
)
add_library(forge::engine ALIAS forge_engine)  # consumers use the namespaced name

target_include_directories(forge_engine
    # Consumers (sandbox, editor) see only include/ — the public API.
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    # Engine internals can include their own private headers from src/,
    # plus the generated (embedded-shader) headers.
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_BINARY_DIR}/generated
)

# sol2's template-heavy bindings overflow MSVC's default object-file section
# limit (C1128) — raise it for the one TU that includes sol2.
if(MSVC)
    set_source_files_properties(src/scripting/ScriptEngine.cpp
        PROPERTIES COMPILE_OPTIONS /bigobj)
endif()

target_link_libraries(forge_engine
    PRIVATE
        forge_warnings
        glfw            # implementation detail: never appears in public headers
        volk            # Vulkan meta-loader; also implementation-only
        Jolt            # physics backend; confined to src/physics (ADR-015)
        assimp          # mesh import; confined to src/assets (ADR-016)
        stb             # image import; confined to src/assets (ADR-016)
        lua_static      # scripting VM; confined to src/scripting (ADR-018)
        sol2            # C++ <-> Lua binding layer; same TU
        imgui_static    # UI lifecycle/backends; confined to src/ui (ADR-019)
        miniaudio       # audio backend; confined to src/audio (ADR-020)
    PUBLIC
        glm::glm        # math types will appear in the public API
)

# Dev convenience, NOT in ALL: recompiles GLSL -> .spv in the SOURCE tree
# (the .spv is the committed artifact). Only exists when the SDK is around.
find_program(FORGE_GLSLC glslc HINTS $ENV{VULKAN_SDK}/Bin $ENV{VULKAN_SDK}/bin)
if(FORGE_GLSLC)
    add_custom_target(forge_shaders
        COMMAND ${FORGE_GLSLC} -O ${CMAKE_CURRENT_SOURCE_DIR}/shaders/mesh.vert
                -o ${CMAKE_CURRENT_SOURCE_DIR}/shaders/mesh.vert.spv
        COMMAND ${FORGE_GLSLC} -O ${CMAKE_CURRENT_SOURCE_DIR}/shaders/mesh.frag
                -o ${CMAKE_CURRENT_SOURCE_DIR}/shaders/mesh.frag.spv
        COMMAND ${FORGE_GLSLC} -O ${CMAKE_CURRENT_SOURCE_DIR}/shaders/shadow.vert
                -o ${CMAKE_CURRENT_SOURCE_DIR}/shaders/shadow.vert.spv
        COMMENT "Recompiling GLSL -> SPIR-V into the source tree (commit the result)"
    )
endif()
