// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/core/sampler.hpp>

#include <exception>

namespace pmp {

Sampler::Sampler(std::vector<std::unique_ptr<Provider>> providers, SamplerOptions opts)
    : providers_(std::move(providers)), opts_(opts) {
    health_.reserve(providers_.size());
    for (const auto& p : providers_) {
        ProviderHealth h;
        h.id = std::string(p->id());
        health_.push_back(std::move(h));
    }
}

void Sampler::setEnabled(std::string_view providerId, bool enabled) {
    for (std::size_t i = 0; i < health_.size(); ++i) {
        auto& h = health_[i];
        if (h.id != providerId) continue;
        if (!enabled) {
            h.state = HealthState::Disabled;
        } else if (h.state == HealthState::Disabled) {
            h.state = HealthState::Healthy;
            h.consecutiveFailures = 0;  // a re-enabled provider starts over, even a quarantined one
            providers_[i]->reset();     // and its first rate must not span the time it was off
        }
        return;
    }
}

void Sampler::resetAll() {
    for (auto& p : providers_) p->reset();
}

Frame Sampler::sample(MonoTime mono, std::chrono::nanoseconds boot,
                      std::chrono::system_clock::time_point wall) {
    Frame frame;
    frame.generation = ++generation_;
    frame.mono = mono;
    frame.boot = boot;
    frame.wall = wall;
    const SampleContext ctx{frame.generation, mono};

    for (std::size_t i = 0; i < providers_.size(); ++i) {
        auto& p = *providers_[i];
        auto& h = health_[i];
        if (h.state == HealthState::Disabled) {
            p.fillUnavailable(frame, Availability::Disabled);
            continue;
        }
        if (h.state == HealthState::Failed) {
            p.fillUnavailable(frame, Availability::Failed);
            continue;
        }
        const auto started = std::chrono::steady_clock::now();
        try {
            p.sample(frame, ctx);
            h.consecutiveFailures = 0;
            h.lastError.clear();
            h.state = HealthState::Healthy;
        } catch (const std::exception& e) {
            h.lastError = e.what();
            ++h.consecutiveFailures;
            p.fillUnavailable(frame, Availability::Failed);
        } catch (...) {
            h.lastError = "unknown error";
            ++h.consecutiveFailures;
            p.fillUnavailable(frame, Availability::Failed);
        }
        if (h.consecutiveFailures > 0) {
            h.state = h.consecutiveFailures >= opts_.failuresBeforeQuarantine ? HealthState::Failed
                                                                              : HealthState::Degraded;
        }
        const double cost =
            std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - started).count();
        h.avgSampleCostMicros = frame.generation == 1 ? cost : 0.8 * h.avgSampleCostMicros + 0.2 * cost;
    }
    frame.providerHealth = health_;
    return frame;
}

}  // namespace pmp
