project(mysqlpool VERSION ${MYSQLPOOL_VERSION} LANGUAGES CXX)

add_library(${PROJECT_NAME}
    ${MYSQLPOOL_ROOT}/include/mysqlpool/mysqlpool.h
    ${MYSQLPOOL_ROOT}/include/mysqlpool/logging.h
    ${MYSQLPOOL_ROOT}/include/mysqlpool/conf.h
    mysqlpool.cpp
    ${LOGGING_SRC}
)

target_include_directories(${PROJECT_NAME} PUBLIC
    $<BUILD_INTERFACE:${Boost_INCLUDE_DIR}>
    $<BUILD_INTERFACE:${MYSQLPOOL_ROOT}/include>
    $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/generated-include>
    $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/external-projects/installed/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE
    $Boost_INCLUDE_DIR
    ${LOGFAULT_INCLUDEDIR}
    )

set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD 20)

if (MYSQLPOOL_LOGDEP)
    message("Adding logfault dependency to the library")
    add_dependencies(${PROJECT_NAME} ${MYSQLPOOL_LOGDEP})
endif()

if(NOT MSVC)
    set(STANDARD_FS_LIB stdc++fs)
endif()

target_link_libraries(${PROJECT_NAME}
    ${Boost_LIBRARIES}
    OpenSSL::SSL
    OpenSSL::Crypto
    ZLIB::ZLIB
    ${STANDARD_FS_LIB}
    ${CMAKE_THREAD_LIBS_INIT}
)

message(STATUS "Components to pack (${PROJECT_NAME}): ${CPACK_COMPONENTS_ALL}")

install(DIRECTORY ${CMAKE_BINARY_DIR}/generated-include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
include(${MYSQLPOOL_ROOT}/cmake/install.cmake)

