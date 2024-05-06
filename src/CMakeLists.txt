project(mysqlpool VERSION ${MYSQLPOOL_VERSION} LANGUAGES CXX)

message("src: LOGFAULT_INCLUDEDIR=${LOGFAULT_INCLUDEDIR}")

add_library(${PROJECT_NAME}
    ${MYSQLPOOL_ROOT}/include/mysqlpool/mysqlpool.h
    ${MYSQLPOOL_ROOT}/include/mysqlpool/logging.h
    ${MYSQLPOOL_ROOT}/include/mysqlpool/conf.h
    mysqlpool.cpp
    ${LOGGING_SRC}
)

target_include_directories(${PROJECT_NAME}
    PRIVATE
    ${CMAKE_INCLUDE_PATH}
    ${Boost_INCLUDE_DIR}
    ${LOGFAULT_INCLUDEDIR}
    $<BUILD_INTERFACE:${Boost_INCLUDE_DIR}>
    $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/external-projects/installed/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
    PUBLIC
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/generated-include>
    $<BUILD_INTERFACE:${MYSQLPOOL_ROOT}/include>
    )

set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD 20)

if (MYSQLPOOL_LOGDEP)
    message("Adding logfault dependency to the library")
    add_dependencies(${PROJECT_NAME} ${MYSQLPOOL_LOGDEP})
endif()

target_link_libraries(${PROJECT_NAME}
    ${Boost_LIBRARIES}
    OpenSSL::SSL
    OpenSSL::Crypto
    ZLIB::ZLIB
    ${CMAKE_THREAD_LIBS_INIT}
)

if (DEFINED CONAN_EXPORTED)
    target_link_libraries(${PROJECT_NAME} logfault::logfault)
endif()

message(STATUS "Components to pack (${PROJECT_NAME}): ${CPACK_COMPONENTS_ALL}")

install(DIRECTORY ${CMAKE_BINARY_DIR}/generated-include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
include(${MYSQLPOOL_ROOT}/cmake/install.cmake)

