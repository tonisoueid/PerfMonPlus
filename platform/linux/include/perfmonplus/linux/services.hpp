// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#pragma once

#include <perfmonplus/linux/command.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace pmp::linux {

enum class ServiceScope { System, User };

struct ServiceInfo {
    std::string unit, description, active, sub, load, startType;
    std::int32_t mainPid = 0;
    ServiceScope scope = ServiceScope::System;
};

// systemd services via `systemctl --output=json`. Unit names are only ever passed after `--`.
class ServicesCollector {
public:
    explicit ServicesCollector(CommandRunner run = runDefault);

    std::vector<ServiceInfo> collect(ServiceScope scope) const;  // throws std::runtime_error(stderr) on failure
    // action is start | stop | restart; anything else throws std::invalid_argument before any command runs.
    CommandResult act(ServiceScope scope, const std::string& action, const std::string& unit) const;
    std::string logs(ServiceScope scope, const std::string& unit, int lines = 200) const;

private:
    CommandRunner run_;
};

}  // namespace pmp::linux
