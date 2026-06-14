# src/util/CMakeLists.txt

add_library(util STATIC
    config.c
    cond.c
    message_queue.c
    mutex.c
    thread.c
    util.c
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
