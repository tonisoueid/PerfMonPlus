// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#pragma once

#include <perfmonplus/core/frame.hpp>
#include <perfmonplus/core/history_store.hpp>
#include <perfmonplus/linux/system_info.hpp>
#include <perfmonplus/vm/theme.hpp>

namespace pmp::gui {

// Implemented by pages that show live data; MainWindow feeds the visible one.
class LiveView {
public:
    virtual ~LiveView() = default;
    virtual void update(const Frame& frame, const HistoryStore& history, MonoTime now) = 0;
    virtual void setTheme(const vm::Theme& theme, bool bloom) = 0;
    // Called by the animation clock while the page is visible: ease meters, scroll graphs.
    virtual void tick(double dtSeconds) { (void)dtSeconds; }
    // Lower animation rates also scroll graphs in coarser steps (vm::scrollQuantumPx).
    virtual void setAnimationRate(int hz) { (void)hz; }
    // Static machine facts, collected once in the background after start-up.
    virtual void setSystemInfo(const linux::SystemInfo& info) { (void)info; }
    // Settings: the graph history window and how many processes the Summary lists.
    virtual void setHistoryWindow(int seconds) { (void)seconds; }
    virtual void setTopProcessCount(int count) { (void)count; }
    // False for pages with nothing moving: the animation clock then stays off.
    virtual bool animates() const { return true; }
};

}  // namespace pmp::gui
