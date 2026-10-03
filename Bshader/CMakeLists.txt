cmake_minimum_required(VERSION 3.15)

project(bshader
    VERSION 1.0.0
    DESCRIPTION "Bshader: a high-level shading language and compiler"
    LANGUAGES C
)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(BSHADER_BUILD_TESTS "Build the bshader test executable" ON)
option(BSHADER_BUILD_SHARED "Build bshader as a shared library instead of static" OFF)

# ---------------------------------------------------------------------------
# Library target
# ---------------------------------------------------------------------------

set(BSHADER_SOURCES
    src/arena.c
    src/diag.c
    src/lexer.c
    src/ast.c
    src/parser.c
    src/strbuf.c
    src/strset.c
    src/transform.c
    src/generator_filament.c
    src/shader_translator.c
)

if(BSHADER_BUILD_SHARED)
    add_library(bshader SHARED ${BSHADER_SOURCES})
else()
    add_library(bshader STATIC ${BSHADER_SOURCES})
endif()

add_library(bshader::bshader ALIAS bshader)

target_include_directories(bshader
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

if(MSVC)
    # MSVC warns about standard C flexible array members even in C11 mode.
    target_compile_options(bshader PRIVATE /W4 /WX /wd4200)
    target_compile_definitions(bshader PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(bshader PRIVATE -Wall -Wextra -Werror)
endif()

set_target_properties(bshader PROPERTIES
    C_VISIBILITY_PRESET hidden
    POSITION_INDEPENDENT_CODE ON
    PUBLIC_HEADER include/shader_translator.h
)

# ---------------------------------------------------------------------------
# Test executable
# ---------------------------------------------------------------------------

if(BSHADER_BUILD_TESTS)
    enable_testing()

    add_executable(bshader_tests tests/test_basic.c)
    target_link_libraries(bshader_tests PRIVATE bshader::bshader)

    if(MSVC)
        target_compile_options(bshader_tests PRIVATE /W4)
    else()
        target_compile_options(bshader_tests PRIVATE -Wall -Wextra)
    endif()

    add_test(NAME bshader_basic_tests COMMAND bshader_tests)
endif()

# ---------------------------------------------------------------------------
# Install rules
# ---------------------------------------------------------------------------

include(GNUInstallDirs)

install(TARGETS bshader
    EXPORT bshaderTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(EXPORT bshaderTargets
    FILE bshaderTargets.cmake
    NAMESPACE bshader::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/bshader
)
