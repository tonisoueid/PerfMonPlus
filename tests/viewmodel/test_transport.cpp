// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/vm/transport.hpp>

#include <catch2/catch_test_macros.hpp>

using namespace pmp::vm;
using namespace std::chrono_literals;

TEST_CASE("advance accumulates fractional samples", "[transport]") {
    Transport t(100, 1000ms);
    t.setSpeed(0.25);
    t.play();
    int moves = 0;
    for (int i = 0; i < 8; ++i) moves += t.advance(1.0) ? 1 : 0;
    REQUIRE(t.pos() == 2);  // one sample every fourth second
    REQUIRE(moves == 2);
    t.setSpeed(2);
    REQUIRE(t.advance(1.0));
    REQUIRE(t.pos() == 4);  // two samples in one second
}

TEST_CASE("speed snaps to the allowed set", "[transport]") {
    Transport t(10, 1000ms);
    t.setSpeed(0.3);
    REQUIRE(t.speed() == 0.25);
    t.setSpeed(3);
    REQUIRE(t.speed() == 4);  // a tie goes up
    t.setSpeed(100);
    REQUIRE(t.speed() == 8);
    t.setSpeed(0);
    REQUIRE(t.speed() == 0.25);
    t.setSpeed(1);
    REQUIRE(t.speed() == 1);
}

TEST_CASE("playback stops at the end and stays paused", "[transport]") {
    Transport t(5, 1000ms);
    t.play();
    REQUIRE(t.playing());
    t.advance(2.0);
    REQUIRE(t.pos() == 2);
    t.advance(100.0);
    REQUIRE(t.pos() == 4);
    REQUIRE_FALSE(t.playing());
    REQUIRE_FALSE(t.advance(1.0));
    t.play();  // playing again from the end restarts from the start
    REQUIRE(t.pos() == 0);
    REQUIRE(t.playing());
}

TEST_CASE("nothing moves while paused or for negative time", "[transport]") {
    Transport t(10, 1000ms);
    REQUIRE_FALSE(t.advance(5.0));
    REQUIRE(t.pos() == 0);
    t.play();
    REQUIRE_FALSE(t.advance(-3.0));
    REQUIRE(t.pos() == 0);
}

TEST_CASE("jump uses the sample interval", "[transport]") {
    Transport t(100, 2000ms);
    t.seek(50);
    t.jump(10s);
    REQUIRE(t.pos() == 55);
    t.jump(-10s);
    REQUIRE(t.pos() == 50);
    t.jump(-1000s);
    REQUIRE(t.pos() == 0);
    t.jump(1000s);
    REQUIRE(t.pos() == 99);
}

TEST_CASE("step clamps at both ends and pauses", "[transport]") {
    Transport t(3, 1000ms);
    t.play();
    t.step(1);
    REQUIRE(t.pos() == 1);
    REQUIRE_FALSE(t.playing());
    t.step(10);
    REQUIRE(t.pos() == 2);
    t.step(-10);
    REQUIRE(t.pos() == 0);
}

TEST_CASE("start, end and seek", "[transport]") {
    Transport t(20, 1000ms);
    t.toEnd();
    REQUIRE(t.pos() == 19);
    t.toStart();
    REQUIRE(t.pos() == 0);
    t.seek(500);
    REQUIRE(t.pos() == 19);
}

TEST_CASE("an empty transport never moves", "[transport]") {
    Transport t(0, 1000ms);
    t.play();
    REQUIRE_FALSE(t.playing());
    t.step(3);
    t.jump(10s);
    t.toEnd();
    REQUIRE(t.pos() == 0);
    REQUIRE_FALSE(t.advance(1.0));
}

TEST_CASE("toggle flips playing", "[transport]") {
    Transport t(10, 1000ms);
    t.toggle();
    REQUIRE(t.playing());
    t.toggle();
    REQUIRE_FALSE(t.playing());
}
