# Copyright (c) 2026 jushar
# SPDX-License-Identifier: MIT

find_package(Threads REQUIRED)

add_library(coap-pp-transport-udp-ip-slip STATIC
    udp_ip_slip_transport.cpp
)
target_link_libraries(coap-pp-transport-udp-ip-slip PUBLIC coap-pp Threads::Threads PRIVATE coap-pp-warnings)
