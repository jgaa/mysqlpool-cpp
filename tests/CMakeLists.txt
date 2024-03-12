
message("tests: LOGFAULT_INCLUDEDIR=${LOGFAULT_INCLUDEDIR}")

message ("GTEST_LIBRARIES: ${GTEST_LIBRARIES}")
message ("GTest_LIBRARIES: ${GTest_LIBRARIES}")

message ("GTEST_INCLUDE_DIRS: ${GTEST_INCLUDE_DIRS}")
message ("GTest_INCLUDE_DIRS: ${GTest_INCLUDE_DIRS}")

if (NOT GTEST_LIBRARIES)
    set (GTEST_LIBRARIES ${GTest_LIBRARIES})
endif()

if (NOT GTEST_INCLUDE_DIRS)
    set (GTEST_INCLUDE_DIRS ${GTest_INCLUDE_DIRS})
endif()


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
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${MYSQLPOOL_ROOT}/include
    ${MYSQLPOOL_ROOT}/src/lib
    ${CMAKE_BINARY_DIR}/generated-include>
    ${CMAKE_BINARY_DIR}/external-projects/installed/include>
    ${CMAKE_CURRENT_BINARY_DIR}
    ${GTEST_INCLUDE_DIRS}
    ${LOGFAULT_INCLUDEDIR}
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
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${MYSQLPOOL_ROOT}/include
    ${MYSQLPOOL_ROOT}/src/lib
    ${CMAKE_BINARY_DIR}/generated-include>
    ${CMAKE_BINARY_DIR}/external-projects/installed/include>
    ${CMAKE_CURRENT_BINARY_DIR}
    ${GTEST_INCLUDE_DIRS}
    ${LOGFAULT_INCLUDEDIR}
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
