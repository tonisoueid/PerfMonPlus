// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#pragma once

#include <perfmonplus/core/provider.hpp>
#include <perfmonplus/linux/proc_fs.hpp>

namespace pmp::linux {

class MemoryProvider final : public Provider {
public:
    explicit MemoryProvider(ProcRoot root) : root_(std::move(root)) {}

    std::string_view id() const override { return "memory"; }
    void sample(Frame& out, const SampleContext& ctx) override;
    void fillUnavailable(Frame& out, Availability why) const override;

private:
    ProcRoot root_;
};

}  // namespace pmp::linux
