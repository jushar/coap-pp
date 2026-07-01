# Copyright (c) 2026 jushar
# SPDX-License-Identifier: MIT

if(NOT TARGET nanopb)
    include(FetchContent)
    FetchContent_Declare(
        nanopb
        GIT_REPOSITORY https://github.com/nanopb/nanopb.git
        GIT_TAG        0.4.9.1
        FIND_PACKAGE_ARGS
    )
    FetchContent_MakeAvailable(nanopb)
endif()

if(DEFINED nanopb_SOURCE_DIR AND NOT COMMAND NANOPB_GENERATE_CPP)
    set(NANOPB_GENERATOR_SOURCE_DIR "${nanopb_SOURCE_DIR}/generator" CACHE PATH "nanopb generator source" FORCE)
    include(${nanopb_SOURCE_DIR}/extra/FindNanopb.cmake)
endif()

include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/coap_pp_serde_nanopb/NanopbGenerate.cmake)

add_library(coap-pp-serde-nanopb INTERFACE)
target_link_libraries(coap-pp-serde-nanopb INTERFACE coap-pp nanopb)

