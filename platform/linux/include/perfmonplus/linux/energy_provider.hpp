// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#pragma once

#include <perfmonplus/core/provider.hpp>
#include <perfmonplus/linux/proc_fs.hpp>

#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace pmp::linux {

// Power source, battery, package watts (RAPL when readable, else battery discharge) and power mode (§7.2.7).
class EnergyProvider final : public Provider {
public:
    explicit EnergyProvider(ProcRoot root) : root_(std::move(root)) {}

    std::string_view id() const override { return "energy"; }
    void sample(Frame& out, const SampleContext& ctx) override;
    void fillUnavailable(Frame& out, Availability why) const override;
    void reset() override {
        prevUj_.reset();
        prevTime_.reset();
        domainPrev_.clear();
    }

private:
    ProcRoot root_;
    std::optional<double> prevUj_;
    std::optional<MonoTime> prevTime_;

    struct DomainDir {
        std::filesystem::path dir;
        std::string id, label;
        int socket = 0;
    };
    struct DomainPrev {
        double uj = 0;
        MonoTime t{};
    };
    void readDomains(EnergySnapshot& e, const SampleContext& ctx);

    std::vector<DomainDir> domainDirs_;
    bool domainsListed_ = false;
    std::uint64_t domainsListedAt_ = 0;
    std::map<std::string, DomainPrev> domainPrev_;
};

}  // namespace pmp::linux
