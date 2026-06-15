# src/frontend/CMakeLists.txt

add_library(frontend STATIC
    c64_layout.c
    c64_layout.h
    frontend.c
    frontend.h
    frontend_input.c
    frontend_input.h
    nuklear_impl.c
    nuklear_config.h
    nuklear.h
    nuklear_sdl.h
)

target_include_directories(frontend PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(frontend PUBLIC c_std_99)

target_link_libraries(frontend
    PUBLIC
        runtime
    PRIVATE
    platform
    SDL2::SDL2
    util
)
