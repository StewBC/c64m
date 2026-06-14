# src/tools/CMakeLists.txt

add_library(tools INTERFACE)

add_subdirectory(assembler)

target_link_libraries(tools INTERFACE
    assembler
)

