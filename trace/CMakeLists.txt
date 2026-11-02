# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 PerfMonPlus contributors

find_package(ZLIB REQUIRED)
find_package(nlohmann_json 3 REQUIRED)
add_library(pmp_trace STATIC
  src/container.cpp
  src/frame_codec.cpp
  src/chunk.cpp
  src/header.cpp
  src/recorder.cpp
  src/player.cpp
)
target_include_directories(pmp_trace PUBLIC include)
target_link_libraries(pmp_trace PUBLIC pmp_core PRIVATE ZLIB::ZLIB nlohmann_json::nlohmann_json)
pmp_set_warnings(pmp_trace)
