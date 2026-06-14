# src/assembler/CMakeLists.txt

add_library(assembler STATIC
    assembler.c
)

target_include_directories(assembler PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(assembler PUBLIC c_std_99)

target_link_libraries(assembler PRIVATE
    util
)