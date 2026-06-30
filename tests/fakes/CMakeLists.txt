# Copyright (c) 2026 jushar
# SPDX-License-Identifier: MIT

add_library(coap-pp-fakes INTERFACE)

target_include_directories(coap-pp-fakes INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(coap-pp-fakes INTERFACE
    coap-pp
    GTest::gtest
)
