# Copyright (c) 2026 jushar
# SPDX-License-Identifier: MIT

add_library(coap-pp-transport-posix STATIC
    udp_transport.cpp
)
find_package(Threads REQUIRED)
target_link_libraries(coap-pp-transport-posix PUBLIC coap-pp Threads::Threads PRIVATE coap-pp-warnings coap-pp-serde-nanopb)
