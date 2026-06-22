# Copyright (c) 2026 jushar
# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.30)
project(coap-pp VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_library(coap-pp-warnings INTERFACE)
target_compile_options(coap-pp-warnings INTERFACE
    $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:
        -Wall -Wpedantic -Werror -Wno-array-bounds>
)

set(COAP_PP_LOG_LEVEL "0" CACHE STRING
    "Minimum log level compiled in (0=Debug, 1=Info, 2=Warning, 3=Error)")
set_property(CACHE COAP_PP_LOG_LEVEL PROPERTY STRINGS "0" "1" "2" "3")

add_library(coap-pp STATIC
    src/coap_pp/log.cpp
    src/coap_pp/pdu/deserialize.cpp
    src/coap_pp/pdu/serialize.cpp
    src/coap_pp/messaging/messenger.cpp
    src/coap_pp/server/coap_server.cpp
    src/coap_pp/server/resource.cpp
)

target_compile_features(coap-pp PUBLIC cxx_std_17)
target_link_libraries(coap-pp PRIVATE coap-pp-warnings)
target_compile_definitions(coap-pp PUBLIC COAP_PP_LOG_LEVEL=${COAP_PP_LOG_LEVEL})
if(DEFINED COAP_PP_NO_STD_SPAN)
    target_compile_definitions(coap-pp PUBLIC COAP_PP_NO_STD_SPAN=${COAP_PP_NO_STD_SPAN})
endif()

option(COAP_PP_USE_INPLACE_FUNCTION
    "Use a fixed-buffer inplace_function instead of std::function for RequestHandler (no heap allocation)"
    OFF)
if(COAP_PP_USE_INPLACE_FUNCTION)
    set(COAP_PP_INPLACE_FUNCTION_CAPACITY "32" CACHE STRING
        "Buffer capacity in bytes for the inplace_function RequestHandler storage")
    target_compile_definitions(coap-pp PUBLIC
        COAP_PP_USE_INPLACE_FUNCTION
        COAP_PP_INPLACE_FUNCTION_CAPACITY=${COAP_PP_INPLACE_FUNCTION_CAPACITY})
endif()

target_include_directories(coap-pp
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

# UDP-over-IP-over-SLIP serial transport (platform-agnostic, requires std::thread)
find_package(Threads REQUIRED)

add_library(coap-pp-transport-udp-ip-slip STATIC
    src/coap_pp_transport_udp_ip_slip/udp_ip_slip_transport.cpp
)
target_link_libraries(coap-pp-transport-udp-ip-slip PUBLIC coap-pp Threads::Threads PRIVATE coap-pp-warnings)
target_compile_features(coap-pp-transport-udp-ip-slip PUBLIC cxx_std_17)

# nanopb serde
option(COAP_PP_BUILD_SERDE_NANOPB "Build nanopb-based serde support" ON)
if(COAP_PP_BUILD_SERDE_NANOPB)
    include(FetchContent)
    FetchContent_Declare(
        nanopb
        GIT_REPOSITORY https://github.com/nanopb/nanopb.git
        GIT_TAG        0.4.9.1
        FIND_PACKAGE_ARGS
    )
    FetchContent_MakeAvailable(nanopb)

    if(DEFINED nanopb_SOURCE_DIR AND NOT COMMAND NANOPB_GENERATE_CPP)
        set(NANOPB_GENERATOR_SOURCE_DIR ${nanopb_SOURCE_DIR}/generator)
        include(${nanopb_SOURCE_DIR}/extra/FindNanopb.cmake)
    endif()

    include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/coap_pp_serde_nanopb/NanopbGenerate.cmake)

    add_library(coap-pp-serde-nanopb INTERFACE)
    target_include_directories(coap-pp-serde-nanopb
        INTERFACE
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
    )
    target_link_libraries(coap-pp-serde-nanopb INTERFACE coap-pp protobuf-nanopb-static)
endif()

# POSIX transport + examples (Linux / macOS only)
if(UNIX)
    option(COAP_PP_BUILD_POSIX_TRANSPORT "Build the POSIX UDP transport" ON)

    if(COAP_PP_BUILD_POSIX_TRANSPORT)
        add_library(coap-pp-transport-posix STATIC
            src/coap_pp_transport_posix/udp_transport.cpp
        )
        target_link_libraries(coap-pp-transport-posix PUBLIC coap-pp Threads::Threads PRIVATE coap-pp-warnings coap-pp-serde-nanopb)
        target_compile_features(coap-pp-transport-posix PUBLIC cxx_std_17)

        option(COAP_PP_BUILD_EXAMPLES "Build example programs" ON)
        if(COAP_PP_BUILD_EXAMPLES)
            add_subdirectory(examples)
        endif()
    endif()
endif()

# Tests
option(COAP_PP_BUILD_TESTS "Build coap-pp tests" ON)
if(COAP_PP_BUILD_TESTS)
    include(CTest)
    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.16.0
    )
    set(INSTALL_GTEST OFF)
    FetchContent_MakeAvailable(googletest)

    add_subdirectory(tests)
endif()
