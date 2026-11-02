# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 PerfMonPlus contributors

add_executable(perfmonplus main.cpp)
target_link_libraries(perfmonplus PRIVATE pmp_gui)
pmp_set_warnings(perfmonplus)

include(GNUInstallDirs)
install(TARGETS perfmonplus RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES ${PROJECT_SOURCE_DIR}/packaging/perfmonplus.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES ${PROJECT_SOURCE_DIR}/ui/gui/resources/perfmonplus.svg
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
