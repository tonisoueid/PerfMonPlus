// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/linux/identity.hpp>
#include <perfmonplus/linux/process_provider.hpp>

namespace pmp::linux {

IdentityCheck verifyIdentity(const ProcRoot& root, ProcessKey key) {
    const auto stat = readText(root.proc / std::to_string(key.pid) / "stat");
    if (!stat.ok()) return IdentityCheck::Exited;
    const auto parsed = parseProcStat(stat.text);
    if (!parsed) return IdentityCheck::Exited;
    return parsed->startTicks == key.startTicks ? IdentityCheck::Alive : IdentityCheck::Reused;
}

}  // namespace pmp::linux
