# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 PerfMonPlus contributors

add_library(pmp_linux STATIC
  src/proc_fs.cpp
  src/memory_provider.cpp
  src/command.cpp
  src/cpu_facts.cpp
  src/cpu_provider.cpp
  src/disk_provider.cpp
  src/drm.cpp
  src/gpu_provider.cpp
  src/energy_provider.cpp
  src/net_provider.cpp
  src/npu_provider.cpp
  src/process_provider.cpp
  src/process_inspector.cpp
  src/process_actions.cpp
  src/connections.cpp
  src/installed_apps.cpp
  src/disk_scan.cpp
  src/bench_kernels.cpp
  src/bench_store.cpp
  src/kernel_modules.cpp
  src/services.cpp
  src/sessions.cpp
  src/startup.cpp
  src/system_info.cpp
  src/thermal_provider.cpp
  src/identity.cpp
)
target_include_directories(pmp_linux PUBLIC include)
find_package(nlohmann_json 3 REQUIRED)
target_link_libraries(pmp_linux PUBLIC pmp_core PRIVATE nlohmann_json::nlohmann_json)
pmp_set_warnings(pmp_linux)
