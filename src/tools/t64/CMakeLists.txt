# src/tools/t64/CMakeLists.txt

add_library(t64 STATIC
    t64.c
)

target_include_directories(t64 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(t64 PUBLIC c_std_99)
