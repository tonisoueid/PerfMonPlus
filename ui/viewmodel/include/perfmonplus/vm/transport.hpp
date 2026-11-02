// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#pragma once

#include <array>
#include <chrono>
#include <cstddef>

namespace pmp::vm {

// The play/pause/scrub state of a replay (FLR-06), by sample index. Pure state: the window calls advance() from a
// timer while playing. A gap in the recording takes no time to cross; the playhead moves sample by sample.
class Transport {
public:
    Transport(std::size_t count, std::chrono::milliseconds interval);

    std::size_t count() const { return count_; }
    std::size_t pos() const { return pos_; }
    bool playing() const { return playing_; }
    double speed() const { return speed_; }
    std::chrono::milliseconds interval() const { return interval_; }

    void play();  // from the end it restarts from the start; a no-op on an empty recording
    void pause() { playing_ = false; }
    void toggle() { playing_ ? pause() : play(); }
    void step(int samples);              // moves and pauses
    void jump(std::chrono::seconds s);   // by whole samples of the recording's interval; keeps the play state
    void toStart() { seek(0); }
    void toEnd() { seek(count_ ? count_ - 1 : 0); }
    void seek(std::size_t index);
    void setSpeed(double s);             // snaps to the nearest of speeds(); a tie goes to the faster one
    // `dtSeconds` of real time passed. Returns true when the playhead moved; reaching the end pauses.
    bool advance(double dtSeconds);

    static const std::array<double, 6>& speeds();

private:
    std::size_t count_;
    std::chrono::milliseconds interval_;
    std::size_t pos_ = 0;
    bool playing_ = false;
    double speed_ = 1.0;
    double carry_ = 0.0;  // fractions of a sample not yet consumed
};

}  // namespace pmp::vm
