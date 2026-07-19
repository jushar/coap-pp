# Copyright (c) 2026 jushar
# SPDX-License-Identifier: MIT

add_executable(coap-blockwise blockwise.cpp)

target_link_libraries(coap-blockwise PRIVATE coap-pp-transport-posix coap-pp)
