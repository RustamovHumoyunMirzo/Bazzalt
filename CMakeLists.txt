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
set(BAZZALT_CMGEN_EXECUTABLE "${BAZZALT_FILAMENT_DIR}/bin/cmgen${_bazzalt_tool_suffix}" CACHE FILEPATH
    "Filament environment compiler")

set(_bazzalt_generated_dir "${CMAKE_CURRENT_BINARY_DIR}/generated")
function(bazzalt_embed_post_process name symbol)
    set(_source "${CMAKE_CURRENT_SOURCE_DIR}/runtime/resources/shaders/${name}.mat")
    set(_binary "${_bazzalt_generated_dir}/${name}.filamat")
    set(_header "${_bazzalt_generated_dir}/${name}_filamat.h")
    add_custom_command(
        OUTPUT "${_header}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${_bazzalt_generated_dir}"
        COMMAND "${BAZZALT_MATC_EXECUTABLE}" --platform desktop --api all
                --output "${_binary}" "${_source}"
        COMMAND "${CMAKE_COMMAND}" -DINPUT=${_binary} -DOUTPUT=${_header}
                -DSYMBOL=${symbol} -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedBinary.cmake"
        DEPENDS "${_source}" "${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedBinary.cmake"
        VERBATIM
        COMMENT "Compiling and embedding ${name}.mat"
    )
    set(BAZZALT_EMBEDDED_HEADERS ${BAZZALT_EMBEDDED_HEADERS} "${_header}" PARENT_SCOPE)
endfunction()

function(bazzalt_embed_binary source symbol output_name)
    set(_header "${_bazzalt_generated_dir}/${output_name}.h")
    add_custom_command(
        OUTPUT "${_header}"
        COMMAND "${CMAKE_COMMAND}" -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/${source}
                -DOUTPUT=${_header} -DSYMBOL=${symbol}
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedBinary.cmake"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${source}"
                "${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedBinary.cmake"
        VERBATIM COMMENT "Embedding ${source}"
    )
    set(BAZZALT_EMBEDDED_HEADERS ${BAZZALT_EMBEDDED_HEADERS} "${_header}" PARENT_SCOPE)
endfunction()

bazzalt_embed_post_process(gaussian_blur GaussianBlurFilamat)
bazzalt_embed_post_process(vignette VignetteFilamat)
bazzalt_embed_post_process(editor_gizmo EditorGizmoFilamat)
bazzalt_embed_binary(Editor/assets/gizmos/singlearrowx.glb EditorTranslateGizmoGlb editor_translate_gizmo_glb)
bazzalt_embed_binary(Editor/assets/gizmos/scalearrowx.glb EditorScaleGizmoGlb editor_scale_gizmo_glb)
bazzalt_embed_binary(Editor/assets/gizmos/rotring.glb EditorRotateGizmoGlb editor_rotate_gizmo_glb)

set(_bazzalt_environment_dir "${_bazzalt_generated_dir}/environment")
set(_bazzalt_environment_ibl "${_bazzalt_environment_dir}/environment_ibl.ktx")
set(_bazzalt_environment_skybox "${_bazzalt_environment_dir}/environment_skybox.ktx")
set(_bazzalt_environment_ibl_header "${_bazzalt_generated_dir}/default_environment_ibl.h")
set(_bazzalt_environment_skybox_header "${_bazzalt_generated_dir}/default_environment_skybox.h")
add_custom_command(
    OUTPUT "${_bazzalt_environment_ibl_header}" "${_bazzalt_environment_skybox_header}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${_bazzalt_environment_dir}"
    COMMAND "${BAZZALT_CMGEN_EXECUTABLE}" --quiet --size=128 --format=ktx
            --deploy "${_bazzalt_environment_dir}" "${CMAKE_CURRENT_SOURCE_DIR}/Editor/assets/default_environ.exr"
    COMMAND "${CMAKE_COMMAND}" -DINPUT=${_bazzalt_environment_ibl}
            -DOUTPUT=${_bazzalt_environment_ibl_header} -DSYMBOL=DefaultEnvironmentIbl
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedBinary.cmake"
    COMMAND "${CMAKE_COMMAND}" -DINPUT=${_bazzalt_environment_skybox}
            -DOUTPUT=${_bazzalt_environment_skybox_header} -DSYMBOL=DefaultEnvironmentSkybox
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedBinary.cmake"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Editor/assets/default_environ.exr"
            "${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedBinary.cmake"
    VERBATIM COMMENT "Compiling and embedding the default Filament environment"
)
list(APPEND BAZZALT_EMBEDDED_HEADERS
    "${_bazzalt_environment_ibl_header}" "${_bazzalt_environment_skybox_header}")

file(GLOB_RECURSE LIB_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)
list(FILTER LIB_SOURCES EXCLUDE REGEX "/EditorBridge/")

file(GLOB_RECURSE LIB_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/*.h"
)

add_library(Bazzalt STATIC
    ${LIB_SOURCES}
    ${LIB_HEADERS}
    ${BAZZALT_EMBEDDED_HEADERS}
)
set_target_properties(Bazzalt PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_include_directories(Bazzalt
    PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
    PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${_bazzalt_generated_dir}"
)

target_link_libraries(Bazzalt PUBLIC EnTT::EnTT PRIVATE ryml::ryml Filament::Filament)

target_compile_features(Bazzalt PUBLIC cxx_std_20)
target_compile_definitions(Bazzalt PRIVATE
    BAZZALT_MATC_EXECUTABLE="${BAZZALT_MATC_EXECUTABLE}"
    BAZZALT_FILAMESH_EXECUTABLE="${BAZZALT_FILAMESH_EXECUTABLE}"
)

option(BAZZALT_BUILD_EDITOR_BRIDGE "Build the private Python editor runtime bridge" ON)
if(BAZZALT_BUILD_EDITOR_BRIDGE)
    find_package(Python COMPONENTS Interpreter Development.Module REQUIRED)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/deps/pybind11/CMakeLists.txt")
        add_subdirectory(deps/pybind11 EXCLUDE_FROM_ALL)
        set(pybind11_FOUND TRUE)
    else()
        find_package(pybind11 CONFIG QUIET)
    endif()
    if(NOT pybind11_FOUND)
        execute_process(COMMAND "${Python_EXECUTABLE}" -m pybind11 --cmakedir
            OUTPUT_VARIABLE _bazzalt_pybind11_dir OUTPUT_STRIP_TRAILING_WHITESPACE
            RESULT_VARIABLE _bazzalt_pybind11_result)
        if(_bazzalt_pybind11_result EQUAL 0)
            list(APPEND CMAKE_PREFIX_PATH "${_bazzalt_pybind11_dir}")
            find_package(pybind11 CONFIG REQUIRED)
        else()
            message(FATAL_ERROR "pybind11 is required for BAZZALT_BUILD_EDITOR_BRIDGE")
        endif()
    endif()
    pybind11_add_module(_bazzalt_runtime MODULE src/EditorBridge/PythonModule.cpp)
    target_link_libraries(_bazzalt_runtime PRIVATE Bazzalt)
    target_include_directories(_bazzalt_runtime PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    set_target_properties(_bazzalt_runtime PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Editor"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Editor")
endif()

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
