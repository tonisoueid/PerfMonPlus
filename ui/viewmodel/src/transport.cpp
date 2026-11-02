// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/vm/transport.hpp>

#include <algorithm>
#include <cmath>

namespace pmp::vm {

Transport::Transport(std::size_t count, std::chrono::milliseconds interval)
    : count_(count), interval_(interval.count() > 0 ? interval : std::chrono::milliseconds(1000)) {}

const std::array<double, 6>& Transport::speeds() {
    static const std::array<double, 6> s{0.25, 0.5, 1, 2, 4, 8};
    return s;
}

void Transport::play() {
    if (count_ == 0) return;
    if (pos_ + 1 >= count_) pos_ = 0;
    carry_ = 0;
    playing_ = true;
}

void Transport::seek(std::size_t index) {
    pos_ = count_ ? std::min(index, count_ - 1) : 0;
    carry_ = 0;
}

void Transport::step(int samples) {
    playing_ = false;
    if (count_ == 0) return;
    const auto target = static_cast<long long>(pos_) + samples;
    seek(static_cast<std::size_t>(std::clamp<long long>(target, 0, static_cast<long long>(count_) - 1)));
}

void Transport::jump(std::chrono::seconds s) {
    if (count_ == 0) return;
    const auto samples = std::llround(static_cast<double>(s.count()) * 1000.0 / static_cast<double>(interval_.count()));
    const auto target = static_cast<long long>(pos_) + samples;
    seek(static_cast<std::size_t>(std::clamp<long long>(target, 0, static_cast<long long>(count_) - 1)));
}

void Transport::setSpeed(double s) {
    double best = speeds()[0], bestDist = std::abs(s - best);
    for (const double c : speeds()) {
        const double d = std::abs(s - c);
        if (d <= bestDist) {  // "<=": a tie goes to the faster speed
            best = c;
            bestDist = d;
        }
    }
    speed_ = best;
}

bool Transport::advance(double dtSeconds) {
    if (!playing_ || count_ == 0 || dtSeconds <= 0) return false;
    carry_ += dtSeconds * speed_ * 1000.0 / static_cast<double>(interval_.count());
    const auto whole = static_cast<std::size_t>(carry_);
    if (whole == 0) return false;
    carry_ -= static_cast<double>(whole);
    const auto before = pos_;
    pos_ = std::min(pos_ + whole, count_ - 1);
    if (pos_ + 1 >= count_) {
        playing_ = false;
        carry_ = 0;
    }
    return pos_ != before;
}

}  // namespace pmp::vm
