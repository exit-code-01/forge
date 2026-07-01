# sandbox/CMakeLists.txt
add_executable(forge_sandbox
    src/main.cpp
)

target_link_libraries(forge_sandbox
    PRIVATE
        forge::engine
        forge_warnings
)
