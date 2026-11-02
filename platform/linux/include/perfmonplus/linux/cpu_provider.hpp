// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#pragma once

#include <perfmonplus/core/provider.hpp>
#include <perfmonplus/linux/proc_fs.hpp>

#include <map>
#include <vector>
#include <string>

namespace pmp::linux {

class CpuProvider final : public Provider {
public:
    explicit CpuProvider(ProcRoot root) : root_(std::move(root)) {}

    std::string_view id() const override { return "cpu"; }
    void sample(Frame& out, const SampleContext& ctx) override;
    void fillUnavailable(Frame& out, Availability why) const override;
    void reset() override { prev_.clear(); }

private:
    struct Counters {
        std::uint64_t busy = 0, idle = 0, kernel = 0;
    };

    void classifyCores(const std::vector<int>& ids);

    ProcRoot root_;
    std::map<int, Counters> prev_;  // key -1 = aggregate "cpu" line
    std::map<int, CoreClass> classes_;
    bool classified_ = false;
    bool policyRead_ = false, throttleRead_ = false;
    std::map<int, std::pair<Metric, Metric>> limits_;  // cpuinfo min/max per cpu, refreshed every 10 generations
    std::vector<int> coreReps_, pkgReps_;              // one cpu per physical core / per package, for the throttle counters
    Metric throttle_;
    std::string driver_, governor_, epp_;
};

}  // namespace pmp::linux
