# Copyright (c) 2026 jushar
# SPDX-License-Identifier: MIT

# COAP_PP_NANOPB_GENERATE_CPP(SRCS HDRS [RELPATH <dir>] <proto-files>...)
#
# Wraps nanopb_generate_cpp and enables nanopb's --cpp-descriptors generator option, which
# emits a nanopb::MessageDescriptor<T> specialization for every message into the generated
# .pb.h. coap-pp-serde-nanopb relies on these descriptors to look up a message's fields and
# maximum encoded size by type.
#
# Calling nanopb_generate_cpp directly works too, as long as NANOPB_OPTIONS contains
# --cpp-descriptors (or nanopb was found via find_package(Nanopb COMPONENTS cpp-descriptors)).
#
# Example:
#   coap_pp_nanopb_generate_cpp(PROTO_SRCS PROTO_HDRS foo.proto)
#   add_executable(my_app main.cpp ${PROTO_SRCS})
#   target_include_directories(my_app PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

function(COAP_PP_NANOPB_GENERATE_CPP SRCS HDRS)
    if(NOT ARGN)
        message(FATAL_ERROR "COAP_PP_NANOPB_GENERATE_CPP: no .proto files given")
    endif()

    # NANOPB_OPTIONS is read by nanopb_generate_cpp at call time; modifying it here only affects
    # this function's scope. nanopb splices it verbatim into a single --nanopb_opt argument, so
    # options must be space-separated (a CMake list would be split into separate protoc args).
    string(REPLACE ";" " " NANOPB_OPTIONS "${NANOPB_OPTIONS}")
    string(FIND "${NANOPB_OPTIONS}" "--cpp-descriptors" _cpp_descriptors_pos)
    if(_cpp_descriptors_pos EQUAL -1)
        string(STRIP "${NANOPB_OPTIONS} --cpp-descriptors" NANOPB_OPTIONS)
    endif()

    nanopb_generate_cpp(${SRCS} ${HDRS} ${ARGN})

    set(${SRCS} ${${SRCS}} PARENT_SCOPE)
    set(${HDRS} ${${HDRS}} PARENT_SCOPE)
endfunction()
