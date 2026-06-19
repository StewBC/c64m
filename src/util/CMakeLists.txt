# src/util/CMakeLists.txt

add_library(util STATIC
    audio_buffer.c
    config.c
    cond.c
    message_queue.c
    mutex.c
    thread.c
    util.c
)

# audio_buffer.c uses C11 _Atomic for lock-free SPSC; the rest of util stays C99.
set_source_files_properties(audio_buffer.c PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
)

target_include_directories(util PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(util PUBLIC c_std_99)

target_link_libraries(util
    PRIVATE
        SDL2::SDL2
        c64m_inih
        c64m_stb_ds
)
