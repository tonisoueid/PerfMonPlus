// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#pragma once

#include <perfmonplus/core/provider.hpp>
#include <perfmonplus/linux/drm.hpp>

#include <map>
#include <optional>
#include <set>
#include <tuple>

namespace pmp::linux {

// GPU devices from /sys/class/drm plus engine utilisation and per-process GPU from DRM fdinfo (D8).
// Must run after the process provider: it fills ProcessRecord::gpu on the live records.
class GpuProvider final : public Provider {
public:
    explicit GpuProvider(ProcRoot root, std::filesystem::path pciIds = {}, int slices = 10);

    std::string_view id() const override { return "gpu"; }
    void sample(Frame& out, const SampleContext& ctx) override;
    void fillUnavailable(Frame& out, Availability why) const override;
    void reset() override {
        prevNs_.clear();
        prevPids_.clear();
        prevTime_.reset();
    }

private:
    ProcRoot root_;
    std::filesystem::path pciIds_;
    DrmClientScanner scanner_;
    std::map<std::tuple<std::string, std::uint64_t, std::string>, std::uint64_t> prevNs_;
    std::set<std::string> enginePdevs_;  // devices seen with engine counters at least once
    std::set<std::int32_t> prevPids_;
    std::optional<MonoTime> prevTime_;
};

}  // namespace pmp::linux
