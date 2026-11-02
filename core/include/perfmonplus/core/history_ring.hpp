// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#pragma once

#include <perfmonplus/core/reading.hpp>

#include <cstddef>
#include <optional>
#include <stdexcept>
#include <vector>

namespace pmp {

// Fixed-capacity history. A slot without a value is an explicit gap (INT-03).
template <class T>
class HistoryRing {
public:
    struct Slot {
        MonoTime t;
        std::optional<T> value;
    };

    explicit HistoryRing(std::size_t capacity) : slots_(capacity) {
        if (capacity == 0) throw std::invalid_argument("HistoryRing capacity must be > 0");
    }

    void push(MonoTime t, T v) { put(Slot{t, std::move(v)}); }
    void pushGap(MonoTime t) { put(Slot{t, std::nullopt}); }

    void clear() noexcept {  // keeps the storage: anything holding a pointer to this ring stays valid
        start_ = 0;
        count_ = 0;
    }

    std::size_t size() const noexcept { return count_; }
    std::size_t capacity() const noexcept { return slots_.size(); }

    // 0 = oldest.
    const Slot& at(std::size_t i) const {
        if (i >= count_) throw std::out_of_range("HistoryRing index out of range");
        return slots_[(start_ + i) % slots_.size()];
    }

private:
    void put(Slot s) {
        if (count_ < slots_.size()) {
            slots_[(start_ + count_) % slots_.size()] = std::move(s);
            ++count_;
        } else {
            slots_[start_] = std::move(s);
            start_ = (start_ + 1) % slots_.size();
        }
    }

    std::vector<Slot> slots_;
    std::size_t start_ = 0;
    std::size_t count_ = 0;
};

}  // namespace pmp
