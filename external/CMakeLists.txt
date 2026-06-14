add_library(c64m_stb_ds INTERFACE)
target_include_directories(c64m_stb_ds INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/stb
)

add_library(c64m_logc STATIC
    logc/log.c
)
target_include_directories(c64m_logc PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/logc
)
target_compile_features(c64m_logc PUBLIC c_std_99)

add_library(c64m_argparse STATIC
    argparse/argparse.c
)
target_include_directories(c64m_argparse PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/argparse
)
target_compile_features(c64m_argparse PUBLIC c_std_99)

add_library(c64m_inih STATIC
    inih/ini.c
)
target_include_directories(c64m_inih PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/inih
)
target_compile_features(c64m_inih PUBLIC c_std_99)

add_library(c64m_whereami STATIC
    whereami/whereami.c
)
target_include_directories(c64m_whereami PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/whereami
)
target_compile_features(c64m_whereami PUBLIC c_std_99)
