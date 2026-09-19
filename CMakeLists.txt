cmake_minimum_required(VERSION 3.15)
project(BAZZALT VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(BAZZALT_ENTT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/deps/entt" CACHE PATH
    "Path to an EnTT source tree")

if(NOT EXISTS "${BAZZALT_ENTT_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "EnTT was not found at '${BAZZALT_ENTT_DIR}'. "
        "Run scripts/get_entt.ps1 on Windows or scripts/get_entt.sh on POSIX, "
        "then configure BAZZALT again.")
endif()

add_subdirectory("${BAZZALT_ENTT_DIR}" EXCLUDE_FROM_ALL)

file(GLOB_RECURSE LIB_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)

file(GLOB_RECURSE LIB_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/*.h"
)

add_library(Bazzalt STATIC
    ${LIB_SOURCES}
    ${LIB_HEADERS}
)

target_include_directories(Bazzalt
    PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(Bazzalt PUBLIC EnTT::EnTT)

target_compile_features(Bazzalt PUBLIC cxx_std_17)

add_executable(BazzaltExample
    "${CMAKE_CURRENT_SOURCE_DIR}/examples/main.cpp"
)

target_link_libraries(BazzaltExample
    PRIVATE
    Bazzalt
)

include(CTest)
if(BUILD_TESTING)
    add_executable(BazzaltMathTests
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/MathTests.cpp"
    )
    target_link_libraries(BazzaltMathTests PRIVATE Bazzalt)
    add_test(NAME BazzaltMathTests COMMAND BazzaltMathTests)

    add_executable(BazzaltEcsTests
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/EcsTests.cpp"
    )
    target_link_libraries(BazzaltEcsTests PRIVATE Bazzalt)
    add_test(NAME BazzaltEcsTests COMMAND BazzaltEcsTests)
endif()
