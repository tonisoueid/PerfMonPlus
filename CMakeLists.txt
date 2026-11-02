# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 PerfMonPlus contributors

cmake_minimum_required(VERSION 3.24)
project(PerfMonPlus VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

function(pmp_set_warnings target)
  target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
endfunction()

# The core, the Linux providers, the trace format, the view models and the probe need no Qt: with
# -DPMP_BUILD_GUI=OFF they build and test on a machine that has none.
option(PMP_BUILD_GUI "Build the Qt 6 user interface (needs Qt 6.4 Widgets and Network)" ON)
if(PMP_BUILD_GUI)
  find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets Network)
  set(CMAKE_AUTOMOC ON)
endif()

enable_testing()
add_subdirectory(core)
add_subdirectory(platform/linux)
add_subdirectory(trace)
add_subdirectory(apps/probe)
add_subdirectory(ui/viewmodel)
if(PMP_BUILD_GUI)
  add_subdirectory(ui/gui)
  add_subdirectory(apps/perfmonplus)
endif()
add_subdirectory(tests)
