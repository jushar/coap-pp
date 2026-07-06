# Copyright (c) 2026 jushar
# SPDX-License-Identifier: MIT

add_subdirectory(raw)

if(COAP_PP_BUILD_SERDE_NANOPB)
    add_subdirectory(serde_nanopb)
endif()

if(COAP_PP_BUILD_SERDE_JSON)
    add_subdirectory(serde_json)
endif()
