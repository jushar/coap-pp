cmake_minimum_required(VERSION 3.30)
project(coap-pp VERSION 0.1.0 LANGUAGES CXX)

add_library(coap-pp STATIC
    src/coap_pp/pdu/deserialize.cpp
    src/coap_pp/pdu/serialize.cpp
    src/coap_pp/messaging/messenger.cpp
    src/coap_pp/server/coap_server.cpp
    src/coap_pp/server/resource.cpp
)

target_compile_features(coap-pp PUBLIC cxx_std_20)

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
target_link_libraries(coap-pp-transport-udp-ip-slip PUBLIC coap-pp Threads::Threads)
target_compile_features(coap-pp-transport-udp-ip-slip PUBLIC cxx_std_20)

# POSIX transport + examples (Linux / macOS only)
if(UNIX)
    add_library(coap-pp-transport-posix STATIC
        src/coap_pp_transport_posix/udp_transport.cpp
    )
    target_link_libraries(coap-pp-transport-posix PUBLIC coap-pp Threads::Threads)
    target_compile_features(coap-pp-transport-posix PUBLIC cxx_std_20)

    option(BUILD_EXAMPLES "Build example programs" ON)
    if(BUILD_EXAMPLES)
        add_subdirectory(examples)
    endif()
endif()

# Tests
include(CTest)
if(BUILD_TESTING)
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
