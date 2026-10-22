# SPDX-FileCopyrightText: Copyright (c) 2026 The WQS KVCache Mock Authors.
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.13)
project(wqs-mock
    VERSION 0.2.0
    DESCRIPTION "In-memory mocks of the WQS SharedClient SDK and legacy KVCache ABI"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# ----------------------------------------------------------------------------
# Library
# ----------------------------------------------------------------------------

find_package(Threads REQUIRED)
add_library(wqs_store SHARED src/shared_client_mock.cpp)
set(mock_targets wqs_store)

option(WQS_MOCK_BUILD_LEGACY "Build the legacy libwclient_kvcache mock" ON)
if(WQS_MOCK_BUILD_LEGACY)
    add_library(wclient_kvcache SHARED src/wclient_kvcache_mock.cpp)
    set_target_properties(wclient_kvcache PROPERTIES
        VERSION ${PROJECT_VERSION}
        SOVERSION 0
    )
    list(APPEND mock_targets wclient_kvcache)
endif()

foreach(target IN LISTS mock_targets)
    target_include_directories(${target} PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    )
    target_link_libraries(${target} PUBLIC Threads::Threads)
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
endforeach()

# ----------------------------------------------------------------------------
# Install
# ----------------------------------------------------------------------------

include(GNUInstallDirs)

install(TARGETS ${mock_targets}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(FILES include/shared_client.h include/pyclient.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
if(WQS_MOCK_BUILD_LEGACY)
    install(FILES include/kv_interface.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
endif()
install(FILES LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/wqs-mock)

# ----------------------------------------------------------------------------
# Tests
# ----------------------------------------------------------------------------

option(WQS_MOCK_BUILD_TESTS "Build the mock smoke test" ON)
if(WQS_MOCK_BUILD_TESTS)
    enable_testing()
    add_executable(wqs_shared_client_smoke tests/shared_client_smoke.cpp)
    target_link_libraries(wqs_shared_client_smoke PRIVATE wqs_store)
    add_test(NAME wqs_shared_client_smoke COMMAND wqs_shared_client_smoke)
    set_tests_properties(wqs_shared_client_smoke PROPERTIES TIMEOUT 30)
    if(WQS_MOCK_BUILD_LEGACY)
        add_executable(wqs_mock_smoke tests/smoke.cpp)
        target_link_libraries(wqs_mock_smoke PRIVATE wclient_kvcache)
        add_test(NAME wqs_mock_smoke COMMAND wqs_mock_smoke)
        set_tests_properties(wqs_mock_smoke PROPERTIES TIMEOUT 30)
    endif()
endif()
