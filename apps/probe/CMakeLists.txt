# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 PerfMonPlus contributors

find_package(nlohmann_json 3 REQUIRED)

add_library(pmp_probe_json OBJECT frame_json.cpp)
target_include_directories(pmp_probe_json PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(pmp_probe_json PUBLIC pmp_core nlohmann_json::nlohmann_json)
pmp_set_warnings(pmp_probe_json)

add_executable(perfmonplus-probe main.cpp)
target_link_libraries(perfmonplus-probe PRIVATE pmp_probe_json pmp_linux)
pmp_set_warnings(perfmonplus-probe)
