# src/runtime/CMakeLists.txt

add_library(runtime STATIC
    runtime_assembler.c
    runtime_client.c
    runtime_breakpoint_condition.c
    runtime_breakpoint_ini.c
    runtime_command.c
    runtime_event.c
    runtime_frame_ring.c
    runtime_vic_ring.c
    runtime_history.c
    runtime_history_query_parse.c
    runtime_history_wire.c
    runtime_inspector.c
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
        crt
        d64
        symbols
        t64
        util
    PRIVATE
        SDL2::SDL2
)
