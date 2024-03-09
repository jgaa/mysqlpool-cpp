####### unit_tests

add_executable(unit_tests
    unit_tests.cpp
    )

set_property(TARGET unit_tests PROPERTY CXX_STANDARD 20)

add_dependencies(unit_tests
    mysqlpool
    ${DEPENDS_GTEST}
    )

target_include_directories(unit_tests
    PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}
    $<BUILD_INTERFACE:${MYSQLPOOL_ROOT}/include
    $<BUILD_INTERFACE:${MYSQLPOOL_ROOT}/src/lib
    $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/generated-include>
    $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/external-projects/installed/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
    )

target_link_libraries(unit_tests
    ${GTEST_LIBRARIES}
    mysqlpool
    # ${Boost_LIBRARIES}
    # ${ZLIB_LIBRARIES}
    # ${OPENSSL_LIBRARIES}
    # stdc++fs
    # ${CMAKE_THREAD_LIBS_INIT}
)

add_test(NAME unit_tests COMMAND unit_tests)

message("GTEST_LIBRARIES: ${GTEST_LIBRARIES}")

####### integration_tests
if (MYSQLPOOL_WITH_INTGRATION_TESTS)

add_executable(integration_tests
    integration_tests.cpp
    )

set_property(TARGET integration_tests PROPERTY CXX_STANDARD 20)

add_dependencies(integration_tests
    mysqlpool
    ${DEPENDS_GTEST}
    )

target_include_directories(integration_tests
    PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}
    $<BUILD_INTERFACE:${MYSQLPOOL_ROOT}/include
    $<BUILD_INTERFACE:${MYSQLPOOL_ROOT}/src/lib
    $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/generated-include>
    $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/external-projects/installed/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
    )

target_link_libraries(integration_tests
    ${GTEST_LIBRARIES}
    mysqlpool
    # ${Boost_LIBRARIES}
    # ${ZLIB_LIBRARIES}
    # ${OPENSSL_LIBRARIES}
    # stdc++fs
    # ${CMAKE_THREAD_LIBS_INIT}
)

add_test(NAME integration_tests COMMAND integration_tests)

endif()
