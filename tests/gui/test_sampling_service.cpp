// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/gui/sampling_service.hpp>

#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QEventLoop>
#include <QThread>
#include <QTimer>

#include <atomic>
#include <memory>
#include <stdexcept>
#include <thread>

using namespace pmp;
using namespace pmp::gui;
using namespace std::chrono_literals;

namespace {

class FakeProvider final : public Provider {
public:
    FakeProvider(std::shared_ptr<std::atomic<int>> calls, bool throws = false)
        : calls_(std::move(calls)), throws_(throws) {}
    std::string_view id() const override { return "fake"; }
    void sample(Frame& out, const SampleContext&) override {
        ++*calls_;
        if (throws_) throw std::runtime_error("boom");
        out.cpu.total = Metric::measured(10.0, Unit::Percent);
    }
    void fillUnavailable(Frame& out, Availability why) const override {
        out.cpu.total = Metric::notMeasured(why, Unit::Percent);
    }

private:
    std::shared_ptr<std::atomic<int>> calls_;
    bool throws_;
};

std::vector<std::unique_ptr<Provider>> fake(std::shared_ptr<std::atomic<int>> calls, bool throws = false) {
    std::vector<std::unique_ptr<Provider>> v;
    v.push_back(std::make_unique<FakeProvider>(std::move(calls), throws));
    return v;
}

// Spin the event loop until `n` frames arrived or the timeout passes.
std::vector<FrameUpdate> collect(SamplingService& s, int n, std::chrono::milliseconds timeout, bool* onUiThread = nullptr) {
    std::vector<FrameUpdate> got;
    QEventLoop loop;
    QObject::connect(&s, &SamplingService::frameReady, &loop, [&](FrameUpdate u) {
        if (onUiThread) *onUiThread = QThread::currentThread() == QCoreApplication::instance()->thread();
        got.push_back(std::move(u));
        if (static_cast<int>(got.size()) >= n) loop.quit();
    });
    QTimer::singleShot(static_cast<int>(timeout.count()), &loop, &QEventLoop::quit);
    loop.exec();
    return got;
}

}  // namespace

TEST_CASE("emits frames on the UI thread with increasing generations", "[gui][sampling]") {
    auto calls = std::make_shared<std::atomic<int>>(0);
    SamplingService s(20ms, fake(calls));
    s.start();
    bool onUi = false;
    const auto got = collect(s, 3, 3000ms, &onUi);
    REQUIRE(got.size() == 3);
    REQUIRE(got[0].frame->generation == 1);
    REQUIRE(got[2].frame->generation == 3);
    REQUIRE_FALSE(got[0].gapBefore);
    REQUIRE(onUi);
}

TEST_CASE("a throwing provider still delivers frames", "[gui][sampling]") {
    auto calls = std::make_shared<std::atomic<int>>(0);
    SamplingService s(20ms, fake(calls, true));
    s.start();
    const auto got = collect(s, 1, 3000ms);
    REQUIRE(got.size() == 1);
    REQUIRE(got[0].frame->cpu.total.availability == Availability::Failed);
    REQUIRE(got[0].frame->providerHealth[0].state != HealthState::Healthy);
}

TEST_CASE("destructor stops sampling", "[gui][sampling]") {
    auto calls = std::make_shared<std::atomic<int>>(0);
    {
        SamplingService s(10ms, fake(calls));
        s.start();
        collect(s, 2, 3000ms);
    }
    const int after = *calls;
    std::this_thread::sleep_for(100ms);
    REQUIRE(*calls == after);
}

TEST_CASE("setInterval takes effect", "[gui][sampling]") {
    auto calls = std::make_shared<std::atomic<int>>(0);
    SamplingService s(500ms, fake(calls));
    s.start();
    REQUIRE(collect(s, 1, 3000ms).size() == 1);
    s.setInterval(20ms);
    REQUIRE(collect(s, 3, 1000ms).size() == 3);
}

TEST_CASE("a non-positive interval cannot spin the sampler", "[gui][sampling]") {
    auto calls = std::make_shared<std::atomic<int>>(0);
    SamplingService s(0ms, fake(calls));
    s.start();
    std::this_thread::sleep_for(100ms);
    REQUIRE(*calls < 200);  // clamped to a sane minimum instead of sampling flat out
}

TEST_CASE("command lines are collected only while asked for", "[gui][sampling]") {
    SamplingService svc(30ms);  // the live providers
    svc.start();
    const auto anyCmdline = [](const FrameUpdate& u) {
        for (const auto& p : u.frame->processes.processes) {
            if (!p.cmdline.empty()) return true;
        }
        return false;
    };
    for (const auto& u : collect(svc, 2, 3s)) REQUIRE_FALSE(anyCmdline(u));
    svc.setCollectCommandLines(true);
    bool seen = false;
    for (const auto& u : collect(svc, 4, 3s)) seen = seen || anyCmdline(u);
    REQUIRE(seen);
    svc.setCollectCommandLines(false);
    collect(svc, 2, 2s);  // frames already in flight
    const auto after = collect(svc, 2, 3s);
    REQUIRE_FALSE(after.empty());
    for (const auto& u : after) REQUIRE_FALSE(anyCmdline(u));
}

TEST_CASE("collecting command lines is a no-op for injected providers", "[gui][sampling]") {
    auto calls = std::make_shared<std::atomic<int>>(0);
    SamplingService svc(20ms, fake(calls));
    svc.setCollectCommandLines(true);  // must not crash
    svc.start();
    REQUIRE(collect(svc, 1, 2s).size() == 1);
}
