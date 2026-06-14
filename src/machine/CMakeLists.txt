# src/machine/CMakeLists.txt

add_library(machine STATIC
    c64.c
    c64_bus.c
    c64_rom.c
    c6510.c
    cpu.c
)

target_include_directories(machine PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(machine PUBLIC c_std_99)

target_link_libraries(machine PRIVATE
    util
)
