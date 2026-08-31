# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Gejile Hu. All rights reserved.
#
# T4's performance baselines. Off by default and never part of a pull request:
# a benchmark on a shared CI runner measures the runner as much as the code, so
# these belong to the nightly gate where the comparison is against yesterday's
# number on the same kind of machine.
#
# google-benchmark comes from the vcpkg `tests` feature. If it is not installed
# this directory says so and builds nothing, because a missing benchmark library
# must never be able to fail a build that was only asked for the simulator.

find_package(benchmark CONFIG QUIET)

if(NOT benchmark_FOUND)
  message(STATUS "benchmarks: google-benchmark not found, skipping")
  return()
endif()

# A Debug benchmark measures the debug runtime, not the model: at target scale
# it reports about 4.6 s against RelWithDebInfo's 1.9 s. google-benchmark says
# so at run time too, but by then someone has already read the number.
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  message(WARNING "benchmarks: configured Debug - timings will not mean anything. Use --preset release.")
endif()

add_executable(paddock_benchmarks StepBenchmark.cpp)

target_link_libraries(paddock_benchmarks PRIVATE Paddock::core benchmark::benchmark
                                                 Paddock::project_options)

message(STATUS "benchmarks: enabled")
