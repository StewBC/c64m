# src/machine/CMakeLists.txt

add_library(machine STATIC
    cia.c
    c1541.c
    c1541_gcr.c
    c1541_media.c
    c64.c
    c64_bus.c
    c64_rom.c
    c64_snapshot.c
    c6510.c
    keyboard.c
    sid.c
    via6522.c
    vicii.c
)

target_include_directories(machine PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(machine PUBLIC c_std_99)

target_link_libraries(machine PRIVATE
    d64
    util
)
