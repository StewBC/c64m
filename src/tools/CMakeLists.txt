# src/tools/CMakeLists.txt

add_library(tools INTERFACE)

add_subdirectory(assembler)
add_subdirectory(disasm_6502)

target_link_libraries(tools INTERFACE
    assembler
    disasm_6502
)
