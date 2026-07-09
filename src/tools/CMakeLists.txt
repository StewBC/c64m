# src/tools/CMakeLists.txt

add_library(tools INTERFACE)

add_subdirectory(assembler)
add_subdirectory(c64masm)
add_subdirectory(crt)
add_subdirectory(d64)
add_subdirectory(disasm_6502)
add_subdirectory(symbols)
add_subdirectory(t64)

target_link_libraries(tools INTERFACE
    assembler
    crt
    d64
    disasm_6502
    symbols
    t64
)
