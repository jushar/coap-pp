# Copyright (c) 2026 jushar
# SPDX-License-Identifier: MIT

# COAP_PP_NANOPB_GENERATE_CPP(SRCS HDRS FIELDS_HDRS [RELPATH <dir>] <proto-files>...)
#
# Wraps nanopb_generate_cpp and additionally invokes protoc-gen-coap_pp_fields
# to produce <name>.coap_pp_fields.hpp for every .proto file.
#
# FIELDS_HDRS  - Output variable populated with paths to the generated
#                NanopbFields specialization headers.
#
# The generated headers are written to CMAKE_CURRENT_BINARY_DIR (alongside the
# nanopb .pb.h files) and only need an #include to be pulled in by consumers.
#
# Example:
#   coap_pp_nanopb_generate_cpp(PROTO_SRCS PROTO_HDRS PROTO_FIELDS foo.proto)
#   add_executable(my_app main.cpp ${PROTO_SRCS} ${PROTO_FIELDS})
#   target_include_directories(my_app PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

set(_COAP_PP_FIELDS_PLUGIN
    "${CMAKE_CURRENT_LIST_DIR}/protoc-gen-coap_pp_fields.py"
    CACHE FILEPATH "Path to the protoc-gen-coap_pp_fields plugin script")

function(COAP_PP_NANOPB_GENERATE_CPP SRCS HDRS FIELDS_HDRS)
    cmake_parse_arguments(_ARG "" "RELPATH" "" ${ARGN})

    if(NOT _ARG_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "COAP_PP_NANOPB_GENERATE_CPP: no .proto files given")
    endif()

    if(NOT EXISTS "${_COAP_PP_FIELDS_PLUGIN}")
        message(FATAL_ERROR
            "protoc-gen-coap_pp_fields plugin not found at "
            "'${_COAP_PP_FIELDS_PLUGIN}'. "
            "Set COAP_PP_FIELDS_PLUGIN to the correct path.")
    endif()

    # Forward to nanopb's own generator for .pb.c / .pb.h
    if(_ARG_RELPATH)
        nanopb_generate_cpp(${SRCS} ${HDRS} RELPATH ${_ARG_RELPATH}
                            ${_ARG_UNPARSED_ARGUMENTS})
    else()
        nanopb_generate_cpp(${SRCS} ${HDRS} ${_ARG_UNPARSED_ARGUMENTS})
    endif()

    # Propagate the nanopb variables up to the caller's scope
    set(${SRCS} ${${SRCS}} PARENT_SCOPE)
    set(${HDRS} ${${HDRS}} PARENT_SCOPE)

    # Run the coap_pp_fields plugin for each proto file
    set(_fields_hdrs)
    foreach(FIL ${_ARG_UNPARSED_ARGUMENTS})
        get_filename_component(ABS_FIL  ${FIL} ABSOLUTE)
        get_filename_component(FIL_WE   ${FIL} NAME_WLE)
        get_filename_component(ABS_PATH ${ABS_FIL} PATH)

        set(FIELDS_HDR "${CMAKE_CURRENT_BINARY_DIR}/${FIL_WE}.coap_pp_fields.hpp")
        list(APPEND _fields_hdrs "${FIELDS_HDR}")

        add_custom_command(
            OUTPUT  "${FIELDS_HDR}"
            COMMAND "${PROTOBUF_PROTOC_EXECUTABLE}"
                    "--plugin=protoc-gen-coap_pp_fields=${_COAP_PP_FIELDS_PLUGIN}"
                    "--coap_pp_fields_out=${CMAKE_CURRENT_BINARY_DIR}"
                    "-I${ABS_PATH}"
                    "${ABS_FIL}"
            DEPENDS "${ABS_FIL}" "${_COAP_PP_FIELDS_PLUGIN}"
            COMMENT "Generating NanopbFields specializations for ${FIL}"
            VERBATIM
        )
    endforeach()

    set(${FIELDS_HDRS} ${_fields_hdrs} PARENT_SCOPE)
endfunction()
