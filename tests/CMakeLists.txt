# Copyright (c) 2026 jushar
# SPDX-License-Identifier: MIT

include(CTest)
include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.16.0
    FIND_PACKAGE_ARGS
)
set(INSTALL_GTEST OFF)
FetchContent_MakeAvailable(googletest)

add_executable(coap-pp-tests
    test_transport_interface.cpp
    test_pdu.cpp
    test_serialize.cpp
    test_messenger.cpp
    test_server.cpp
)

target_link_libraries(coap-pp-tests PRIVATE
    coap-pp
    GTest::gtest_main
    coap-pp-warnings
)

include(GoogleTest)
gtest_discover_tests(coap-pp-tests)

