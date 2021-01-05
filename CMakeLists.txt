# Copyright (c) 2019, QuantStack and Mamba Contributors
#
# Distributed under the terms of the BSD 3-Clause License.
#
# The full license is in the file LICENSE, distributed with this software.

cmake_minimum_required (VERSION 2.8.11)
if(POLICY CMP0025)
  cmake_policy(SET CMP0025 NEW)
endif()
#if(POLICY CMP0028)
#  cmake_policy(SET CMP0028 NEW)
#endif()
project(mamba)

set(MAMBA_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include)
set(MAMBA_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)

# Versionning
# ===========

# Getting the mamba version from python
file (READ mamba/_version.py VERSION_FILE)

STRING(REGEX REPLACE "\r?\n" ";" TMP_VERSION_FILE_LIST "${VERSION_FILE}")
LIST(GET TMP_VERSION_FILE_LIST 0 VERSION_LINE)
string(REGEX MATCHALL "version_info = \\(([0-9]*), ([0-9]*), ([0-9]*)\\)" MATCHES ${VERSION_LINE})

set(MAMBA_VERSION_MAJOR "${CMAKE_MATCH_1}")
set(MAMBA_VERSION_MINOR "${CMAKE_MATCH_2}")
set(MAMBA_VERSION_PATCH "${CMAKE_MATCH_3}")

message("--> mamba version: ${MAMBA_VERSION_MAJOR}.${MAMBA_VERSION_MINOR}.${MAMBA_VERSION_PATCH}")

# Binary version
# See the following URL for explanations about the binary versionning
# https://www.gnu.org/software/libtool/manual/html_node/Updating-version-info.html#Updating-version-info
file(STRINGS "${MAMBA_INCLUDE_DIR}/mamba/version.hpp.in" mamba_version_defines
    REGEX "#define MAMBA_BINARY_(CURRENT|REVISION|AGE)")
foreach(ver ${mamba_version_defines})
    if(ver MATCHES "#define MAMBA_BINARY_(CURRENT|REVISION|AGE) +([^ ]+)$")
        set(MAMBA_BINARY_${CMAKE_MATCH_1} "${CMAKE_MATCH_2}" CACHE INTERNAL "")
    endif()
endforeach()
set(MAMBA_BINARY_VERSION
    ${MAMBA_BINARY_CURRENT}.${MAMBA_BINARY_REVISION}.${MAMBA_BINARY_AGE})
message(STATUS "mamba binary version: v${MAMBA_BINARY_VERSION}")
configure_file(${MAMBA_INCLUDE_DIR}/mamba/version.hpp.in
    ${CMAKE_SOURCE_DIR}/include/mamba/version.hpp @ONLY)

# Build options
# =============

option(BUILD_EXE "" OFF)
option(BUILD_SHARED "" OFF)
option(BUILD_STATIC "" ON)
option(BUILD_BINDINGS "" ON)
option(STATIC_DEPENDENCIES "" OFF)
option(USE_VENDORED_CLI11 "" OFF)
option(ENABLE_TESTS "Enable C++ tests for mamba" OFF)

if (USE_VENDORED_CLI11)
    add_definitions(-DVENDORED_CLI11=1)
endif()

# Dependencies
# ============

find_package(Threads REQUIRED)
find_package(nlohmann_json REQUIRED)

message("Found libraries: ${MAMBA_DEPENDENCIES_LIBS}")

# Source files
# ============

set(MAMBA_SOURCES
    ${MAMBA_SOURCE_DIR}/activation.cpp
    ${MAMBA_SOURCE_DIR}/channel.cpp
    ${MAMBA_SOURCE_DIR}/context.cpp
    ${MAMBA_SOURCE_DIR}/environments_manager.cpp
    ${MAMBA_SOURCE_DIR}/fetch.cpp
    ${MAMBA_SOURCE_DIR}/transaction_context.cpp
    ${MAMBA_SOURCE_DIR}/link.cpp
    ${MAMBA_SOURCE_DIR}/history.cpp
    ${MAMBA_SOURCE_DIR}/match_spec.cpp
    ${MAMBA_SOURCE_DIR}/url.cpp
    ${MAMBA_SOURCE_DIR}/output.cpp
    ${MAMBA_SOURCE_DIR}/package_handling.cpp
    ${MAMBA_SOURCE_DIR}/package_cache.cpp
    ${MAMBA_SOURCE_DIR}/pool.cpp
    ${MAMBA_SOURCE_DIR}/prefix_data.cpp
    ${MAMBA_SOURCE_DIR}/package_info.cpp
    ${MAMBA_SOURCE_DIR}/package_paths.cpp
    ${MAMBA_SOURCE_DIR}/query.cpp
    ${MAMBA_SOURCE_DIR}/repo.cpp
    ${MAMBA_SOURCE_DIR}/shell_init.cpp
    ${MAMBA_SOURCE_DIR}/solver.cpp
    ${MAMBA_SOURCE_DIR}/subdirdata.cpp
    ${MAMBA_SOURCE_DIR}/thread_utils.cpp
    ${MAMBA_SOURCE_DIR}/transaction.cpp
    ${MAMBA_SOURCE_DIR}/util.cpp
    ${MAMBA_SOURCE_DIR}/validate.cpp
    ${MAMBA_SOURCE_DIR}/version.cpp
    ${MAMBA_SOURCE_DIR}/thirdparty/WinReg.hpp
    ${MAMBA_SOURCE_DIR}/thirdparty/WinReg.hpp
    ${MAMBA_SOURCE_DIR}/thirdparty/WinReg.hpp
)

