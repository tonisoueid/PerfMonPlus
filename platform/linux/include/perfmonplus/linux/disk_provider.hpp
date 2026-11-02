// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#pragma once

#include <perfmonplus/core/provider.hpp>
#include <perfmonplus/linux/proc_fs.hpp>

#include <map>
#include <optional>

namespace pmp::linux {

class DiskProvider final : public Provider {
public:
    explicit DiskProvider(ProcRoot root) : root_(std::move(root)) {}

    std::string_view id() const override { return "disks"; }
    void sample(Frame& out, const SampleContext& ctx) override;
    void fillUnavailable(Frame& out, Availability why) const override;
    void reset() override {
        prev_.clear();
        prevTime_.reset();
    }

private:
    struct Stats {
        std::uint64_t reads = 0, sectorsRead = 0, msReading = 0;
        std::uint64_t writes = 0, sectorsWritten = 0, msWriting = 0, msIo = 0;
    };

    ProcRoot root_;
    std::map<std::string, Stats, std::less<>> prev_;
    std::optional<MonoTime> prevTime_;
};

}  // namespace pmp::linux
