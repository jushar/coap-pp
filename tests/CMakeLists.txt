# Copyright (c) 2026 jushar
# SPDX-License-Identifier: MIT

include(CTest)
include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.16.0
    FIND_PACKAGE_ARGS
)
set(INSTALL_GTEST OFF)
FetchContent_MakeAvailable(googletest)

include(GoogleTest)

add_subdirectory(coap_pp)
