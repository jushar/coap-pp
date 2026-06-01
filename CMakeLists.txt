cmake_minimum_required(VERSION 3.30)
project(coap-pp VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_library(coap-pp-warnings INTERFACE)
target_compile_options(coap-pp-warnings INTERFACE
    $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:
        -Wall -Wpedantic -Werror -Wno-array-bounds>
)

add_library(coap-pp STATIC
    src/coap_pp/pdu/deserialize.cpp
    src/coap_pp/pdu/serialize.cpp
    src/coap_pp/messaging/messenger.cpp
    src/coap_pp/server/coap_server.cpp
    src/coap_pp/server/resource.cpp
)

target_compile_features(coap-pp PUBLIC cxx_std_20)
target_link_libraries(coap-pp PRIVATE coap-pp-warnings)

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
target_compile_features(coap-pp-transport-udp-ip-slip PUBLIC cxx_std_20)

# POSIX transport + examples (Linux / macOS only)
if(UNIX)
    option(COAP_PP_BUILD_POSIX_TRANSPORT "Build the POSIX UDP transport" ON)

    if(COAP_PP_BUILD_POSIX_TRANSPORT)
        add_library(coap-pp-transport-posix STATIC
            src/coap_pp_transport_posix/udp_transport.cpp
        )
        target_link_libraries(coap-pp-transport-posix PUBLIC coap-pp Threads::Threads PRIVATE coap-pp-warnings)
        target_compile_features(coap-pp-transport-posix PUBLIC cxx_std_20)

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
