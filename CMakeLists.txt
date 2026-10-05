cmake_minimum_required(VERSION 3.16)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/releases/versions.json" _bazzalt_versions)
string(REGEX MATCH "\"core\"[ \t\r\n]*:[ \t\r\n]*\"([0-9]+\\.[0-9]+\\.[0-9]+)\"" _bazzalt_core_version "${_bazzalt_versions}")
if(NOT _bazzalt_core_version)
    message(FATAL_ERROR "releases/versions.json must declare a semantic core version")
endif()
project(BAZZALT VERSION "${CMAKE_MATCH_1}" LANGUAGES C CXX)
find_package(Python COMPONENTS Interpreter REQUIRED)

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

set(BAZZALT_SDL3_DIR "${CMAKE_CURRENT_SOURCE_DIR}/deps/SDL3" CACHE PATH "SDL3 source tree")
if(NOT EXISTS "${BAZZALT_SDL3_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "SDL3 is missing. Run scripts/get_sdl3.ps1 or scripts/get_sdl3.sh.")
endif()
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
# Qt owns native windows, and Filament owns rendering. SDL is input-only.
foreach(_subsystem AUDIO VIDEO GPU RENDER CAMERA JOYSTICK HAPTIC SENSOR DIALOG)
    set(SDL_${_subsystem} OFF CACHE BOOL "" FORCE)
endforeach()
add_subdirectory("${BAZZALT_SDL3_DIR}" "deps/SDL3" EXCLUDE_FROM_ALL)
if(NOT WIN32)
    set_target_properties(SDL3-static PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()

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
bazzalt_embed_post_process(editor_guide EditorGuideFilamat)
bazzalt_embed_post_process(editor_selection_mask EditorSelectionMaskFilamat)
bazzalt_embed_post_process(editor_selection_outline EditorSelectionOutlineFilamat)
bazzalt_embed_post_process(editor_grid EditorGridFilamat)
bazzalt_embed_post_process(editor_icon EditorIconFilamat)
bazzalt_embed_post_process(editor_unlit EditorUnlitFilamat)
bazzalt_embed_post_process(editor_lighting_only EditorLightingOnlyFilamat)
bazzalt_embed_post_process(editor_overdraw EditorOverdrawFilamat)
bazzalt_embed_post_process(editor_wireframe EditorWireframeFilamat)
bazzalt_embed_post_process(primitive_lit PrimitiveLitFilamat)
bazzalt_embed_binary(Editor/assets/gizmos/singlearrowx.glb EditorTranslateGizmoGlb editor_translate_gizmo_glb)
bazzalt_embed_binary(Editor/assets/gizmos/scalearrowx.glb EditorScaleGizmoGlb editor_scale_gizmo_glb)
bazzalt_embed_binary(Editor/assets/gizmos/rotring.glb EditorRotateGizmoGlb editor_rotate_gizmo_glb)

function(bazzalt_embed_svg_icon name symbol)
    set(_rgba "${_bazzalt_generated_dir}/${name}.rgba")
    set(_header "${_bazzalt_generated_dir}/${name}_rgba.h")
    add_custom_command(OUTPUT "${_header}"
        COMMAND "${Python_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/cmake/RasterizeSvg.py"
                "${CMAKE_CURRENT_SOURCE_DIR}/Editor/assets/icons/${name}.svg" "${_rgba}" 64
        COMMAND "${CMAKE_COMMAND}" -DINPUT=${_rgba} -DOUTPUT=${_header} -DSYMBOL=${symbol}
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedBinary.cmake"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Editor/assets/icons/${name}.svg"
                "${CMAKE_CURRENT_SOURCE_DIR}/cmake/RasterizeSvg.py"
                "${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedBinary.cmake" VERBATIM)
    set(BAZZALT_EMBEDDED_HEADERS ${BAZZALT_EMBEDDED_HEADERS} "${_header}" PARENT_SCOPE)
endfunction()
bazzalt_embed_svg_icon(scene_cam SceneCameraIconRgba)
bazzalt_embed_svg_icon(scene_light SceneLightIconRgba)

# Environments are project assets, imported on demand; no hidden default lighting.

file(GLOB_RECURSE LIB_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)
list(FILTER LIB_SOURCES EXCLUDE REGEX "/EditorBridge/")

file(GLOB_RECURSE LIB_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/*.h"
)

add_library(Bazzalt SHARED
    ${LIB_SOURCES}
    ${LIB_HEADERS}
    ${BAZZALT_EMBEDDED_HEADERS}
)
set_target_properties(Bazzalt PROPERTIES POSITION_INDEPENDENT_CODE ON
    WINDOWS_EXPORT_ALL_SYMBOLS ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Editor"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Editor"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/ScriptSDK/lib"
    BUILD_RPATH "$ORIGIN" INSTALL_RPATH "$ORIGIN")
target_compile_definitions(Bazzalt PRIVATE BAZZALT_BUILDING_LIBRARY)
# SDK staged beside the editor: public headers only, with the EnTT dependency.
add_custom_command(TARGET Bazzalt POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/include/Bazzalt" "${CMAKE_BINARY_DIR}/ScriptSDK/include/Bazzalt"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${BAZZALT_ENTT_DIR}/src/entt" "${CMAKE_BINARY_DIR}/ScriptSDK/include/entt"
    COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:Bazzalt>" "${CMAKE_BINARY_DIR}/ScriptSDK/lib/$<TARGET_FILE_NAME:Bazzalt>"
    COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_LINKER_FILE:Bazzalt>" "${CMAKE_BINARY_DIR}/ScriptSDK/lib/$<TARGET_LINKER_FILE_NAME:Bazzalt>"
    VERBATIM)

target_include_directories(Bazzalt
    PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
    PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${_bazzalt_generated_dir}"
)

target_link_libraries(Bazzalt PUBLIC EnTT::EnTT PRIVATE SDL3::SDL3-static ryml::ryml Filament::Filament ${CMAKE_DL_LIBS})

target_compile_features(Bazzalt PUBLIC cxx_std_20)
if(MSVC)
    # Public API strings and checked-in sources use UTF-8, regardless of the
    # developer/CI machine's ANSI code page (including Unicode path literals).
    target_compile_options(Bazzalt PUBLIC /utf-8)
endif()
# Match these flags to the linked Filament SDK when using a custom build.
option(BAZZALT_FILAMENT_OPENGL "SDK contains the OpenGL backend" ON)
if(APPLE)
    set(_bazzalt_vulkan_default OFF)
else()
    set(_bazzalt_vulkan_default ON)
endif()
option(BAZZALT_FILAMENT_VULKAN "SDK contains the Vulkan backend" ${_bazzalt_vulkan_default})
option(BAZZALT_FILAMENT_METAL "SDK contains the Metal backend" ${APPLE})
option(BAZZALT_FILAMENT_WEBGPU "SDK contains the WebGPU backend" OFF)
target_compile_definitions(Bazzalt PRIVATE
    BAZZALT_FILAMENT_OPENGL=$<BOOL:${BAZZALT_FILAMENT_OPENGL}>
    BAZZALT_FILAMENT_VULKAN=$<BOOL:${BAZZALT_FILAMENT_VULKAN}>
    BAZZALT_FILAMENT_METAL=$<BOOL:${BAZZALT_FILAMENT_METAL}>
    BAZZALT_FILAMENT_WEBGPU=$<BOOL:${BAZZALT_FILAMENT_WEBGPU}>)
target_compile_definitions(Bazzalt PRIVATE
    BAZZALT_MATC_EXECUTABLE="${BAZZALT_MATC_EXECUTABLE}"
    BAZZALT_FILAMESH_EXECUTABLE="${BAZZALT_FILAMESH_EXECUTABLE}"
    BAZZALT_CMGEN_EXECUTABLE="${BAZZALT_CMGEN_EXECUTABLE}"
)

option(BAZZALT_BUILD_EDITOR_BRIDGE "Build the private Python editor runtime bridge" ON)
if(BAZZALT_BUILD_EDITOR_BRIDGE)
    find_package(Python COMPONENTS Development.Module REQUIRED)
    # Resolve pybind11 as a package. Avoid a conditional add_subdirectory for
    # the optional, normally-empty dependency folder: IDE CMake parsers inspect
    # that call without evaluating EXISTS and report a false missing-CMakeLists
    # diagnostic.
    find_package(pybind11 CONFIG QUIET)
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
    set(BSHADER_BUILD_SHARED ON CACHE BOOL "" FORCE)
    set(BSHADER_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    add_subdirectory(Bshader EXCLUDE_FROM_ALL)
    set_target_properties(bshader PROPERTIES OUTPUT_NAME bshad
        C_VISIBILITY_PRESET default WINDOWS_EXPORT_ALL_SYMBOLS ON
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Editor"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Editor")
    add_dependencies(_bazzalt_runtime bshader)
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
if(WIN32)
    add_custom_command(TARGET BazzaltExample POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:Bazzalt>" "$<TARGET_FILE_DIR:BazzaltExample>/$<TARGET_FILE_NAME:Bazzalt>")
endif()

include(CTest)
if(BUILD_TESTING)
    function(bazzalt_enable_test_assertions target)
        target_sources(${target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/tests/TestDiagnostics.cpp")
        if(WIN32)
            add_custom_command(TARGET ${target} POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:Bazzalt>" "$<TARGET_FILE_DIR:${target}>/$<TARGET_FILE_NAME:Bazzalt>")
        endif()
        if(MSVC)
            target_compile_options(${target} PRIVATE /UNDEBUG)
        else()
            target_compile_options(${target} PRIVATE -UNDEBUG)
        endif()
    endfunction()
    add_executable(BazzaltMathTests
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/MathTests.cpp"
    )
    add_executable(BazzaltTimeTests tests/TimeTests.cpp)
    target_link_libraries(BazzaltTimeTests PRIVATE Bazzalt)
    add_executable(BazzaltInputTests tests/InputTests.cpp)
    target_link_libraries(BazzaltInputTests PRIVATE Bazzalt)
    target_include_directories(BazzaltInputTests PRIVATE src)
    bazzalt_enable_test_assertions(BazzaltInputTests)
    add_test(NAME BazzaltInputTests COMMAND BazzaltInputTests $<TARGET_FILE:BazzaltScriptLifecycleModule>)
    add_executable(BazzaltMaterialTests tests/MaterialTests.cpp)
    target_link_libraries(BazzaltMaterialTests PRIVATE Bazzalt Filament::Filament)
    target_include_directories(BazzaltMaterialTests PRIVATE src)
    target_compile_definitions(BazzaltMaterialTests PRIVATE BAZZALT_MATERIAL_TEST_PACKAGE="${_bazzalt_generated_dir}/primitive_lit.filamat")
    bazzalt_enable_test_assertions(BazzaltMaterialTests)
    add_test(NAME BazzaltMaterialTests COMMAND BazzaltMaterialTests $<TARGET_FILE:BazzaltScriptLifecycleModule>)
    target_include_directories(BazzaltTimeTests PRIVATE include src)
    bazzalt_enable_test_assertions(BazzaltTimeTests)
    add_test(NAME BazzaltTimeTests COMMAND BazzaltTimeTests)
    target_link_libraries(BazzaltMathTests PRIVATE Bazzalt)
    bazzalt_enable_test_assertions(BazzaltMathTests)
    add_test(NAME BazzaltMathTests COMMAND BazzaltMathTests)

    add_executable(BazzaltEcsTests
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/EcsTests.cpp"
    )
    target_link_libraries(BazzaltEcsTests PRIVATE Bazzalt)
    bazzalt_enable_test_assertions(BazzaltEcsTests)
    add_test(NAME BazzaltEcsTests COMMAND BazzaltEcsTests)

    add_executable(BazzaltSerializationTests
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/SerializationTests.cpp"
    )
    target_link_libraries(BazzaltSerializationTests PRIVATE Bazzalt)
    target_include_directories(BazzaltSerializationTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    bazzalt_enable_test_assertions(BazzaltSerializationTests)
    add_test(NAME BazzaltSerializationTests COMMAND BazzaltSerializationTests)

    add_executable(BazzaltRenderingTests
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/RenderingTests.cpp"
    )
    target_link_libraries(BazzaltRenderingTests PRIVATE Bazzalt Filament::Filament)
    target_include_directories(BazzaltRenderingTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    bazzalt_enable_test_assertions(BazzaltRenderingTests)
    add_test(NAME BazzaltRenderingTests COMMAND BazzaltRenderingTests)
    add_executable(BazzaltModelRenderingTests tests/ModelRenderingTests.cpp)
    target_link_libraries(BazzaltModelRenderingTests PRIVATE Bazzalt Filament::Filament)
    target_include_directories(BazzaltModelRenderingTests PRIVATE src)
    bazzalt_enable_test_assertions(BazzaltModelRenderingTests)
    add_test(NAME BazzaltModelRenderingTests COMMAND BazzaltModelRenderingTests)

    add_executable(BazzaltAssetPipelineTests
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/AssetPipelineTests.cpp"
    )
    target_link_libraries(BazzaltAssetPipelineTests PRIVATE Bazzalt)
    target_include_directories(BazzaltAssetPipelineTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    bazzalt_enable_test_assertions(BazzaltAssetPipelineTests)
    add_test(NAME BazzaltAssetPipelineTests COMMAND BazzaltAssetPipelineTests)

    add_library(BazzaltScriptLifecycleModule MODULE tests/fixtures/ScriptLifecycleModule.cpp)
    target_link_libraries(BazzaltScriptLifecycleModule PRIVATE Bazzalt)
    set_target_properties(BazzaltScriptLifecycleModule PROPERTIES PREFIX "")
    add_library(BazzaltPublicApiModule MODULE tests/fixtures/PublicApiModule.cpp)
    target_link_libraries(BazzaltPublicApiModule PRIVATE Bazzalt)
    set_target_properties(BazzaltPublicApiModule PROPERTIES PREFIX "")
    add_executable(BazzaltPublicScriptApiTests tests/PublicScriptApiTests.cpp)
    target_include_directories(BazzaltPublicScriptApiTests PRIVATE src)
    target_link_libraries(BazzaltPublicScriptApiTests PRIVATE Bazzalt)
    bazzalt_enable_test_assertions(BazzaltPublicScriptApiTests)
    add_test(NAME BazzaltPublicScriptApiTests COMMAND BazzaltPublicScriptApiTests $<TARGET_FILE:BazzaltPublicApiModule>)
    add_executable(BazzaltNativeScriptRuntimeTests tests/NativeScriptRuntimeTests.cpp)
    target_include_directories(BazzaltNativeScriptRuntimeTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include" "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(BazzaltNativeScriptRuntimeTests PRIVATE Bazzalt ${CMAKE_DL_LIBS})
    bazzalt_enable_test_assertions(BazzaltNativeScriptRuntimeTests)
    add_test(NAME BazzaltNativeScriptRuntimeTests COMMAND BazzaltNativeScriptRuntimeTests $<TARGET_FILE:BazzaltScriptLifecycleModule>)
endif()
