# engine/CMakeLists.txt
add_library(forge_engine STATIC
    src/core/Log.cpp
    src/platform/Window.cpp
    src/platform/Input.cpp
)
add_library(forge::engine ALIAS forge_engine)  # consumers use the namespaced name

target_include_directories(forge_engine
    # Consumers (sandbox, editor) see only include/ — the public API.
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    # Engine internals can include their own private headers from src/.
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(forge_engine
    PRIVATE
        forge_warnings
        glfw            # implementation detail: never appears in public headers
    PUBLIC
        glm::glm        # math types will appear in the public API
)
