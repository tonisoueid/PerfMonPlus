// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#pragma once

#include <perfmonplus/linux/proc_fs.hpp>

#include <filesystem>
#include <string_view>
#include <vector>

// A throwaway fake /proc + /sys tree for provider tests.
class FakeRoot {
public:
    FakeRoot();
    ~FakeRoot();
    FakeRoot(const FakeRoot&) = delete;
    FakeRoot& operator=(const FakeRoot&) = delete;

    void write(std::string_view rel, std::string_view content);  // rel like "proc/meminfo"
    void remove(std::string_view rel);
    void makeDir(std::string_view rel);
    void symlink(std::string_view target, std::string_view rel);  // creates rel -> target
    void makeUnreadable(std::string_view rel);  // chmod 000
    pmp::linux::ProcRoot root() const;

private:
    std::filesystem::path dir_;
    std::vector<std::filesystem::path> unreadable_;
};
