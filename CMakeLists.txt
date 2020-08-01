# Copyright (c) 2019, QuantStack and Mamba Contributors
#
# Distributed under the terms of the BSD 3-Clause License.
#
# The full license is in the file LICENSE, distributed with this software.

cmake_minimum_required (VERSION 2.8.11)
if(POLICY CMP0025)
  cmake_policy(SET CMP0025 NEW)
endif()
project (mamba)

if (MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /DNOMINMAX")
    set(CMAKE_BUILD_TYPE Release)
    # add_definitions("-DUNICODE -D_UNICODE")
else()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall")
endif()

set(CMAKE_CXX_STANDARD 17)

option(BUILD_EXE OFF)
option(BUILD_BINDINGS ON)
option(LINK_STATIC OFF)
option(USE_CONDA_PYTHON ON)

if (USE_CONDA_PYTHON)
    message("Using conda prefix: $ENV{CONDA_PREFIX}")
    set(PYTHON_EXECUTABLE "$ENV{CONDA_PREFIX}/bin/python")
    set(PYTHON_LIBRARIES "$ENV{CONDA_PREFIX}/lib/")
endif ()

find_package(Threads REQUIRED)
find_library(LIBSOLV_LIBRARIES NAMES solv)
find_library(LIBSOLVEXT_LIBRARIES NAMES solvext)
find_package(CURL REQUIRED)
find_package(LibArchive REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(nlohmann_json REQUIRED)

set(MAMBA_REQUIRED_LIBS
    ${LIBSOLV_LIBRARIES}
    ${LIBSOLVEXT_LIBRARIES}
    ${LibArchive_LIBRARIES}
    ${CURL_LIBRARIES}
    ${OPENSSL_LIBRARIES}
    nlohmann_json::nlohmann_json
)

# Getting the mamba version from python
file (READ mamba/_version.py VERSION_FILE)

STRING(REGEX REPLACE "\r?\n" ";" TMP_VERSION_FILE_LIST "${VERSION_FILE}")
LIST(GET TMP_VERSION_FILE_LIST 1 VERSION_LINE)
string(REGEX MATCHALL "version_info = \\(([0-9]*), ([0-9]*), ([0-9]*)\\)" MATCHES ${VERSION_LINE})

set(MAMBA_VERSION_MAJOR "${CMAKE_MATCH_1}")
set(MAMBA_VERSION_MINOR "${CMAKE_MATCH_2}")
set(MAMBA_VERSION_PATCH "${CMAKE_MATCH_3}")

message("--> mamba version: ${MAMBA_VERSION_MAJOR}.${MAMBA_VERSION_MINOR}.${MAMBA_VERSION_PATCH}")

configure_file(include/version.hpp.in ${CMAKE_SOURCE_DIR}/include/version.hpp @ONLY)

if (LINK_STATIC)

    set(MAMBA_STATIC_LIBS
        libcurl.a
        libssh2.a

        libgssapi_krb5.a
        libkrb5.a
        libk5crypto.a
        libkrb5support.a
        libcom_err.a

        libssl.a
        libcrypto.a

        libarchive.a

        libiconv.a
        libbz2.a
        liblz4.a
        libzstd.a
        libz.a
        liblzma.a
        liblzo2.a

        libsolv.a
        libsolvext.a
    )

    if (APPLE)
        set (MAMBA_FORCE_DYNAMIC_LIBS resolv)
        set(FINAL_LIBS "")
        foreach(LIB ${MAMBA_STATIC_LIBS})
            set(TMP_LIB "TMP_LIB-NOTFOUND")
            find_library(TMP_LIB "${LIB}")
            list(APPEND FINAL_LIBS "${TMP_LIB}")
        endforeach(LIB)
        set (MAMBA_STATIC_LIBS "${FINAL_LIBS}")
    elseif (UNIX)
        set(MAMBA_FORCE_DYNAMIC_LIBS rt dl resolv)
    endif()

    if (APPLE)
        add_link_options(-static-libstdc++)
    elseif (UNIX)
        add_link_options(-static-libstdc++ -static-libgcc)
    endif ()
endif()

if (LINK_STATIC)
    message("Found libraries: ${MAMBA_STATIC_LIBS}")
else ()
    message("Found libraries: ${MAMBA_REQUIRED_LIBS}")
endif ()

option(ENABLE_TESTS "Enable C++ tests for mamba" OFF)

include_directories(
    include
    include/thirdparty
)

set(MAMBA_SOURCES
    ${CMAKE_SOURCE_DIR}/src/activation.cpp
    ${CMAKE_SOURCE_DIR}/src/channel.cpp
    ${CMAKE_SOURCE_DIR}/src/context.cpp
    ${CMAKE_SOURCE_DIR}/src/environments_manager.cpp
    ${CMAKE_SOURCE_DIR}/src/fetch.cpp
    ${CMAKE_SOURCE_DIR}/src/history.cpp
    ${CMAKE_SOURCE_DIR}/src/index.cpp
    ${CMAKE_SOURCE_DIR}/src/link.cpp
    ${CMAKE_SOURCE_DIR}/src/match_spec.cpp
    ${CMAKE_SOURCE_DIR}/src/output.cpp
    ${CMAKE_SOURCE_DIR}/src/package_handling.cpp
    ${CMAKE_SOURCE_DIR}/src/package_cache.cpp
    ${CMAKE_SOURCE_DIR}/src/pool.cpp
    ${CMAKE_SOURCE_DIR}/src/prefix_data.cpp
    ${CMAKE_SOURCE_DIR}/src/package_info.cpp
    ${CMAKE_SOURCE_DIR}/src/package_paths.cpp
    ${CMAKE_SOURCE_DIR}/src/query.cpp
    ${CMAKE_SOURCE_DIR}/src/repo.cpp
    ${CMAKE_SOURCE_DIR}/src/solver.cpp
    ${CMAKE_SOURCE_DIR}/src/subdirdata.cpp
    ${CMAKE_SOURCE_DIR}/src/thread_utils.cpp
    ${CMAKE_SOURCE_DIR}/src/transaction.cpp
    ${CMAKE_SOURCE_DIR}/src/transaction_context.cpp
    ${CMAKE_SOURCE_DIR}/src/url.cpp
    ${CMAKE_SOURCE_DIR}/src/util.cpp
    ${CMAKE_SOURCE_DIR}/src/validate.cpp
    ${CMAKE_SOURCE_DIR}/src/version.cpp
)

add_library(mamba_lib STATIC ${MAMBA_SOURCES})
if (LINK_STATIC)
    target_link_libraries(mamba_lib PUBLIC "${MAMBA_STATIC_LIBS}" ${MAMBA_FORCE_DYNAMIC_LIBS} nlohmann_json::nlohmann_json Threads::Threads)
else()
    target_link_libraries(mamba_lib PUBLIC ${MAMBA_REQUIRED_LIBS} Threads::Threads)
endif()

if (BUILD_BINDINGS)
    find_package(pybind11 REQUIRED)

    pybind11_add_module(mamba_api
        src/py_interface.cpp
    )
    target_link_libraries(mamba_api PUBLIC pybind11::pybind11 mamba_lib)
    set_property(TARGET mamba_api PROPERTY CXX_STANDARD 17)

    install(TARGETS mamba_api
            LIBRARY DESTINATION ${CMAKE_SOURCE_DIR}/mamba/)
endif()

if (BUILD_EXE)
    set(MAMBA_EXE
        ${CMAKE_SOURCE_DIR}/src/main.cpp
    )

    add_executable(micromamba ${MAMBA_EXE})
    target_link_libraries(micromamba PUBLIC mamba_lib)

    install(TARGETS micromamba)
endif()


if (ENABLE_TESTS)
    add_subdirectory(test)
endif()
