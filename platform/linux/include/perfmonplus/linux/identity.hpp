// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#pragma once

#include <perfmonplus/core/frame.hpp>
#include <perfmonplus/linux/proc_fs.hpp>

namespace pmp::linux {

enum class IdentityCheck { Alive, Exited, Reused };

// Re-checks (pid, start time) against /proc immediately before an action
// (INT-05). Anything other than Alive means: take no action. A process that
// cannot be inspected is reported as Exited, so the caller fails safe.
IdentityCheck verifyIdentity(const ProcRoot& root, ProcessKey key);

}  // namespace pmp::linux
