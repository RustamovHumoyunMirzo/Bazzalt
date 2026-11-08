include("${PROJECT_SOURCE_DIR}/cmake/Recast.cmake")
bazzalt_add_runtime_module(${BAZZALT_MODULE_TARGET}
    OUTPUT_NAME ${BAZZALT_MODULE_OUTPUT_NAME} SOURCES ${BAZZALT_MODULE_SOURCES}
    LIBRARIES Bazzalt BazzaltRecast BazzaltDetour BazzaltDetourCrowd BazzaltDetourTileCache)
