include(GNUInstallDirs)
install(TARGETS ${PROJECT_NAME} EXPORT ${PROJECT_NAME}Targets
    ARCHIVE  DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY  DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME  DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
install(DIRECTORY ${CMAKE_SOURCE_DIR}/include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

include(GenerateExportHeader)
generate_export_header(${PROJECT_NAME}
    EXPORT_MACRO_NAME EXPORT
    NO_EXPORT_MACRO_NAME NO_EXPORT
    PREFIX_NAME MYSQLPOOL_
    EXPORT_FILE_NAME ${CMAKE_BINARY_DIR}/include-exports/${PROJECT_NAME}/export.h)
target_include_directories(${PROJECT_NAME}
    PUBLIC $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/include-exports> $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
install(DIRECTORY ${CMAKE_BINARY_DIR}/include-exports/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

include(CMakePackageConfigHelpers)
set_property(TARGET ${PROJECT_NAME} PROPERTY VERSION ${PROJECT_VERSION})
set_property(TARGET ${PROJECT_NAME} PROPERTY SOVERSION ${PROJECT_VERSION_MAJOR})
set_property(TARGET ${PROJECT_NAME} PROPERTY INTERFACE_${PROJECT_NAME}_MAJOR_VERSION ${PROJECT_VERSION_MAJOR})
set_property(TARGET ${PROJECT_NAME} APPEND PROPERTY COMPATIBLE_INTERFACE_STRING ${PROJECT_VERSION_MAJOR})
write_basic_package_version_file(
    "${CMAKE_BINARY_DIR}/CMakePackage/${PROJECT_NAME}ConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)
export(EXPORT ${PROJECT_NAME}Targets
    FILE "${CMAKE_BINARY_DIR}/CMakePackage/${PROJECT_NAME}.cmake"
)
SET(CONFIG_SOURCE_DIR ${CMAKE_SOURCE_DIR})
SET(CONFIG_DIR ${CMAKE_BINARY_DIR})
SET(${PROJECT_NAME}_INCLUDE_DIR "\${${PROJECT_NAME}_SOURCE_DIR}/include")
configure_package_config_file(${CMAKE_SOURCE_DIR}/cmake/${PROJECT_NAME}Config.cmake.in
    "${CMAKE_BINARY_DIR}/CMakePackage/${PROJECT_NAME}Config.cmake"
    INSTALL_DESTINATION lib/cmake/${PROJECT_NAME}
    PATH_VARS ${PROJECT_NAME}_INCLUDE_DIR)
install(EXPORT ${PROJECT_NAME}Targets
    FILE ${PROJECT_NAME}.cmake
    DESTINATION lib/cmake/${PROJECT_NAME}
)
install(
    FILES
        "${CMAKE_BINARY_DIR}/CMakePackage/${PROJECT_NAME}Config.cmake"
        "${CMAKE_BINARY_DIR}/CMakePackage/${PROJECT_NAME}ConfigVersion.cmake"
    DESTINATION lib/cmake/${PROJECT_NAME}
    COMPONENT Devel
)
