# aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

ggl_init_module(ggl-trace LIBS gg-sdk)

if(BUILD_TESTING AND GG_LOG_TRACE)
  include(CTest)
  add_executable(ggl_trace_test test/trace_test.c)
  target_link_libraries(ggl_trace_test PRIVATE ggl-trace gg-sdk pthread)
  target_compile_definitions(ggl_trace_test PRIVATE "GG_MODULE=(\"ggl_trace_test\")")
  target_compile_definitions(ggl_trace_test PRIVATE _GNU_SOURCE)
  add_test(NAME ggl_trace_test COMMAND ggl_trace_test)
endif()
