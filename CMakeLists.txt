cmake_minimum_required(VERSION 3.24)

if (NOT DEFINED MYSQLPOOL_VERSION)
    set(MYSQLPOOL_VERSION 0.4.0)
endif()

project(mysqlpool-cpp
    DESCRIPTION "Connection pool for db connections to MySql/MariaDB"
    HOMEPAGE_URL https://github.com/jgaa/mysqlpool-cpp
    VERSION ${MYSQLPOOL_VERSION}
    LANGUAGES CXX)

include(GNUInstallDirs)

set(MYSQLPOOL_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH};${CMAKE_CURRENT_SOURCE_DIR}/cmake)

option(MYSQLPOOL_WITH_TESTS "Enable Tests" ON)
option(MYSQLPOOL_WITH_INTGRATION_TESTS "Enable Integretion Tests" OFF)
option(MYSQLPOOL_WITH_EXAMPLES "Compile examples" ON)
option(MYSQLPOOL_EMBEDDED "Do not try to install dependencies" OFF)

set(MYSQLPOOL_LOGGER "clog" CACHE STRING "Log system to use. One of 'clog', 'internal', 'logfault', 'boost' or 'none'")
set(MYSQLPOOL_LOG_LEVEL_STR "info" CACHE STRING "Minimum log level to enable. One of 'none', error', 'warn', 'info', 'debug', 'trace'")

set(MYSQLPOOL_DBUSER "MYSQLPOOL_DBUSER" CACHE STRING "Environment variable to get login user name name from")
set(MYSQLPOOL_DBPASSW "MYSQLPOOL_DBPASSW" CACHE STRING "Environment variable to get user login password from")
set(MYSQLPOOL_DATABASE "MYSQLPOOL_DATABASE" CACHE STRING "Environment variable to get database name from")
set(MYSQLPOOL_DBHOST "MYSQLPOOL_DBHOST" CACHE STRING "Environment variable to get the dbservers hostname or IP address from")
set(MYSQLPOOL_DBPORT "MYSQLPOOL_DBPORT" CACHE STRING "Environment variable to get the dbservers port number from")
set(MYSQLPOOL_DB_TLS_MODE "MYSQLPOOL_DB_TLS_MODE" CACHE STRING "Environment variable to get the TLS mode from . One of: 'disable', 'enable', 'require'")

set(DEFAULT_MYSQLPOOL_DBUSER "" CACHE STRING "Default db user")
set(DEFAULT_MYSQLPOOL_DBPASSW "" CACHE STRING "Default db password")
set(DEFAULT_MYSQLPOOL_DATABASE "" CACHE STRING "Default database name")
set(DEFAULT_MYSQLPOOL_HOST "localhost" CACHE STRING "Default host for the server")
set(DEFAULT_MYSQLPOOL_PORT 3306 CACHE STRING "Default port to the server")
set(DEFAULT_MYSQLPOOL_TLS_MODE "enable" CACHE STRING "TLS requirements. One of 'disable', 'enable', 'require'")

if (MYSQLPOOL_LOGGER STREQUAL "clog")
    set(MYSQLPOOL_LOG_WITH_CLOG ON)
elseif (MYSQLPOOL_LOGGER STREQUAL "internal")
    set(MYSQLPOOL_LOG_WITH_INTERNAL_LOG ON)
elseif (MYSQLPOOL_LOGGER STREQUAL "logfault")
    set(MYSQLPOOL_LOG_WITH_LOGFAULT ON)
elseif (MYSQLPOOL_LOGGER STREQUAL "boost")
    set(MYSQLPOOL_LOG_WITH_BOOST_LOG ON)
elseif (MYSQLPOOL_LOGGER STREQUAL "none")
    message("Logging is disabled")
else()
    message(FATAL_ERROR "Unsupported logger ${MYSQLPOOL_LOGGER}")
endif()

if (MYSQLPOOL_LOG_LEVEL_STR STREQUAL "none")
    set (MYSQLPOOL_LOG_LEVEL 0)
