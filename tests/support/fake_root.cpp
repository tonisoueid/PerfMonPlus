// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include "support/fake_root.hpp"

#include <cstdlib>
#include <fstream>
#include <stdexcept>
#include <string>

FakeRoot::FakeRoot() {
    auto tmpl = (std::filesystem::temp_directory_path() / "pmp-fake-XXXXXX").string();
    if (!mkdtemp(tmpl.data())) throw std::runtime_error("mkdtemp failed");
    dir_ = tmpl;
}

FakeRoot::~FakeRoot() {
    std::error_code ec;
    for (const auto& p : unreadable_) std::filesystem::permissions(p, std::filesystem::perms::owner_all, ec);
    std::filesystem::remove_all(dir_, ec);
}

void FakeRoot::write(std::string_view rel, std::string_view content) {
    const auto p = dir_ / std::string(rel);
    std::filesystem::create_directories(p.parent_path());
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f << content;
}

void FakeRoot::remove(std::string_view rel) {
    std::error_code ec;
    std::filesystem::remove_all(dir_ / std::string(rel), ec);
}

void FakeRoot::makeDir(std::string_view rel) {
    std::filesystem::create_directories(dir_ / std::string(rel));
}

void FakeRoot::symlink(std::string_view target, std::string_view rel) {
    const auto p = dir_ / std::string(rel);
    std::filesystem::create_directories(p.parent_path());
    std::filesystem::create_symlink(std::string(target), p);
}

void FakeRoot::makeUnreadable(std::string_view rel) {
    const auto p = dir_ / std::string(rel);
    std::filesystem::permissions(p, std::filesystem::perms::none);
    unreadable_.push_back(p);
}

pmp::linux::ProcRoot FakeRoot::root() const {
    pmp::linux::ProcRoot r;
    r.proc = dir_ / "proc";
    r.sys = dir_ / "sys";
    r.clockTicksPerSec = 100;
    r.pageSize = 4096;
    return r;
}
