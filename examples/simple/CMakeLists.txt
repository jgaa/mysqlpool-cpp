

project(simple_sql_app
    DESCRIPTION "Simple example on how to use mysqlpool-cpp"
    LANGUAGES CXX
)

message("examples: LOGFAULT_INCLUDEDIR=${LOGFAULT_INCLUDEDIR}")

add_executable(${PROJECT_NAME} main.cpp fun_with_sql.cpp)
set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD 20)
target_link_libraries(${PROJECT_NAME} mysqlpool ${MYSQLPOOL_BOOST_PROGRAM_OPTIONS})

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_INCLUDE_PATH}
    $<BUILD_INTERFACE:${Boost_INCLUDE_DIR}>
    $<BUILD_INTERFACE:${MYSQLPOOL_ROOT}/include>
    $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/generated-include>
    $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/external-projects/installed/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    ${LOGFAULT_INCLUDEDIR}
    )

