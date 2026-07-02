# sandbox/CMakeLists.txt
add_executable(forge_sandbox
    src/main.cpp
)

target_link_libraries(forge_sandbox
    PRIVATE
        forge::engine
        forge_warnings
        imgui_static    # the app BUILDS panels; the widget API is the product
        imguizmo_static # transform gizmos on the ImGui draw list
)
