# src/machine/CMakeLists.txt

add_library(machine STATIC
    cia.c
    c64.c
    c64_bus.c
    c64_rom.c
    c6510.c
    keyboard.c
    vicii.c
)

target_include_directories(machine PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(machine PUBLIC c_std_99)

target_link_libraries(machine PRIVATE
    util
)

option(C64M_DEBUG_CIA1_WRITES "Log CIA1 register writes and timer state to stderr" OFF)
if(C64M_DEBUG_CIA1_WRITES)
    target_compile_definitions(machine PRIVATE C64M_DEBUG_CIA1_WRITES)
endif()
