# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 PerfMonPlus contributors

add_executable(perfmonplus main.cpp)
target_link_libraries(perfmonplus PRIVATE pmp_gui)
pmp_set_warnings(perfmonplus)
