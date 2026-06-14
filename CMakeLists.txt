cmake_minimum_required(VERSION 3.28)

project(c64m
    VERSION 0.1.0
    LANGUAGES C
)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_LINK_LIBRARIES_STRATEGY REORDER_FREELY)
enable_testing()

find_package(SDL2 CONFIG REQUIRED)

add_subdirectory(external)

add_subdirectory(src/util)
add_subdirectory(src/machine)
add_subdirectory(src/runtime)
add_subdirectory(src/platform)
add_subdirectory(src/tools)
add_subdirectory(src/frontend)

add_executable(c64m
    src/app_options.c
    src/main.c
)

target_compile_features(c64m PRIVATE c_std_99)

target_link_libraries(c64m PRIVATE
    c64m_argparse
    runtime
    platform
    frontend
    tools
    util
)

add_executable(test_c64_bus
    tests/machine/test_c64_bus.c
)

target_compile_features(test_c64_bus PRIVATE c_std_99)

target_link_libraries(test_c64_bus PRIVATE
    machine
)

add_test(NAME c64_bus COMMAND test_c64_bus)

add_executable(test_c64_rom
    tests/machine/test_c64_rom.c
)

target_compile_features(test_c64_rom PRIVATE c_std_99)

target_link_libraries(test_c64_rom PRIVATE
    machine
)

add_test(NAME c64_rom COMMAND test_c64_rom)

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