set(MAMBA_HEADERS
    ${MAMBA_INCLUDE_DIR}/mamba/activation.hpp
    ${MAMBA_INCLUDE_DIR}/mamba/channel.hpp
    ${MAMBA_INCLUDE_DIR}/mamba/context.hpp
    ${MAMBA_INCLUDE_DIR}/mamba/environment.hpp
    ${MAMBA_INCLUDE_DIR}/mamba/environments_manager.hpp
    ${MAMBA_INCLUDE_DIR}/mamba/fetch.hpp
    ${MAMBA_INCLUDE_DIR}/mamba/fsutil.hpp
    ${MAMBA_INCLUDE_DIR}/mamba/graph_util.hpp
    ${MAMBA_INCLUDE_DIR}/mamba/history.hpp
    ${MAMBA_INCLUDE_DIR}/mamba/link.hpp
    ${MAMBA_INCLUDE_DIR}/mamba/mamba_fs.hpp
    ${MAMBA_INCLUDE_DIR}/mamba/match_spec.hpp
    ${MAMBA_INCLUDE_DIR}/mamba/output.hpp
    ${MAMBA_INCLUDE_DIR}/mamba/package_cache.hpp
    ${MAMBA_INCLUDE_DIR}/mamba/package_handling.hpp
    ${MAMBA_INCLUDE_DIR}/mamba/package_info.hpp
    ${MAMBA_INCLUDE_DIR}/mamba/package_paths.hpp
    ${MAMBA_INCLUDE_DIR}/mamba/pool.hpp
    ${MAMBA_INCLUDE_DIR}/mamba/prefix_data.hpp
    ${MAMBA_INCLUDE_DIR}/mamba/query.hpp
    ${MAMBA_INCLUDE_DIR}/mamba/repo.hpp
    ${MAMBA_INCLUDE_DIR}/mamba/shell_init.hpp
    ${MAMBA_INCLUDE_DIR}/mamba/solver.hpp
    ${MAMBA_INCLUDE_DIR}/mamba/subdirdata.hpp
    ${MAMBA_INCLUDE_DIR}/mamba/thread_utils.hpp
    ${MAMBA_INCLUDE_DIR}/mamba/transaction.hpp
    ${MAMBA_INCLUDE_DIR}/mamba/transaction_context.hpp
    ${MAMBA_INCLUDE_DIR}/mamba/url.hpp
    ${MAMBA_INCLUDE_DIR}/mamba/util.hpp
    ${MAMBA_INCLUDE_DIR}/mamba/validate.hpp
    ${MAMBA_INCLUDE_DIR}/mamba/version.hpp
)

# Targets and link
# ================

