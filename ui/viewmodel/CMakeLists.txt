# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 PerfMonPlus contributors

add_library(pmp_viewmodel STATIC
  src/color.cpp
  src/theme.cpp
  src/graph_geometry.cpp
  src/hardware_model.cpp
  src/meter.cpp
  src/summary_model.cpp
  src/tables_model.cpp
  src/process_model.cpp
  src/process_table.cpp
  src/process_details.cpp
  src/tunnel.cpp
  src/version_compare.cpp
  src/system_model.cpp
  src/performance_model.cpp
  src/power_freq_model.cpp
  src/transport.cpp
  src/connections_model.cpp
  src/apps_model.cpp
  src/treemap.cpp
  src/disk_space_model.cpp
  src/bench_model.cpp
)
target_include_directories(pmp_viewmodel PUBLIC include)
target_link_libraries(pmp_viewmodel PUBLIC pmp_core pmp_linux)
pmp_set_warnings(pmp_viewmodel)
