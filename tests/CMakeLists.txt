# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 PerfMonPlus contributors

find_package(Catch2 3 REQUIRED)
include(Catch)

add_executable(pmp_tests
  core/test_version.cpp
  core/test_reading.cpp
  core/test_format.cpp
  core/test_history_ring.cpp
  core/test_gap.cpp
  core/test_sampler.cpp
  core/test_history_store.cpp
  core/test_process_history.cpp
  support/fake_root.cpp
  linux/test_proc_fs.cpp
  linux/test_memory_provider.cpp
  linux/test_cpu_provider.cpp
  linux/test_cpu_facts.cpp
  linux/test_thermal_provider.cpp
  linux/test_energy_provider.cpp
  linux/test_connections.cpp
  linux/test_installed_apps.cpp
  linux/test_disk_scan.cpp
  linux/test_bench_kernels.cpp
  linux/test_bench_store.cpp
  linux/test_drm.cpp
  linux/test_gpu_provider.cpp
  linux/test_npu_provider.cpp
  linux/test_command.cpp
  linux/test_services.cpp
  linux/test_sessions.cpp
  linux/test_startup.cpp
  linux/test_system_info.cpp
  linux/test_disk_provider.cpp
  linux/test_net_provider.cpp
  linux/test_process_provider.cpp
  linux/test_process_inspector.cpp
  linux/test_process_actions.cpp
  linux/test_identity.cpp
  probe/test_frame_json.cpp
  trace/test_container.cpp
  trace/test_frame_codec.cpp
  trace/test_recorder.cpp
  trace/test_player.cpp
  trace/test_golden.cpp
  viewmodel/test_theme.cpp
  viewmodel/test_graph_geometry.cpp
  viewmodel/test_meter.cpp
  viewmodel/test_summary_model.cpp
  viewmodel/test_hardware_model.cpp
  viewmodel/test_performance_model.cpp
  viewmodel/test_power_freq_model.cpp
  viewmodel/test_transport.cpp
  viewmodel/test_connections_model.cpp
  viewmodel/test_apps_model.cpp
  viewmodel/test_treemap.cpp
  viewmodel/test_disk_space_model.cpp
  viewmodel/test_bench_model.cpp
  viewmodel/test_system_model.cpp
  viewmodel/test_tables_model.cpp
  viewmodel/test_process_model.cpp
  viewmodel/test_process_table.cpp
  viewmodel/test_process_details.cpp
  viewmodel/test_graph_cursor.cpp
  viewmodel/test_tunnel.cpp
  viewmodel/test_version_compare.cpp

)
if(CMAKE_BUILD_TYPE MATCHES "^(Release|RelWithDebInfo)$")
  target_compile_definitions(pmp_tests PRIVATE PMP_OPTIMIZED=1)
