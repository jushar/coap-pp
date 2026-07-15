# Copyright (c) 2026 jushar
# SPDX-License-Identifier: MIT

add_executable(coap-pp-tests
    messaging/test_messenger.cpp
    pdu/test_pdu.cpp
    pdu/test_serialize.cpp
    server/test_server.cpp
    transport/test_endpoint.cpp
    util/test_inplace_function.cpp
    util/test_memory_pool.cpp
    util/test_panic.cpp
)

target_link_libraries(coap-pp-tests PRIVATE
    coap-pp
    coap-pp-fakes
    GTest::gtest_main
)

gtest_discover_tests(coap-pp-tests)
