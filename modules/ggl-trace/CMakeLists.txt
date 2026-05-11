# aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

ggl_init_module(ggl-trace LIBS gg-sdk)

# Use --wrap=gg_log to intercept all calls to gg_log and redirect them to
# __wrap_gg_log (defined in trace_log.c). The original is available as
# __real_gg_log. This INTERFACE property propagates to all binaries that
# link ggl-trace (directly or transitively).
target_link_options(ggl-trace INTERFACE "LINKER:--wrap=gg_log")
