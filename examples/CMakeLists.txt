# Copyright (c) 2026 jushar
# SPDX-License-Identifier: MIT

add_executable(coap-hello-world hello_world.cpp)

target_link_libraries(coap-hello-world PRIVATE coap-pp-transport-posix)
target_compile_features(coap-hello-world PRIVATE cxx_std_20)
