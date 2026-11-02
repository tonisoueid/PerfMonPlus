// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#pragma once

#include <perfmonplus/linux/command.hpp>

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace pmp::linux {

struct SessionInfo {
    std::string id, user, seat, tty, sessionClass;
    std::uint32_t uid = 0;
    std::int32_t leader = 0;
    bool idle = false;
};

// Login sessions via `loginctl --json=short`. Session ids are only ever passed after `--`.
class SessionsCollector {
public:
    explicit SessionsCollector(CommandRunner run = runDefault);

    std::vector<SessionInfo> collect() const;  // throws std::runtime_error on failure
    CommandResult lock(const std::string& id) const;
    CommandResult terminate(const std::string& id) const;

private:
    CommandRunner run_;
};

}  // namespace pmp::linux
