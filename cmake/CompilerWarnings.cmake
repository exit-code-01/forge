# cmake/CompilerWarnings.cmake
#
# One INTERFACE target carrying our warning flags. Every real target does:
#     target_link_libraries(my_target PRIVATE forge_warnings)
# PRIVATE matters: warnings apply to OUR code, never propagate to
# third-party code we FetchContent later (their warnings are their problem).

add_library(forge_warnings INTERFACE)

if(MSVC)
    target_compile_options(forge_warnings INTERFACE
        /W4
        /permissive-        # standards-conformant mode
        /w14640             # thread-unsafe static init — engine-relevant
    )
else()
    target_compile_options(forge_warnings INTERFACE
        -Wall
        -Wextra
        -Wpedantic
        -Wshadow            # shadowed vars cause real engine bugs
        -Wnon-virtual-dtor  # deleting through base ptr without virtual dtor
        -Wold-style-cast
        -Wunused
        -Woverloaded-virtual
        -Wconversion        # noisy at first, priceless in math/render code
        -Wsign-conversion
        -Wnull-dereference
        -Wdouble-promotion  # accidental float->double kills perf on GPU-adjacent code
    )
endif()
