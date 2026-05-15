# SPDX-FileCopyrightText: Copyright (c) 2026 The WQS KVCache Mock Authors.
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.13)
project(wqs-mock
    VERSION 0.1.0
    DESCRIPTION "In-memory Apache-2.0 mock of the WQS KVCache client ABI"
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

add_library(wclient_kvcache SHARED
    src/wclient_kvcache_mock.cpp
)

target_include_directories(wclient_kvcache
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

find_package(Threads REQUIRED)
target_link_libraries(wclient_kvcache PUBLIC Threads::Threads)

set_target_properties(wclient_kvcache PROPERTIES
    VERSION   ${PROJECT_VERSION}
    SOVERSION 0
    OUTPUT_NAME wclient_kvcache
)

target_compile_options(wclient_kvcache PRIVATE
    -Wall -Wextra -Wpedantic
)

# ----------------------------------------------------------------------------
# Install
# ----------------------------------------------------------------------------

include(GNUInstallDirs)

install(TARGETS wclient_kvcache
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(FILES include/kv_interface.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

# ----------------------------------------------------------------------------
# Tests
# ----------------------------------------------------------------------------

option(WQS_MOCK_BUILD_TESTS "Build the mock smoke test" ON)
if(WQS_MOCK_BUILD_TESTS)
    enable_testing()
    add_executable(wqs_mock_smoke tests/smoke.cpp)
    target_link_libraries(wqs_mock_smoke PRIVATE wclient_kvcache)
    add_test(NAME wqs_mock_smoke COMMAND wqs_mock_smoke)
endif()
