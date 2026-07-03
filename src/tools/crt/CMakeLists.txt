# src/tools/crt/CMakeLists.txt

add_library(crt STATIC
    crt.c
)

target_include_directories(crt PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(crt PUBLIC c_std_99)
