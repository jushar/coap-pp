# Copyright (c) 2026 jushar
# SPDX-License-Identifier: MIT

add_executable(coap-pp-serde-json-tests
    test_json_router.cpp
)

target_link_libraries(coap-pp-serde-json-tests PRIVATE
    coap-pp
    coap-pp-fakes
    coap-pp-serde-json
    GTest::gtest_main
)

gtest_discover_tests(coap-pp-serde-json-tests)
