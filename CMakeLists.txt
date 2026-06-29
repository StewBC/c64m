cmake_minimum_required(VERSION 3.28)

project(c64m
    VERSION 0.1.0
    LANGUAGES C
)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_LINK_LIBRARIES_STRATEGY REORDER_FREELY)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
if(MSVC)
    add_compile_options($<$<COMPILE_LANGUAGE:C>:/experimental:c11atomics>)
    add_link_options(/STACK:8388608)
endif()
enable_testing()

find_package(SDL2 CONFIG REQUIRED)
find_package(Python3 COMPONENTS Interpreter REQUIRED)

add_subdirectory(external)

add_subdirectory(src/util)
add_subdirectory(src/machine)
add_subdirectory(src/runtime)
add_subdirectory(src/platform)
add_subdirectory(src/control)
add_subdirectory(src/tools)
add_subdirectory(src/frontend)

add_executable(c64m
    src/app_options.c
    src/main.c
)

target_compile_features(c64m PRIVATE c_std_99)

target_compile_definitions(c64m PRIVATE SDL_MAIN_HANDLED)

target_link_libraries(c64m PRIVATE
    c64m_argparse
    control
    runtime
    platform
    frontend
    tools
    util
    SDL2::SDL2
)

target_include_directories(c64m PRIVATE
    ${SDL2_INCLUDE_DIRS}
)

add_executable(test_audio_buffer
    tests/util/test_audio_buffer.c
)

target_compile_features(test_audio_buffer PRIVATE c_std_99)

target_link_libraries(test_audio_buffer PRIVATE
    util
)

add_test(NAME audio_buffer COMMAND test_audio_buffer)

add_executable(test_paste_parser
    tests/util/test_paste_parser.c
)

target_compile_features(test_paste_parser PRIVATE c_std_99)

target_link_libraries(test_paste_parser PRIVATE
    util
)

add_test(NAME paste_parser COMMAND test_paste_parser)

add_executable(test_c64_bus
    tests/machine/test_c64_bus.c
)

target_compile_features(test_c64_bus PRIVATE c_std_99)

target_link_libraries(test_c64_bus PRIVATE
    machine
)

add_test(NAME c64_bus COMMAND test_c64_bus)

add_executable(test_sid
    tests/machine/test_sid.c
)

target_compile_features(test_sid PRIVATE c_std_99)

target_link_libraries(test_sid PRIVATE
    machine
    util
)

add_test(NAME sid COMMAND test_sid)

add_executable(test_via6522
    tests/machine/test_via6522.c
)

target_compile_features(test_via6522 PRIVATE c_std_99)

target_link_libraries(test_via6522 PRIVATE
    machine
)

add_test(NAME via6522 COMMAND test_via6522)

add_executable(test_c1541
    tests/machine/test_c1541.c
)

target_compile_features(test_c1541 PRIVATE c_std_99)

target_link_libraries(test_c1541 PRIVATE
    machine
)

add_test(NAME c1541 COMMAND test_c1541)

add_executable(test_c64_rom
    tests/machine/test_c64_rom.c
)

target_compile_features(test_c64_rom PRIVATE c_std_99)

target_link_libraries(test_c64_rom PRIVATE
    machine
)

add_test(NAME c64_rom COMMAND test_c64_rom)

add_executable(test_c64_frame
    tests/machine/test_c64_frame.c
)

target_compile_features(test_c64_frame PRIVATE c_std_99)

target_link_libraries(test_c64_frame PRIVATE
    machine
)

add_test(NAME c64_frame COMMAND test_c64_frame)

add_executable(test_c64_vicii
    tests/machine/test_c64_vicii.c
)

target_compile_features(test_c64_vicii PRIVATE c_std_99)

target_link_libraries(test_c64_vicii PRIVATE
    machine
)

add_test(NAME c64_vicii COMMAND test_c64_vicii)

add_executable(test_c64_cia
    tests/machine/test_c64_cia.c
)

target_compile_features(test_c64_cia PRIVATE c_std_99)

target_link_libraries(test_c64_cia PRIVATE
    machine
)

add_test(NAME c64_cia COMMAND test_c64_cia)

