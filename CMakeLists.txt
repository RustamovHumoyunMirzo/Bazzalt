cmake_minimum_required(VERSION 3.15)
project(BAZZALT VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
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

set(BAZZALT_RYML_DIR "${CMAKE_CURRENT_SOURCE_DIR}/deps/rapidyaml" CACHE PATH
    "Path to a rapidyaml source tree")
if(NOT EXISTS "${BAZZALT_RYML_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "rapidyaml was not found at '${BAZZALT_RYML_DIR}'. "
        "Run scripts/get_rapidyaml.ps1 on Windows or scripts/get_rapidyaml.sh on POSIX.")
endif()
set(RYML_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(RYML_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(RYML_INSTALL OFF CACHE BOOL "" FORCE)
set(RYML_DEFAULT_CALLBACK_USES_EXCEPTIONS ON CACHE BOOL "" FORCE)
add_subdirectory("${BAZZALT_RYML_DIR}" EXCLUDE_FROM_ALL)

set(BAZZALT_FILAMENT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/deps/filament" CACHE PATH
    "Path to the extracted Filament SDK")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/Filament.cmake")
bazzalt_configure_filament("${BAZZALT_FILAMENT_DIR}")

if(WIN32)
    set(_bazzalt_tool_suffix ".exe")
else()
    set(_bazzalt_tool_suffix "")
endif()
set(BAZZALT_MATC_EXECUTABLE "${BAZZALT_FILAMENT_DIR}/bin/matc${_bazzalt_tool_suffix}" CACHE FILEPATH
    "Filament material compiler")
set(BAZZALT_FILAMESH_EXECUTABLE "${BAZZALT_FILAMENT_DIR}/bin/filamesh${_bazzalt_tool_suffix}" CACHE FILEPATH
    "Filament mesh compiler")

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
    PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_link_libraries(Bazzalt PUBLIC EnTT::EnTT PRIVATE ryml::ryml Filament::Filament)

target_compile_features(Bazzalt PUBLIC cxx_std_20)
target_compile_definitions(Bazzalt PRIVATE
    BAZZALT_MATC_EXECUTABLE="${BAZZALT_MATC_EXECUTABLE}"
    BAZZALT_FILAMESH_EXECUTABLE="${BAZZALT_FILAMESH_EXECUTABLE}"
)

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

    add_executable(BazzaltSerializationTests
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/SerializationTests.cpp"
    )
    target_link_libraries(BazzaltSerializationTests PRIVATE Bazzalt)
    target_include_directories(BazzaltSerializationTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    add_test(NAME BazzaltSerializationTests COMMAND BazzaltSerializationTests)

    add_executable(BazzaltRenderingTests
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/RenderingTests.cpp"
    )
    target_link_libraries(BazzaltRenderingTests PRIVATE Bazzalt)
    target_include_directories(BazzaltRenderingTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    add_test(NAME BazzaltRenderingTests COMMAND BazzaltRenderingTests)

    add_executable(BazzaltAssetPipelineTests
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/AssetPipelineTests.cpp"
    )
    target_link_libraries(BazzaltAssetPipelineTests PRIVATE Bazzalt)
    target_include_directories(BazzaltAssetPipelineTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    add_test(NAME BazzaltAssetPipelineTests COMMAND BazzaltAssetPipelineTests)
endif()
