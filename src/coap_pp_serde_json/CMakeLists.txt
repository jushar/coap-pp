# Copyright (c) 2026 jushar
# SPDX-License-Identifier: MIT

include(FetchContent)

set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
set(JSON_Install OFF CACHE BOOL "" FORCE)
set(JSON_NoExceptions ON CACHE BOOL "" FORCE)

FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    FIND_PACKAGE_ARGS NAMES nlohmann_json
)
FetchContent_MakeAvailable(nlohmann_json)

add_library(coap-pp-serde-json INTERFACE)
target_link_libraries(coap-pp-serde-json INTERFACE coap-pp nlohmann_json::nlohmann_json)
