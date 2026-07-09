# src/tools/c64masm/CMakeLists.txt
# Command-line front-end for the shared 6502 assembler library.

add_executable(c64masm
    main.c
)

target_compile_features(c64masm PRIVATE c_std_99)

target_link_libraries(c64masm PRIVATE
    assembler
)

# Emit the binary in the build root, next to the c64m executable, rather than
# under build/src/tools/c64masm/ where subdirectory targets default to.
set_target_properties(c64masm PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)
