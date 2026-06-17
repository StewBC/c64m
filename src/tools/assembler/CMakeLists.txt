# src/tools/assembler/CMakeLists.txt

add_library(assembler STATIC
    asm.c
    define.c
    dynarray.c
    emit.c
    errorlog.c
    err.c
    expr.c
    file.c
    gperf.c
    opcode.c
    parse.c
    scope.c
    segment.c
    symbol.c
    token.c
)

target_include_directories(assembler PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(assembler PUBLIC c_std_99)
