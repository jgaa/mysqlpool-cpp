set(EXTERNAL_PROJECTS_PREFIX ${CMAKE_BINARY_DIR}/external-projects)
set(EXTERNAL_PROJECTS_INSTALL_PREFIX ${EXTERNAL_PROJECTS_PREFIX}/installed)

include(GNUInstallDirs)
include(ExternalProject)

# MUST be called before any add_executable() # https://stackoverflow.com/a/40554704/8766845
link_directories(${EXTERNAL_PROJECTS_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR})
include_directories($<BUILD_INTERFACE:${EXTERNAL_PROJECTS_INSTALL_PREFIX}/${CMAKE_INSTALL_INCLUDEDIR}>)

if (MYSQLPOOL_LOG_WITH_LOGFAULT)
    if (LOGFAULT_ROOT)
        # Assume that we are using a library that might not be available yet
        message("LOGFAULT_ROOT: ${LOGFAULT_ROOT}")
        set(LOGFAULT_DIR ${LOGFAULT_ROOT})
        #include_directories(${LOGFAULT_DIR})
    else()
        find_path(LOGFAULT_DIR NAMES logfault.h PATH_SUFFIXES logfault)
    endif()
    message("LOGFAULT_DIR: " ${LOGFAULT_DIR})

    if (NOT LOGFAULT_DIR STREQUAL "LOGFAULT_DIR-NOTFOUND" )
        message ("Using existing logfault at: ${LOGFAULT_DIR}")
        add_library(logfault INTERFACE IMPORTED)
        set(LOGFAULT_INCLUDEDIR ${LOGFAULT_DIR})
    else()
        message ("Embedding logfault header only library")
        set(MYSQLPOOL_LOGDEP ${RESTC_CPP_EXTERNAL_DEPS} embeddedLogfault)
        ExternalProject_Add(embeddedLogfault
            PREFIX "${EXTERNAL_PROJECTS_PREFIX}"
            GIT_REPOSITORY "https://github.com/jgaa/logfault.git"
            GIT_TAG "master"
            CMAKE_ARGS
                -DCMAKE_INSTALL_PREFIX=${EXTERNAL_PROJECTS_INSTALL_PREFIX}
                -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
                -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
                -DCMAKE_GENERATOR='${CMAKE_GENERATOR}'
                -DCMAKE_BUILD_TYPE='${CMAKE_BUILD_TYPE}'
                -DLOGFAULT_BUILD_TESTS=OFF
        )

    endif()
endif()
