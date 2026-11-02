// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#pragma once

#include <perfmonplus/core/provider.hpp>
#include <perfmonplus/linux/proc_fs.hpp>

#include <map>
#include <optional>

namespace pmp::linux {

class NetProvider final : public Provider {
public:
    explicit NetProvider(ProcRoot root) : root_(std::move(root)) {}

    std::string_view id() const override { return "network"; }
    void sample(Frame& out, const SampleContext& ctx) override;
    void fillUnavailable(Frame& out, Availability why) const override;
    void reset() override {
        prev_.clear();
        prevTime_.reset();
    }

private:
    struct Bytes {
        std::uint64_t rx = 0, tx = 0;
    };

    ProcRoot root_;
    std::map<std::string, Bytes, std::less<>> prev_;
    std::optional<MonoTime> prevTime_;
};

}  // namespace pmp::linux
