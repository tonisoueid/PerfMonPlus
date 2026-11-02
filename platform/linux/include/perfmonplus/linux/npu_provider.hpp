// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#pragma once

#include <perfmonplus/core/provider.hpp>
#include <perfmonplus/linux/proc_fs.hpp>

#include <map>
#include <optional>

namespace pmp::linux {

// NPUs from /sys/class/accel (§7.2.4). Active time is the delta of the driver's busy-time counter.
class NpuProvider final : public Provider {
public:
    explicit NpuProvider(ProcRoot root) : root_(std::move(root)) {}

    std::string_view id() const override { return "npu"; }
    void sample(Frame& out, const SampleContext& ctx) override;
    void fillUnavailable(Frame& out, Availability why) const override;
    void reset() override {
        prevBusyUs_.clear();
        prevTime_.reset();
    }

private:
    ProcRoot root_;
    std::map<std::string, double> prevBusyUs_;
    std::optional<MonoTime> prevTime_;
};

}  // namespace pmp::linux
