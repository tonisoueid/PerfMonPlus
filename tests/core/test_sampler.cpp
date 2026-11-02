// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/core/format.hpp>
#include <perfmonplus/core/sampler.hpp>

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <set>
#include <stdexcept>

using namespace pmp;
using namespace std::chrono_literals;

namespace {

struct Counters {
    int calls = 0;
    int resets = 0;
};

class FakeProvider final : public Provider {
public:
    FakeProvider(std::string id, std::shared_ptr<Counters> c, std::set<int> throwOn = {},
                 bool throwNonStd = false)
        : id_(std::move(id)), counters_(std::move(c)), throwOn_(std::move(throwOn)), nonStd_(throwNonStd) {}

    std::string_view id() const override { return id_; }

    void sample(Frame& out, const SampleContext&) override {
        ++counters_->calls;
        if (throwOn_.contains(counters_->calls)) {
            if (nonStd_) throw 42;
            throw std::runtime_error("boom");
        }
        out.cpu.total = Metric::measured(10.0, Unit::Percent);
    }

    void fillUnavailable(Frame& out, Availability why) const override {
        out.cpu.total = Metric::notMeasured(why, Unit::Percent);
    }

    void reset() override { ++counters_->resets; }

private:
    std::string id_;
    std::shared_ptr<Counters> counters_;
    std::set<int> throwOn_;
    bool nonStd_;
};

MonoTime t(int ms) { return MonoTime{} + std::chrono::milliseconds(ms); }

Sampler makeSampler(std::vector<std::unique_ptr<Provider>> ps) { return Sampler(std::move(ps)); }

std::vector<std::unique_ptr<Provider>> one(std::unique_ptr<Provider> p) {
    std::vector<std::unique_ptr<Provider>> v;
    v.push_back(std::move(p));
    return v;
}

}  // namespace

TEST_CASE("generations increment from 1", "[sampler]") {
    auto c = std::make_shared<Counters>();
    auto s = makeSampler(one(std::make_unique<FakeProvider>("a", c)));
    REQUIRE(s.sample(t(0), 0ns, {}).generation == 1);
    REQUIRE(s.sample(t(1000), 1s, {}).generation == 2);
    REQUIRE(s.sample(t(2000), 2s, {}).generation == 3);
}

TEST_CASE("throwing provider is degraded then quarantined", "[sampler]") {
    auto c = std::make_shared<Counters>();
    auto s = makeSampler(one(std::make_unique<FakeProvider>("a", c, std::set<int>{1, 2, 3, 4})));

    auto f1 = s.sample(t(0), 0ns, {});
    REQUIRE(s.health()[0].state == HealthState::Degraded);
    REQUIRE(s.health()[0].consecutiveFailures == 1);
    REQUIRE(s.health()[0].lastError == "boom");
    REQUIRE(f1.cpu.total.availability == Availability::Failed);

    s.sample(t(1000), 1s, {});
    s.sample(t(2000), 2s, {});
    REQUIRE(s.health()[0].state == HealthState::Failed);

    auto f4 = s.sample(t(3000), 3s, {});
    REQUIRE(c->calls == 3);
    REQUIRE(f4.cpu.total.availability == Availability::Failed);
}

TEST_CASE("other providers keep sampling when one fails", "[sampler]") {
    auto bad = std::make_shared<Counters>();
    auto good = std::make_shared<Counters>();
    std::vector<std::unique_ptr<Provider>> ps;
    ps.push_back(std::make_unique<FakeProvider>("bad", bad, std::set<int>{1, 2, 3, 4}));
    ps.push_back(std::make_unique<FakeProvider>("good", good));
    auto s = makeSampler(std::move(ps));
    for (int i = 0; i < 4; ++i) s.sample(t(i * 1000), std::chrono::seconds(i), {});
    REQUIRE(good->calls == 4);
    REQUIRE(bad->calls == 3);
}

TEST_CASE("recovery resets health", "[sampler]") {
    auto c = std::make_shared<Counters>();
    auto s = makeSampler(one(std::make_unique<FakeProvider>("a", c, std::set<int>{1})));
    s.sample(t(0), 0ns, {});
    s.sample(t(1000), 1s, {});
    REQUIRE(s.health()[0].state == HealthState::Healthy);
    REQUIRE(s.health()[0].consecutiveFailures == 0);
}

