# src/tools/CMakeLists.txt

add_library(tools INTERFACE)

add_subdirectory(assembler)
add_subdirectory(d64)
add_subdirectory(disasm_6502)
add_subdirectory(symbols)
add_subdirectory(t64)

target_link_libraries(tools INTERFACE
    assembler
    d64
    disasm_6502
    symbols
    t64
)
