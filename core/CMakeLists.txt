# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 PerfMonPlus contributors

add_library(pmp_core STATIC
  src/version.cpp
  src/format.cpp
  src/gap.cpp
  src/sampler.cpp
  src/history_store.cpp
  src/process_history.cpp
)
target_include_directories(pmp_core PUBLIC include)
target_compile_definitions(pmp_core PRIVATE PMP_VERSION="${PROJECT_VERSION}")
pmp_set_warnings(pmp_core)
