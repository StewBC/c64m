# src/frontend/CMakeLists.txt

set(C64M_HELP_CONTENT "${CMAKE_CURRENT_BINARY_DIR}/generated/help_content.inc")
set(C64M_HELP_SECTION_LEVEL "2" CACHE STRING "Markdown heading level used for help sections")

add_custom_command(
    OUTPUT "${C64M_HELP_CONTENT}"
    COMMAND ${Python3_EXECUTABLE}
        "${CMAKE_SOURCE_DIR}/tools/gen_help.py"
        --level "${C64M_HELP_SECTION_LEVEL}"
        "${CMAKE_SOURCE_DIR}/manual/manual.md"
        "${C64M_HELP_CONTENT}"
    DEPENDS
        "${CMAKE_SOURCE_DIR}/manual/manual.md"
        "${CMAKE_SOURCE_DIR}/tools/gen_help.py"
    VERBATIM
)

add_custom_target(frontend_help_content DEPENDS "${C64M_HELP_CONTENT}")

add_library(frontend STATIC
    c64_layout.c
    c64_layout.h
    frontend.c
    frontend.h
    frontend_input.c
    frontend_input.h
    help_view.c
    help_view.h
    "${C64M_HELP_CONTENT}"
    nuklear_impl.c
    nuklear_config.h
    nuklear.h
    nuklear_sdl.h
)

add_dependencies(frontend frontend_help_content)

target_include_directories(frontend PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${SDL2_INCLUDE_DIRS}
)

target_compile_features(frontend PUBLIC c_std_99)

target_link_libraries(frontend
    PUBLIC
        runtime
    PRIVATE
    platform
    SDL2::SDL2
    tools
    util
)