elseif(MYSQLPOOL_LOG_LEVEL_STR STREQUAL  "error")
    set (MYSQLPOOL_LOG_LEVEL 1)
elseif(MYSQLPOOL_LOG_LEVEL_STR STREQUAL  "warn")
    set (MYSQLPOOL_LOG_LEVEL 2)
elseif(MYSQLPOOL_LOG_LEVEL_STR STREQUAL  "info")
    set (MYSQLPOOL_LOG_LEVEL 3)
elseif(MYSQLPOOL_LOG_LEVEL_STR STREQUAL  "debug")
    set (MYSQLPOOL_LOG_LEVEL 4)
elseif(MYSQLPOOL_LOG_LEVEL_STR STREQUAL  "trace")
    set (MYSQLPOOL_LOG_LEVEL 5)
else()
    message(FATAL_ERROR "Unsupported log level ${MYSQLPOOL_LOG_LEVEL_STR}")
endif()

if (MYSQLPOOL_LOG_WITH_INTERNAL_LOG)
    message(STATUS "Using internal log handler to 'std::clog'")
    set(LOGGING_SRC logging.cpp)
endif()

if (MYSQLPOOL_LOG_WITH_BOOST_LOG)
    set(BOOST_LOG_DEP log_setup log)
    message(STATUS "Using Boost.Log for logging (brace for horrors!)")
endif()

if (MYSQLPOOL_LOG_WITH_CLOG)
    message(STATUS "Using std::clog for logging")
endif()


#add_definitions(-DMYSQLPOOL_VERSION=\"${CMAKE_PROJECT_VERSION}\")

set(CMAKE_CXX_STANDARD 20)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

set(MYSQLPOOL_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

if(WIN32)
    add_compile_options(-D_WIN32_WINNT=0x0601 -DWINVER=0x0601 -DWIN32_LEAN_AND_MEAN=1)
endif()

message(STATUS "Using ${CMAKE_CXX_COMPILER}")

if (MYSQLPOOL_WITH_CONAN)
    if (MYSQLPOOL_LOG_WITH_LOGFAULT)
        message("Conan is in use. Will use that to get logfault")
        #find_package (Logfault REQUIRED)
        message("CMAKE_INCLUDE_PATH: ${CMAKE_INCLUDE_PATH}")
    endif()
elseif (MYSQLPOOL_EMBEDDED)
    message("Using dependencies as they are installed on the system.")
else()
    include(cmake/3rdparty.cmake)
endif()


find_package(Threads REQUIRED)
find_package(ZLIB REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(Doxygen)

if(NOT DEFINED USE_BOOST_VERSION)
    set(USE_BOOST_VERSION 1.83)
endif()

find_package(Boost ${USE_BOOST_VERSION} REQUIRED COMPONENTS
    system
    context
    chrono
    json
    program_options
    ${BOOST_LOG_DEP}
    )

add_library(boost INTERFACE IMPORTED)
set_property(TARGET boost PROPERTY
    INTERFACE_INCLUDE_DIRECTORIES ${Boost_INCLUDE_DIR})

if (MYSQLPOOL_WITH_EXAMPLES)
    add_subdirectory(examples/simple)
endif()

if (MYSQLPOOL_WITH_TESTS)
    enable_testing()
    find_package(GTest REQUIRED)
    # Problem with Conan.
    # find_package(GTest REQUIRED) does not set GTEST_LIBRARIES.
    # Conan suggests to use 'gtest:gtest' which aslo don't work.
    if (NOT GTEST_LIBRARIES)
        message("GTest: GTEST_LIBRARIES unset. Setting it manually to gtest")
        set(GTEST_LIBRARIES GTest::gtest)
    endif()
    add_subdirectory(tests)
endif()

add_subdirectory(src)

# We create a configuration file so that other code that include our header files gets the correct configuration.
CONFIGURE_FILE(config.h.template ${CMAKE_BINARY_DIR}/generated-include/mysqlpool/config.h)

install(TARGETS mysqlpool
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib)
