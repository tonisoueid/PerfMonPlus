// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#pragma once

#include <perfmonplus/core/frame.hpp>

#include <cstdint>
#include <string_view>

namespace pmp {

struct SampleContext {
    std::uint64_t generation;
    MonoTime mono;
};

class Provider {
public:
    virtual ~Provider() = default;
    virtual std::string_view id() const = 0;
    virtual void sample(Frame& out, const SampleContext& ctx) = 0;          // may throw
    virtual void fillUnavailable(Frame& out, Availability why) const = 0;  // mark its section
    // Forget the previous sample: after a pause, a suspend or a switch-off, the next rate would span the whole
    // discontinuity, so it reads Pending instead (INT-02). Providers without rates need not override this.
    virtual void reset() {}
};

}  // namespace pmp
