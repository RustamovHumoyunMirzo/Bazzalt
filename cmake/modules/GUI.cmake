bazzalt_add_runtime_module(${BAZZALT_MODULE_TARGET}
    OUTPUT_NAME ${BAZZALT_MODULE_OUTPUT_NAME}
    SOURCES ${BAZZALT_MODULE_SOURCES} ${BAZZALT_EMBEDDED_HEADERS}
    INCLUDE_DIRECTORIES "${_bazzalt_generated_dir}"
    LIBRARIES Bazzalt Filament::Filament BazzaltCSS freetype harfbuzz SheenBidi)