TEST_CASE("an error from before the provider recovered is not shown as current", "[sampler][minor]") {
    auto c = std::make_shared<Counters>();
    auto s = makeSampler(one(std::make_unique<FakeProvider>("a", c, std::set<int>{1})));
    s.sample(t(0), 0ns, {});
    REQUIRE(s.health()[0].lastError == "boom");
    s.sample(t(1000), 1s, {});
    REQUIRE(s.health()[0].lastError.empty());
}

TEST_CASE("non-std exceptions are caught", "[sampler]") {
    auto c = std::make_shared<Counters>();
    auto s = makeSampler(one(std::make_unique<FakeProvider>("a", c, std::set<int>{1}, true)));
    s.sample(t(0), 0ns, {});
    REQUIRE(s.health()[0].lastError == "unknown error");
}

TEST_CASE("frame carries health and timestamps", "[sampler]") {
    auto c1 = std::make_shared<Counters>();
    auto c2 = std::make_shared<Counters>();
    std::vector<std::unique_ptr<Provider>> ps;
    ps.push_back(std::make_unique<FakeProvider>("a", c1));
    ps.push_back(std::make_unique<FakeProvider>("b", c2));
    auto s = makeSampler(std::move(ps));
    const auto wall = std::chrono::system_clock::time_point{} + 5s;
    auto f = s.sample(t(123), 456ms, wall);
    REQUIRE(f.providerHealth.size() == 2);
    REQUIRE(f.providerHealth[0].id == "a");
    REQUIRE(f.mono == t(123));
    REQUIRE(f.boot == 456ms);
    REQUIRE(f.wall == wall);
}

TEST_CASE("disabled provider is skipped and labelled", "[sampler]") {
    auto c = std::make_shared<Counters>();
    auto s = makeSampler(one(std::make_unique<FakeProvider>("a", c)));
    s.sample(t(0), 0ns, {});
    REQUIRE(c->calls == 1);

    s.setEnabled("a", false);
    const auto f = s.sample(t(1000), 1s, {});
    REQUIRE(c->calls == 1);
    REQUIRE(s.health()[0].state == HealthState::Disabled);
    REQUIRE(f.providerHealth[0].state == HealthState::Disabled);
    REQUIRE(f.cpu.total.availability == Availability::Disabled);
    REQUIRE(format(f.cpu.total) == "Disabled");

    s.setEnabled("a", true);
    s.sample(t(2000), 2s, {});
    REQUIRE(c->calls == 2);
    REQUIRE(s.health()[0].state == HealthState::Healthy);
}

TEST_CASE("enabling an unknown provider id is ignored", "[sampler]") {
    auto c = std::make_shared<Counters>();
    auto s = makeSampler(one(std::make_unique<FakeProvider>("a", c)));
    s.setEnabled("nope", false);
    s.sample(t(0), 0ns, {});
    REQUIRE(c->calls == 1);
}

TEST_CASE("a re-enabled provider starts its rates over", "[sampler][minor]") {
    auto c = std::make_shared<Counters>();
    auto s = makeSampler(one(std::make_unique<FakeProvider>("a", c)));
    s.sample(t(0), 0ns, std::chrono::system_clock::now());
    s.setEnabled("a", true);  // already enabled: nothing to start over
    REQUIRE(c->resets == 0);
    s.setEnabled("a", false);
    s.sample(t(1000), 0ns, std::chrono::system_clock::now());
    s.setEnabled("a", true);  // the first rate after a pause would span the whole pause: it must be Pending instead
    REQUIRE(c->resets == 1);
}

TEST_CASE("resetAll restarts every provider's rates", "[sampler][minor]") {
    auto a = std::make_shared<Counters>(), b = std::make_shared<Counters>();
    std::vector<std::unique_ptr<Provider>> ps;
    ps.push_back(std::make_unique<FakeProvider>("a", a));
    ps.push_back(std::make_unique<FakeProvider>("b", b));
    auto s = makeSampler(std::move(ps));
    s.resetAll();
    REQUIRE(a->resets == 1);
    REQUIRE(b->resets == 1);
}
