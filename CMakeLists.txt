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

set(COAP_PP_MAX_RESPONSE_OPTIONS "4" CACHE STRING
    "Maximum number of additional options a response can carry (besides Content-Format)")

add_library(coap-pp STATIC
    src/coap_pp/log.cpp
    src/coap_pp/panic.cpp
    src/coap_pp/pdu/deserialize.cpp
    src/coap_pp/pdu/serialize.cpp
    src/coap_pp/messaging/messenger.cpp
    src/coap_pp/server/coap_server.cpp
    src/coap_pp/server/resource.cpp
)

target_compile_features(coap-pp PUBLIC cxx_std_17)
target_link_libraries(coap-pp PRIVATE coap-pp-warnings)
target_compile_definitions(coap-pp PUBLIC
    COAP_PP_LOG_LEVEL=${COAP_PP_LOG_LEVEL}
    COAP_PP_MAX_RESPONSE_OPTIONS=${COAP_PP_MAX_RESPONSE_OPTIONS})
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

# nanopb serde
option(COAP_PP_BUILD_SERDE_NANOPB "Build nanopb-based serde support" OFF)
if(COAP_PP_BUILD_SERDE_NANOPB)
    add_subdirectory(src/coap_pp_serde_nanopb)
endif()

# nlohmann/json serde
option(COAP_PP_BUILD_SERDE_JSON "Build nlohmann/json-based serde support" OFF)
if(COAP_PP_BUILD_SERDE_JSON)
    add_subdirectory(src/coap_pp_serde_json)
endif()

# POSIX transport + examples (Linux / macOS only)
option(COAP_PP_BUILD_POSIX_TRANSPORT "Build the POSIX UDP transport" OFF)
if(COAP_PP_BUILD_POSIX_TRANSPORT)
    add_subdirectory(src/coap_pp_transport_posix)
endif()

# UDP/IP/SLIP transport
option(COAP_PP_BUILD_TRANSPORT_UDP_IP_SLIP "Build UDP/IP/SLIP transport" OFF)
if(COAP_PP_BUILD_TRANSPORT_UDP_IP_SLIP)
    add_subdirectory(src/coap_pp_transport_udp_ip_slip)
endif()

# Examples
option(COAP_PP_BUILD_EXAMPLES "Build example programs" ON)
if(COAP_PP_BUILD_POSIX_TRANSPORT AND COAP_PP_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# Tests
option(COAP_PP_BUILD_TESTS "Build coap-pp tests" ON)
if(COAP_PP_BUILD_TESTS)
    add_subdirectory(tests)
endif()
