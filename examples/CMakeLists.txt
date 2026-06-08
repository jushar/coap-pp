# Copyright (c) 2026 jushar
# SPDX-License-Identifier: MIT

nanopb_generate_cpp(PROTO_SRCS PROTO_HDRS hello_world.proto)

add_executable(coap-hello-world hello_world.cpp ${PROTO_SRCS})

target_include_directories(coap-hello-world PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(coap-hello-world PRIVATE coap-pp-transport-posix coap-pp-serde-nanopb)
target_compile_features(coap-hello-world PRIVATE cxx_std_20)
