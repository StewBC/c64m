# src/runtime/CMakeLists.txt

add_library(runtime STATIC
    runtime_assembler.c
    runtime_client.c
    runtime_breakpoint_ini.c
    runtime_command.c
    runtime_event.c
    runtime.c
    runtime_thread.c
)

target_include_directories(runtime PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(runtime PUBLIC c_std_99)

target_link_libraries(runtime
    PUBLIC
        machine
        assembler
        d64
        symbols
        util
    PRIVATE
        SDL2::SDL2
)
