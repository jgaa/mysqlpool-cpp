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
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
    $<INSTALL_INTERFACE:include>
    PRIVATE $Boost_INCLUDE_DIR
    )

set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD 20)

if (MYSQLPOOL_LOGDEP)
    add_dependencies(${PROJECT_NAME} ${MYSQLPOOL_LOGDEP})
endif()

target_link_libraries(${PROJECT_NAME}
    PRIVATE
        $<BUILD_INTERFACE:${Boost_LIBRARIES}>
        $<BUILD_INTERFACE:${stdc++fs}>
        $<BUILD_INTERFACE:Threads::Threads>
        $<BUILD_INTERFACE:ZLIB>
    )

message(STATUS "Components to pack (${PROJECT_NAME}): ${CPACK_COMPONENTS_ALL}")

