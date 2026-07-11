# src/tools/g64/CMakeLists.txt

add_library(g64 STATIC
    g64.c
)

target_include_directories(g64 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(g64 PUBLIC c_std_99)
