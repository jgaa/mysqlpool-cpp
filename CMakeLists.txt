cmake_minimum_required(VERSION 3.24)

if (NOT DEFINED MYSQLPOOL_VERSION)
    set(MYSQLPOOL_VERSION 0.0.1)
endif()

project(mysqlpool-cpp
    DESCRIPTION "Connection pool for db connections to MySql/MariaDB"
    HOMEPAGE_URL https://github.com/jgaa/mysqlpool-cpp
    VERSION ${MYSQLPOOL_VERSION}
    LANGUAGES CXX)

set(MYSQLPOOL_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH};${CMAKE_CURRENT_SOURCE_DIR}/cmake)

option(MYSQLPOOL_WITH_TESTS "Enable Tests" ON)

option(MYSQLPOOL_LOG_WITH_BOOST_LOG "Use boost::log for logging" OFF)

option(MYSQLPOOL_LOG_WITH_LOGFAULT "Use logfault for logging" OFF)

option(MYSQLPOOL_LOG_WITH_CLOG "Use std::clog for logging" OFF)

set(MYSQLPOOL_LOGGER "clog" CACHE STRING "Log system to use. One of 'clog', 'logfault', 'boost' or 'none'")
set(MYSQLPOOL_LOG_LEVEL_STR "info" CACHE STRING "Minimum log level to enable")

if (MYSQLPOOL_LOGGER STREQUAL "clog")
    set(MYSQLPOOL_LOG_WITH_CLOG ON)
elseif (MYSQLPOOL_LOGGER STREQUAL "logfault")
    set(MYSQLPOOL_LOG_WITH_LOGFAULT ON)
elseif (MYSQLPOOL_LOGGER STREQUAL "boost")
    set(MYSQLPOOL_LOG_WITH_BOOST_LOG ON)
elseif (MYSQLPOOL_LOGGER STREQUAL "node")
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
    if (MYSQLPOOL_LOG_WITH_LOGFAULT OR MYSQLPOOL_LOG_WITH_CLOG OR MYSQLPOOL_LOG_WITH_INTERNAL_LOG)
        message( FATAL_ERROR "You must use MYSQLPOOL_LOG_WITH_BOOST_LOG or MYSQLPOOL_LOG_WITH_LOGFAULT or MYSQLPOOL_LOG_WITH_INTERNAL_LOG or neither")
    endif()
    set(BOOST_LOG_DEP log)
    message(STATUS "Using Boost.Log for logging (brace for horrors!)")
endif()

if (MYSQLPOOL_LOG_WITH_LOGFAULT)
    if (MYSQLPOOL_LOG_WITH_BOOST_LOG OR MYSQLPOOL_LOG_WITH_CLOG OR MYSQLPOOL_LOG_WITH_INTERNAL_LOG)
        message( FATAL_ERROR "You must use MYSQLPOOL_LOG_WITH_BOOST_LOG or MYSQLPOOL_LOG_WITH_LOGFAULT or MYSQLPOOL_LOG_WITH_INTERNAL_LOG or neither")
    endif()
    message(STATUS "Using restc for logging")
endif()

if (MYSQLPOOL_LOG_WITH_CLOG)
    if (MYSQLPOOL_LOG_WITH_BOOST_LOG OR MYSQLPOOL_LOG_WITH_LOGFAULT OR MYSQLPOOL_LOG_WITH_INTERNAL_LOG)
        message( FATAL_ERROR "You must use MYSQLPOOL_LOG_WITH_BOOST_LOG or MYSQLPOOL_LOG_WITH_LOGFAULT or MYSQLPOOL_LOG_WITH_INTERNAL_LOG or neither")
    endif()
    message(STATUS "Using std::clog for logging")
endif()


add_definitions(-DMYSQLPOOL_VERSION=\"${CMAKE_PROJECT_VERSION}\")

set(CMAKE_CXX_STANDARD 20)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

set(MYSQLPOOL_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

message(STATUS "Using ${CMAKE_CXX_COMPILER}")

find_package(Threads REQUIRED)
find_package(ZLIB REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(Doxygen)

if(NOT DEFINED USE_BOOST_VERSION)
    set(USE_BOOST_VERSION 1.83)
endif()

option(Boost_USE_STATIC_LIBS "Link statically with boost libraries" OFF)
find_package(Boost ${USE_BOOST_VERSION} REQUIRED COMPONENTS
    system
#    date_time
#    coroutine
    context
    chrono
    json
    ${BOOST_LOG_DEP}
    )

#set(MYSQLPOOL_LIBS = ${Boost_LIBRARIES} stdc++fs ZLIB Threads::Threads)

add_library(boost INTERFACE IMPORTED)
set_property(TARGET boost PROPERTY
    INTERFACE_INCLUDE_DIRECTORIES ${Boost_INCLUDE_DIR})

if (MYSQLPOOL_WITH_TESTS)
    find_package(GTest REQUIRED)
    add_subdirectory(tests)
endif()

include(cmake/3rdparty.cmake)

add_subdirectory(src)

# We create a configuration file so that other code that include our header files gets the correct configuration.
CONFIGURE_FILE(config.h.template ${CMAKE_BINARY_DIR}/generated-include/mysqlpool/config.h)
