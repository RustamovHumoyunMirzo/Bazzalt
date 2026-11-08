include("${PROJECT_SOURCE_DIR}/cmake/Jolt.cmake")
bazzalt_add_runtime_module(${BAZZALT_MODULE_TARGET}
    OUTPUT_NAME ${BAZZALT_MODULE_OUTPUT_NAME} SOURCES ${BAZZALT_MODULE_SOURCES}
    LIBRARIES Bazzalt Bazzalt::JoltDependency)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SIZEOF_VOID_P EQUAL 4)
    target_link_libraries(${BAZZALT_MODULE_TARGET} PRIVATE atomic)
endif()