endif()
target_link_libraries(pmp_tests PRIVATE pmp_core pmp_linux pmp_trace pmp_probe_json pmp_viewmodel Catch2::Catch2WithMain)
target_include_directories(pmp_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(pmp_tests PRIVATE PMP_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
pmp_set_warnings(pmp_tests)
catch_discover_tests(pmp_tests)

# Tests that touch the real /proc, fork, or spin CPUs. Excluded from the unit run with `ctest -LE live`.
add_executable(pmp_live_tests
  live/test_live_identity.cpp
  live/test_process_actions_live.cpp
  live/test_live_crosscheck.cpp
  live/test_connections_live.cpp
  live/test_disk_scan_live.cpp
  live/test_bench_live.cpp
  live/test_recorder_memory_live.cpp
)
target_link_libraries(pmp_live_tests PRIVATE pmp_core pmp_linux pmp_trace Catch2::Catch2WithMain)
target_include_directories(pmp_live_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
pmp_set_warnings(pmp_live_tests)
catch_discover_tests(pmp_live_tests PROPERTIES LABELS live)
target_compile_definitions(pmp_live_tests PRIVATE PMP_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
if(PMP_BUILD_GUI)  # the one live test that starts the real app
  target_sources(pmp_live_tests PRIVATE live/test_live_gui_cpu.cpp)
  add_dependencies(pmp_live_tests perfmonplus)
  target_compile_definitions(pmp_live_tests PRIVATE PMP_APP_PATH="$<TARGET_FILE:perfmonplus>")
endif()
if(CMAKE_BUILD_TYPE MATCHES "^(Release|RelWithDebInfo)$")
  target_compile_definitions(pmp_live_tests PRIVATE PMP_OPTIMIZED=1)
endif()


if(PMP_BUILD_GUI)
  # GUI tests: Catch2 with a custom main that owns the QApplication (offscreen by default).
  add_executable(pmp_gui_tests
    gui/gui_main.cpp
    support/fake_root.cpp
    gui/test_main_window.cpp
    gui/test_sampling_service.cpp
    gui/test_meter_widgets.cpp
    gui/test_shell.cpp
    gui/test_app_icon.cpp
    gui/test_summary_page.cpp
    gui/test_performance_page.cpp
    gui/test_system_info_page.cpp
    gui/test_power_freq_page.cpp
    gui/test_recording_controller.cpp
    gui/test_recording_ui.cpp
    gui/test_replay.cpp
    gui/test_connections_page.cpp
    gui/test_installed_apps_page.cpp
    gui/test_disk_space_page.cpp
    gui/test_benchmarks_page.cpp
    gui/test_video_recorder.cpp
    gui/test_video_ui.cpp
    gui/test_services_page.cpp
    gui/test_startup_users.cpp
    gui/test_settings_dialog.cpp
    gui/test_screensaver.cpp
    gui/test_update_checker.cpp
    gui/test_processes_page.cpp
    gui/test_process_details_pane.cpp
    gui/test_process_actions_controller.cpp
    gui/test_graph_hover.cpp
    gui/test_culprits.cpp
    gui/test_page_links.cpp
    gui/test_processes_e2e.cpp
    gui/test_colors.cpp
    gui/test_animation_clock.cpp
  )
  target_link_libraries(pmp_gui_tests PRIVATE pmp_gui Catch2::Catch2)
  target_compile_definitions(pmp_gui_tests PRIVATE PMP_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
  target_include_directories(pmp_gui_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  pmp_set_warnings(pmp_gui_tests)
  catch_discover_tests(pmp_gui_tests PROPERTIES ENVIRONMENT QT_QPA_PLATFORM=offscreen)

  # A stand-in for ffmpeg, so the video recorder is tested without encoding anything.
  add_executable(fake_ffmpeg support/fake_ffmpeg.cpp)
  pmp_set_warnings(fake_ffmpeg)
  add_dependencies(pmp_gui_tests fake_ffmpeg)
  target_compile_definitions(pmp_gui_tests PRIVATE PMP_FAKE_FFMPEG="$<TARGET_FILE:fake_ffmpeg>")
endif()

# The probe's argument checks: each of these must be refused with exit status 2 and nothing on stdout.
foreach(bad IN ITEMS "--interval-ms|3600001" "--interval-ms|0" "--proc-root|" "--sys-root|" "--count|-1")
  string(REPLACE "|" ";" args "${bad}")
  list(GET args 0 flag)
  list(LENGTH args n)
  if(n GREATER 1)
    list(GET args 1 value)
  else()
    set(value "")
  endif()
  string(MAKE_C_IDENTIFIER "probe_rejects_${flag}_${value}" tname)
  add_test(NAME ${tname} COMMAND sh -c "out=$(\"$0\" \"$1\" \"$2\" 2>/dev/null); st=$?; [ $st -eq 2 ] && [ -z \"$out\" ]"
           $<TARGET_FILE:perfmonplus-probe> ${flag} "${value}")
  set_tests_properties(${tname} PROPERTIES LABELS probe)
endforeach()
add_dependencies(pmp_tests perfmonplus-probe)
