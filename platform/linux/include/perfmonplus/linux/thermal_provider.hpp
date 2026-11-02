// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#pragma once

#include <perfmonplus/core/provider.hpp>
#include <perfmonplus/linux/proc_fs.hpp>

namespace pmp::linux {

// hwmon temperatures and fans, the CPU hotspot and thermal pressure (§7.2.8).
class ThermalProvider final : public Provider {
public:
    explicit ThermalProvider(ProcRoot root) : root_(std::move(root)) {}

    std::string_view id() const override { return "thermals"; }
    void sample(Frame& out, const SampleContext& ctx) override;
    void fillUnavailable(Frame& out, Availability why) const override;

private:
    ProcRoot root_;
};

}  // namespace pmp::linux
