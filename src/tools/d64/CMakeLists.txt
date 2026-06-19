# src/tools/d64/CMakeLists.txt

add_library(d64 STATIC
    d64.c
)

target_include_directories(d64 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(d64 PUBLIC c_std_99)
