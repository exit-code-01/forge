# CMakeLists.txt (root)
cmake_minimum_required(VERSION 3.24)

project(Forge
    VERSION 0.1.0
    DESCRIPTION "Forge Engine - a from-scratch(ish) 3D game engine"
    LANGUAGES CXX
)

# ---- Global language settings (project-wide, non-negotiable) ----
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# clangd / clang-tidy support out of the box
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Sane default when the user forgets -DCMAKE_BUILD_TYPE
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo)
endif()

# Keep all binaries in one predictable place: build/bin, build/lib
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Warnings live in their own module so every target opts in identically
include(cmake/CompilerWarnings.cmake)
include(cmake/Dependencies.cmake)

# ---- Project layout ----
add_subdirectory(engine)   # forge::engine (static lib)
add_subdirectory(sandbox)  # forge_sandbox (executable, links engine)

# P7 will add: add_subdirectory(editor)
