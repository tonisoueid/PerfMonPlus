// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#pragma once

#include <perfmonplus/core/provider.hpp>

#include <chrono>
#include <memory>
#include <string_view>
#include <vector>

namespace pmp {

struct SamplerOptions {
    int failuresBeforeQuarantine = 3;
};

// Runs every provider once per generation. A throwing provider is marked
// Failed in the frame; after enough consecutive failures it is quarantined
// and never called again (INT-10).
class Sampler {
public:
    explicit Sampler(std::vector<std::unique_ptr<Provider>> providers, SamplerOptions opts = {});

    Frame sample(MonoTime mono, std::chrono::nanoseconds boot, std::chrono::system_clock::time_point wall);
    const std::vector<ProviderHealth>& health() const noexcept { return health_; }

    // A disabled provider is never called; its section is filled Disabled. Unknown ids are ignored. Not thread-safe:
    // call it from the thread that calls sample().
    void setEnabled(std::string_view providerId, bool enabled);
    // Every provider starts its rates over (after a pause or a suspend). Same thread as sample().
    void resetAll();

private:
    std::vector<std::unique_ptr<Provider>> providers_;
    std::vector<ProviderHealth> health_;
    SamplerOptions opts_;
    std::uint64_t generation_ = 0;
};

}  // namespace pmp
