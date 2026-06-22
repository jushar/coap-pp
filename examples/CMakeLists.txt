# Copyright (c) 2026 jushar
# SPDX-License-Identifier: MIT

coap_pp_nanopb_generate_cpp(PROTO_SRCS PROTO_HDRS PROTO_FIELDS hello_world.proto)

add_executable(coap-hello-world hello_world.cpp ${PROTO_SRCS} ${PROTO_FIELDS})

target_include_directories(coap-hello-world PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(coap-hello-world PRIVATE coap-pp-transport-posix coap-pp-serde-nanopb)
