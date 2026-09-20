function(bazzalt_configure_filament ROOT)
    if(NOT EXISTS "${ROOT}/include/filament/Engine.h")
        message(FATAL_ERROR
            "Filament SDK not found at '${ROOT}'. Run scripts/get_filament.ps1 "
            "on Windows or scripts/get_filament.sh on POSIX.")
    endif()

    if(WIN32)
        set(_filament_arch x86_64)
        set(_filament_debug_dir "${ROOT}/lib/${_filament_arch}/mdd")
        set(_filament_release_dir "${ROOT}/lib/${_filament_arch}/md")
        set(_filament_suffix ".lib")
        set(_filament_prefix "")
    elseif(APPLE)
        set(_filament_arch x86_64)
        set(_filament_debug_dir "${ROOT}/lib/${_filament_arch}")
        set(_filament_release_dir "${ROOT}/lib/${_filament_arch}")
        set(_filament_suffix ".a")
        set(_filament_prefix "lib")
    else()
        set(_filament_arch x86_64)
        set(_filament_debug_dir "${ROOT}/lib/${_filament_arch}")
        set(_filament_release_dir "${ROOT}/lib/${_filament_arch}")
        set(_filament_suffix ".a")
        set(_filament_prefix "lib")
    endif()

    set(_filament_libraries
        gltfio gltfio_core filameshio filamat shaders matp uberarchive uberzlib
        dracodec meshoptimizer mikktspace basis_transcoder stb image imageio-lite
        ktxreader filament backend bluegl bluevk filabridge filaflat utils smol-v
        geometry ibl-lite zstd
    )
    set(_filament_targets)
    foreach(_library IN LISTS _filament_libraries)
        set(_target "BazzaltFilament_${_library}")
        add_library(${_target} STATIC IMPORTED GLOBAL)
        set_target_properties(${_target} PROPERTIES
            IMPORTED_LOCATION_DEBUG "${_filament_debug_dir}/${_filament_prefix}${_library}${_filament_suffix}"
            IMPORTED_LOCATION_RELEASE "${_filament_release_dir}/${_filament_prefix}${_library}${_filament_suffix}"
            IMPORTED_LOCATION_RELWITHDEBINFO "${_filament_release_dir}/${_filament_prefix}${_library}${_filament_suffix}"
            IMPORTED_LOCATION_MINSIZEREL "${_filament_release_dir}/${_filament_prefix}${_library}${_filament_suffix}"
        )
        list(APPEND _filament_targets ${_target})
    endforeach()

    add_library(BazzaltFilament INTERFACE)
    add_library(Filament::Filament ALIAS BazzaltFilament)
    target_include_directories(BazzaltFilament INTERFACE "${ROOT}/include")
    target_link_libraries(BazzaltFilament INTERFACE ${_filament_targets})
    if(WIN32)
        target_link_libraries(BazzaltFilament INTERFACE opengl32 user32 gdi32 shlwapi)
    elseif(APPLE)
        target_link_libraries(BazzaltFilament INTERFACE
            "-framework Cocoa" "-framework Metal" "-framework CoreVideo" "-framework QuartzCore")
    else()
        target_link_libraries(BazzaltFilament INTERFACE dl pthread)
    endif()
endfunction()
