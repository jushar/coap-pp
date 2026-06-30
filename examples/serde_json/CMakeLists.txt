# Copyright (c) 2026 jushar
# SPDX-License-Identifier: MIT

add_executable(coap-serde-json serde_json.cpp)

target_link_libraries(coap-serde-json PRIVATE coap-pp-transport-posix coap-pp-serde-json)
