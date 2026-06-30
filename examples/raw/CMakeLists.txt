# Copyright (c) 2026 jushar
# SPDX-License-Identifier: MIT

add_executable(coap-raw raw.cpp)

target_link_libraries(coap-raw PRIVATE coap-pp-transport-posix coap-pp)