add_executable(test_c64_boot_progression
    tests/machine/test_c64_boot_progression.c
)

target_compile_features(test_c64_boot_progression PRIVATE c_std_99)

target_compile_definitions(test_c64_boot_progression PRIVATE
    C64M_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(test_c64_boot_progression PRIVATE
    machine
)

add_test(NAME c64_boot_progression COMMAND test_c64_boot_progression)

add_executable(test_c64_keyboard
    tests/machine/test_c64_keyboard.c
)

target_compile_features(test_c64_keyboard PRIVATE c_std_99)

target_link_libraries(test_c64_keyboard PRIVATE
    machine
)

add_test(NAME c64_keyboard COMMAND test_c64_keyboard)

add_executable(test_c64_disk_load
    tests/machine/test_c64_disk_load.c
)

target_compile_features(test_c64_disk_load PRIVATE c_std_99)

target_compile_definitions(test_c64_disk_load PRIVATE
    C64M_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(test_c64_disk_load PRIVATE
    machine
    d64
)

add_test(NAME c64_disk_load COMMAND test_c64_disk_load)

add_executable(test_c64_real_1541_load
    tests/machine/test_c64_real_1541_load.c
)

target_compile_features(test_c64_real_1541_load PRIVATE c_std_99)

target_compile_definitions(test_c64_real_1541_load PRIVATE
    C64M_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(test_c64_real_1541_load PRIVATE
    machine
)

add_test(NAME c64_real_1541_load COMMAND test_c64_real_1541_load)

add_executable(test_frontend_input
    tests/frontend/test_frontend_input.c
)

target_compile_features(test_frontend_input PRIVATE c_std_99)

target_compile_definitions(test_frontend_input PRIVATE SDL_MAIN_HANDLED)

target_include_directories(test_frontend_input PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/frontend
    ${CMAKE_CURRENT_SOURCE_DIR}/src/machine
)

target_link_libraries(test_frontend_input PRIVATE
    frontend
)

add_test(NAME frontend_input COMMAND test_frontend_input)

add_executable(test_c64_cpu_validation
    tests/machine/test_c64_cpu_validation.c
)

target_compile_features(test_c64_cpu_validation PRIVATE c_std_99)

target_link_libraries(test_c64_cpu_validation PRIVATE
    runtime
)

add_test(NAME c64_cpu_validation COMMAND test_c64_cpu_validation)

add_executable(test_app_options
    src/app_options.c
    tests/test_app_options.c
)

target_compile_features(test_app_options PRIVATE c_std_99)

target_include_directories(test_app_options PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(test_app_options PRIVATE
    c64m_argparse
    util
)

add_test(NAME app_options COMMAND test_app_options)

add_executable(test_control_protocol
    tests/control/test_control_protocol.c
)

target_compile_features(test_control_protocol PRIVATE c_std_99)

target_link_libraries(test_control_protocol PRIVATE
    control
)

add_test(NAME control_protocol COMMAND test_control_protocol)

add_executable(test_disasm_6502
    tests/tools/test_disasm_6502.c
)

target_compile_features(test_disasm_6502 PRIVATE c_std_99)

target_link_libraries(test_disasm_6502 PRIVATE
    disasm_6502
)

add_test(NAME disasm_6502 COMMAND test_disasm_6502)

add_executable(test_symbol_table
    tests/tools/test_symbol_table.c
)

target_compile_features(test_symbol_table PRIVATE c_std_99)

target_link_libraries(test_symbol_table PRIVATE
    symbols
)

add_test(NAME symbol_table COMMAND test_symbol_table)

add_executable(test_d64
    tests/tools/test_d64.c
)

target_compile_features(test_d64 PRIVATE c_std_99)

target_compile_definitions(test_d64 PRIVATE
    C64M_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(test_d64 PRIVATE
    d64
)

add_test(NAME d64 COMMAND test_d64)

add_executable(profile_c64_hotloop
    tools/profile_c64_hotloop.c
)

target_compile_features(profile_c64_hotloop PRIVATE c_std_99)

target_link_libraries(profile_c64_hotloop PRIVATE
    machine
)

add_executable(profile_runtime_hotloop
    tools/profile_runtime_hotloop.c
)

target_compile_features(profile_runtime_hotloop PRIVATE c_std_99)

target_link_libraries(profile_runtime_hotloop PRIVATE
    runtime
)

add_executable(test_assembler_conditionals
    tests/tools/test_assembler_conditionals.c
)

target_compile_features(test_assembler_conditionals PRIVATE c_std_99)

target_link_libraries(test_assembler_conditionals PRIVATE
    assembler
)

add_test(NAME assembler_conditionals COMMAND test_assembler_conditionals)

add_executable(test_assembler_loops
    tests/tools/test_assembler_loops.c
)

target_compile_features(test_assembler_loops PRIVATE c_std_99)

target_link_libraries(test_assembler_loops PRIVATE
    assembler
)

add_test(NAME assembler_loops COMMAND test_assembler_loops)

add_executable(test_assembler_macros
    tests/tools/test_assembler_macros.c
)

target_compile_features(test_assembler_macros PRIVATE c_std_99)

target_link_libraries(test_assembler_macros PRIVATE
    assembler
)

add_test(NAME assembler_macros COMMAND test_assembler_macros)

add_executable(test_assembler_scopes_segments
    tests/tools/test_assembler_scopes_segments.c
)

target_compile_features(test_assembler_scopes_segments PRIVATE c_std_99)

target_link_libraries(test_assembler_scopes_segments PRIVATE
    assembler
)

add_test(NAME assembler_scopes_segments COMMAND test_assembler_scopes_segments)

add_executable(test_runtime_romboot
    tests/runtime/test_runtime_romboot.c
)

target_compile_features(test_runtime_romboot PRIVATE c_std_99)

target_link_libraries(test_runtime_romboot PRIVATE
    runtime
)

add_test(NAME runtime_romboot COMMAND test_runtime_romboot)

add_executable(test_runtime_scheduler
    tests/runtime/test_runtime_scheduler.c
)

target_compile_features(test_runtime_scheduler PRIVATE c_std_99)

target_link_libraries(test_runtime_scheduler PRIVATE
    runtime
)

add_test(NAME runtime_scheduler COMMAND test_runtime_scheduler)

add_executable(test_runtime_frame
    tests/runtime/test_runtime_frame.c
)

target_compile_features(test_runtime_frame PRIVATE c_std_99)

target_link_libraries(test_runtime_frame PRIVATE
    runtime
)

add_test(NAME runtime_frame COMMAND test_runtime_frame)

add_executable(test_runtime_disk
    tests/runtime/test_runtime_disk.c
)

target_compile_features(test_runtime_disk PRIVATE c_std_99)

target_compile_definitions(test_runtime_disk PRIVATE
    C64M_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(test_runtime_disk PRIVATE
    runtime
)

add_test(NAME runtime_disk COMMAND test_runtime_disk)

add_executable(test_runtime_real_1541_autorun
    tests/runtime/test_runtime_real_1541_autorun.c
)

target_compile_features(test_runtime_real_1541_autorun PRIVATE c_std_99)

target_compile_definitions(test_runtime_real_1541_autorun PRIVATE
    C64M_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(test_runtime_real_1541_autorun PRIVATE
    runtime
)

add_test(NAME runtime_real_1541_autorun COMMAND test_runtime_real_1541_autorun)

add_executable(test_runtime_assembler
    tests/runtime/test_runtime_assembler.c
)

target_compile_features(test_runtime_assembler PRIVATE c_std_99)

target_link_libraries(test_runtime_assembler PRIVATE
    runtime
)

add_test(NAME runtime_assembler COMMAND test_runtime_assembler)

add_executable(test_runtime_fileio
    tests/runtime/test_runtime_fileio.c
)

target_compile_features(test_runtime_fileio PRIVATE c_std_99)

target_link_libraries(test_runtime_fileio PRIVATE
    runtime
)

add_test(NAME runtime_fileio COMMAND test_runtime_fileio)

add_executable(test_runtime_stepping
    tests/runtime/test_runtime_stepping.c
)

target_compile_features(test_runtime_stepping PRIVATE c_std_99)

target_link_libraries(test_runtime_stepping PRIVATE
    runtime
)

add_test(NAME runtime_stepping COMMAND test_runtime_stepping)