if (MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /DNOMINMAX")
    set(CMAKE_BUILD_TYPE Release)
    # add_definitions("-DUNICODE -D_UNICODE")
else()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall")
endif()

macro(mamba_create_target target_name linkage output_name)
    string(TOUPPER "${linkage}" linkage_upper)

    if (NOT ${linkage_upper} MATCHES "^(SHARED|STATIC)$")
        message(FATAL_ERROR "Invalid library linkage: ${linkage}")
    endif ()

    # Output
    # ======

    add_library(${target_name} ${linkage_upper} ${MAMBA_SOURCES} ${MAMBA_HEADERS} ${MAMBA_THIRD_PARTIES})
    if (NOT STATIC_DEPENDENCIES)
        find_library(LIBSOLV_LIBRARIES NAMES solv)
        find_library(LIBSOLVEXT_LIBRARIES NAMES solvext)
        find_library(LIBZCK_LIBRARIES REQUIRED NAMES zck)
        find_package(CURL REQUIRED)
        find_package(LibArchive REQUIRED)
        find_package(OpenSSL REQUIRED)
        find_package(yaml-cpp CONFIG REQUIRED)
        find_package(reproc++ CONFIG REQUIRED)

        set(MAMBA_DEPENDENCIES_LIBS
            ${LIBSOLV_LIBRARIES}
            ${LIBSOLVEXT_LIBRARIES}
            ${LIBZCK_LIBRARIES}
            ${LibArchive_LIBRARIES}
            ${CURL_LIBRARIES}
            ${OPENSSL_LIBRARIES}
            ${YAML_CPP_LIBRARIES}
            reproc++
        )

        target_link_libraries(${target_name} PUBLIC
                              ${MAMBA_DEPENDENCIES_LIBS}
                              ${MAMBA_FORCE_DYNAMIC_LIBS}
                              nlohmann_json::nlohmann_json
                              Threads::Threads)
    endif()
    set_property(TARGET ${target_name} PROPERTY CXX_STANDARD 17)

    target_include_directories(
        ${target_name}
        PUBLIC
            $<BUILD_INTERFACE:${MAMBA_INCLUDE_DIR}>
            $<INSTALL_INTERFACE:include>
    )

    set_target_properties(
        ${target_name}
        PROPERTIES
        PUBLIC_HEADER "${MAMBA_HEADERS}"
        COMPILE_DEFINITIONS "MAMBA_EXPORTS"
        PREFIX ""
        VERSION ${MAMBA_BINARY_VERSION}
        SOVERSION ${MAMBA_BINARY_CURRENT}
        OUTPUT_NAME "lib${output_name}"
    )
endmacro()


set(mamba_targets "")

if (BUILD_SHARED)
    message(STATUS "BUILDING SHARED")
    mamba_create_target(mamba SHARED mamba)
    list(APPEND mamba_targets mamba)
endif ()

if (BUILD_STATIC)
    message(STATUS "BUILDING STATIC")
    # On Windows, a static library should use a different output name
    # to avoid the conflict with the import library of a shared one.
    if (CMAKE_HOST_WIN32)
        mamba_create_target(mamba-static STATIC mamba_static)
    else ()
        mamba_create_target(mamba-static STATIC mamba)
    endif ()

    find_package(nlohmann_json CONFIG REQUIRED)

    target_link_libraries(mamba-static PUBLIC
                          nlohmann_json::nlohmann_json
                          Threads::Threads)

    list(APPEND mamba_targets mamba-static)
endif ()

if (BUILD_BINDINGS)
    find_package(pybind11 REQUIRED)

    pybind11_add_module(mamba_api
        src/py_interface.cpp
    )
    target_link_libraries(mamba_api PUBLIC pybind11::pybind11 mamba-static)
    set_property(TARGET mamba_api PROPERTY CXX_STANDARD 17)

    install(TARGETS mamba_api
            LIBRARY DESTINATION ${CMAKE_SOURCE_DIR}/mamba/)
endif()

if (BUILD_EXE)
    include(${CMAKE_CURRENT_SOURCE_DIR}/src/micromamba/CMakeLists.txt)
endif()

# Tests
# =====
if (ENABLE_TESTS)
    add_subdirectory(test)
endif()

# Installation
# ============

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(MAMBA_CMAKECONFIG_INSTALL_DIR "${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}" CACHE STRING "install path for mambaConfig.cmake")

if (NOT (BUILD_EXE AND STATIC_DEPENDENCIES))

    install(TARGETS ${mamba_targets}
            EXPORT ${PROJECT_NAME}-targets
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
            LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
            PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/mamba)

    # Makes the project importable from the build directory
    export(EXPORT ${PROJECT_NAME}-targets
           FILE "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}Targets.cmake")

    # Configure 'mambaConfig.cmake' for a build tree
    set(MAMBA_CONFIG_CODE "####### Expanded from \@MAMBA_CONFIG_CODE\@ #######\n")
    set(MAMBA_CONFIG_CODE "${MAMBA_CONFIG_CODE}set(CMAKE_MODULE_PATH \"${CMAKE_CURRENT_SOURCE_DIR}/cmake;\${CMAKE_MODULE_PATH}\")\n")
    set(MAMBA_CONFIG_CODE "${MAMBA_CONFIG_CODE}##################################################")
    configure_package_config_file(${PROJECT_NAME}Config.cmake.in
                                  "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}Config.cmake"
                                  INSTALL_DESTINATION ${PROJECT_BINARY_DIR})

    # Configure 'mambaConfig.cmake' for an install tree
    set(MAMBA_CONFIG_CODE "")
    configure_package_config_file(${PROJECT_NAME}Config.cmake.in
                                  "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/${PROJECT_NAME}Config.cmake"
                                  INSTALL_DESTINATION ${MAMBA_CMAKECONFIG_INSTALL_DIR})


    write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}ConfigVersion.cmake
                                     VERSION ${MAMBA_VERSION_MAJOR}.${MAMBA_VERSION_MINOR}.${MAMBA_VERSION_PATCH}
                                     COMPATIBILITY AnyNewerVersion)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/${PROJECT_NAME}Config.cmake
                  ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}ConfigVersion.cmake
                  DESTINATION ${MAMBA_CMAKECONFIG_INSTALL_DIR})
    install(EXPORT ${PROJECT_NAME}-targets
            FILE ${PROJECT_NAME}Targets.cmake
            DESTINATION ${MAMBA_CMAKECONFIG_INSTALL_DIR})
endif ()
