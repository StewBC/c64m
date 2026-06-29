# src/control/CMakeLists.txt

add_library(control STATIC
    control_protocol.c
    control_protocol.h
    control_server.c
    control_server.h
)

target_include_directories(control PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(control PUBLIC c_std_99)

target_link_libraries(control
    PUBLIC
        platform
        util
    PRIVATE
        SDL2::SDL2
)

