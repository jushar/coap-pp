# Copyright (c) 2026 jushar
# SPDX-License-Identifier: MIT

coap_pp_nanopb_generate_cpp(PROTO_SRCS PROTO_HDRS PROTO_FIELDS serde_test.proto)

add_executable(coap-pp-serde-nanopb-tests
    test_nanopb_router.cpp
    ${PROTO_SRCS}
    ${PROTO_FIELDS}
)

target_include_directories(coap-pp-serde-nanopb-tests PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}
)

target_link_libraries(coap-pp-serde-nanopb-tests PRIVATE
    coap-pp
    coap-pp-fakes
    coap-pp-serde-nanopb
    GTest::gtest_main
)

gtest_discover_tests(coap-pp-serde-nanopb-tests)
