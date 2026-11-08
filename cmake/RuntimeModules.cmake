include_guard(GLOBAL)

# A provider owns its implementation sources. Core adapters stay in src/Runtime
# or the feature directory and are deliberately outside this source list.
function(bazzalt_add_runtime_module target)
    cmake_parse_arguments(MODULE "" "OUTPUT_NAME" "SOURCES;LIBRARIES;INCLUDE_DIRECTORIES" ${ARGN})
    if(MODULE_UNPARSED_ARGUMENTS OR NOT MODULE_OUTPUT_NAME OR NOT MODULE_SOURCES)
        message(FATAL_ERROR "${target}: provide OUTPUT_NAME and SOURCES")
    endif()
    get_property(_owned GLOBAL PROPERTY BAZZALT_RUNTIME_IMPLEMENTATION_SOURCES)
    foreach(_source IN LISTS MODULE_SOURCES)
        if(_source MATCHES "\\.(cpp|c|mm)$")
            get_filename_component(_absolute "${_source}" REALPATH BASE_DIR "${PROJECT_SOURCE_DIR}")
            if(_absolute IN_LIST _owned)
                message(FATAL_ERROR "Runtime implementation has multiple owners: ${_absolute}")
            endif()
            list(APPEND _owned "${_absolute}")
        endif()
    endforeach()
    set_property(GLOBAL PROPERTY BAZZALT_RUNTIME_IMPLEMENTATION_SOURCES "${_owned}")
    add_library(${target} SHARED ${MODULE_SOURCES})
    target_include_directories(${target} PRIVATE "${PROJECT_SOURCE_DIR}/include"
        "${PROJECT_SOURCE_DIR}/src" ${MODULE_INCLUDE_DIRECTORIES})
    target_link_libraries(${target} PRIVATE ${MODULE_LIBRARIES})
    target_compile_features(${target} PRIVATE cxx_std_20)
    set(_rpath "$ORIGIN")
    if(APPLE)
        set(_rpath "@loader_path")
    endif()
    set_target_properties(${target} PROPERTIES OUTPUT_NAME "${MODULE_OUTPUT_NAME}"
        POSITION_INDEPENDENT_CODE ON
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Editor"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Editor"
        BUILD_RPATH "${_rpath}" INSTALL_RPATH "${_rpath}")
    if(MSVC)
        target_compile_options(${target} PRIVATE /utf-8 /bigobj)
    endif()
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/ScriptSDK/lib"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:${target}>"
            "${CMAKE_BINARY_DIR}/ScriptSDK/lib/$<TARGET_FILE_NAME:${target}>" VERBATIM)
    set_property(GLOBAL APPEND PROPERTY BAZZALT_RUNTIME_MODULE_TARGETS ${target})
endfunction()

function(bazzalt_load_runtime_module id target output provider)
    set(BAZZALT_MODULE_TARGET "${target}")
    set(BAZZALT_MODULE_OUTPUT_NAME "${output}")
    set(BAZZALT_MODULE_SOURCES ${ARGN})
    # Exclude the original implementation even when a studio substitutes a provider.
    foreach(_source IN LISTS BAZZALT_MODULE_SOURCES)
        get_filename_component(_absolute "${_source}" REALPATH BASE_DIR "${PROJECT_SOURCE_DIR}")
        set_property(GLOBAL APPEND PROPERTY BAZZALT_RUNTIME_EXCLUDED_SOURCES "${_absolute}")
    endforeach()
    set(BAZZALT_${id}_PROVIDER "${provider}" CACHE FILEPATH "CMake provider for ${target}")
    if(NOT EXISTS "${BAZZALT_${id}_PROVIDER}")
        message(FATAL_ERROR "Missing ${target} provider: ${BAZZALT_${id}_PROVIDER}")
    endif()
    include("${BAZZALT_${id}_PROVIDER}")
    get_property(_registered GLOBAL PROPERTY BAZZALT_RUNTIME_MODULE_TARGETS)
    if(NOT TARGET ${target} OR NOT target IN_LIST _registered)
        message(FATAL_ERROR "${target} provider must call bazzalt_add_runtime_module for ${target}")
    endif()
    get_target_property(_output ${target} OUTPUT_NAME)
    if(NOT _output STREQUAL output)
        message(FATAL_ERROR "${target} provider must preserve runtime name ${output}")
    endif()
endfunction()

# Hosts and native tests use this list instead of repeating sibling DLL names.
function(bazzalt_stage_runtime target)
    get_property(_modules GLOBAL PROPERTY BAZZALT_RUNTIME_MODULE_TARGETS)
    set(_runtime Bazzalt BazzaltCrypto BazzaltLua ${_modules})
    add_dependencies(${target} ${_runtime})
    if(WIN32)
        set(_files)
        foreach(_module IN LISTS _runtime)
            list(APPEND _files "$<TARGET_FILE:${_module}>")
        endforeach()
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${_files}
                "$<TARGET_FILE_DIR:${target}>" VERBATIM)
    endif()
endfunction()

function(bazzalt_finalize_runtime)
    get_property(_modules GLOBAL PROPERTY BAZZALT_RUNTIME_MODULE_TARGETS)
    add_custom_target(BazzaltRuntime DEPENDS Bazzalt BazzaltCrypto BazzaltLua ${_modules})
    set(_entries)
    foreach(_target Bazzalt BazzaltCrypto BazzaltLua ${_modules})
        list(APPEND _entries "\"$<TARGET_FILE_NAME:${_target}>\"")
    endforeach()
    if(UNIX AND NOT ANDROID)
        list(APPEND _entries "\"$<TARGET_LINKER_FILE_NAME:BazzaltLua>\""
            "\"$<TARGET_SONAME_FILE_NAME:BazzaltLua>\"")
    endif()
    string(JOIN ",\n    " _libraries ${_entries})
    string(TOLOWER "${CMAKE_SYSTEM_NAME}" _platform)
    if(APPLE)
        set(_platform macos)
    endif()
    file(GENERATE OUTPUT "$<TARGET_FILE_DIR:Bazzalt>/runtime-libraries.json"
        CONTENT "{\n  \"format_version\": 1,\n  \"platform\": \"${_platform}\",\n  \"libraries\": [\n    ${_libraries}\n  ]\n}\n")
endfunction()
