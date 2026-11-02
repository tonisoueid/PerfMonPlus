// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/linux/net_provider.hpp>

#include <charconv>
#include <chrono>

namespace pmp::linux {
namespace {

Metric pending(Unit u) { return Metric::notMeasured(Availability::Pending, u); }

struct DevLine {
    std::string name;
    std::uint64_t rx = 0, tx = 0;
};

std::vector<DevLine> parseNetDev(std::string_view text) {
    std::vector<DevLine> out;
    while (!text.empty()) {
        const auto nl = text.find('\n');
        std::string_view line = text.substr(0, nl);
        text = nl == std::string_view::npos ? std::string_view{} : text.substr(nl + 1);
        const auto colon = line.find(':');
        if (colon == std::string_view::npos) continue;  // header lines have no iface name
        std::string_view name = line.substr(0, colon);
        while (!name.empty() && name.front() == ' ') name.remove_prefix(1);
        std::string_view rest = line.substr(colon + 1);
        std::uint64_t f[9] = {};
        bool ok = true;
        for (int i = 0; i < 9 && ok; ++i) {
            while (!rest.empty() && rest.front() == ' ') rest.remove_prefix(1);
            const auto r = std::from_chars(rest.data(), rest.data() + rest.size(), f[i]);
            ok = r.ec == std::errc{};
            if (ok) rest.remove_prefix(static_cast<std::size_t>(r.ptr - rest.data()));
        }
        if (ok) out.push_back({std::string(name), f[0], f[8]});
    }
    return out;
}

}  // namespace

void NetProvider::sample(Frame& out, const SampleContext& ctx) {
    out.net = {};
    const auto dev = readText(root_.proc / "net/dev");
    if (!dev.ok()) {
        fillUnavailable(out, availabilityForErrno(dev.error));
        return;
    }

    const double dt = prevTime_ ? std::chrono::duration<double>(ctx.mono - *prevTime_).count() : 0.0;
    std::map<std::string, Bytes, std::less<>> now;
    bool combinedPending = dt <= 0;
    double rxSum = 0, txSum = 0, rxTotal = 0, txTotal = 0;
    int physical = 0;

    for (const auto& l : parseNetDev(dev.text)) {
        NetInterface n;
        n.name = l.name;
        const auto base = root_.sys / "class/net" / l.name;
        std::error_code existsEc;
        n.isVirtual = !std::filesystem::exists(base / "device", existsEc);
        auto state = readText(base / "operstate").text;
        while (!state.empty() && (state.back() == '\n' || state.back() == ' ')) state.pop_back();
        n.up = state == "up";
        n.totalRx = Metric::measured(static_cast<double>(l.rx), Unit::Bytes);
        n.totalTx = Metric::measured(static_cast<double>(l.tx), Unit::Bytes);

        const auto p = prev_.find(l.name);
        if (dt > 0 && p != prev_.end() && l.rx >= p->second.rx && l.tx >= p->second.tx) {
            const double rx = static_cast<double>(l.rx - p->second.rx) / dt;
            const double tx = static_cast<double>(l.tx - p->second.tx) / dt;
            n.rxBps = Metric::measured(rx, Unit::BytesPerSec);
            n.txBps = Metric::measured(tx, Unit::BytesPerSec);
            if (!n.isVirtual) {
                rxSum += rx;
                txSum += tx;
            }
        } else {
            n.rxBps = pending(Unit::BytesPerSec);
            n.txBps = pending(Unit::BytesPerSec);
            if (!n.isVirtual) combinedPending = true;
        }
        if (!n.isVirtual) {
            ++physical;
            rxTotal += static_cast<double>(l.rx);
            txTotal += static_cast<double>(l.tx);
        }
        now[l.name] = {l.rx, l.tx};
        out.net.interfaces.push_back(std::move(n));
    }

    auto& c = out.net.combined;
    c.name = "combined";
    if (physical == 0) {
        // Nothing to aggregate (container veth, loopback-only host): never report a measured zero.
        c.rxBps = c.txBps = Metric::notMeasured(Availability::Unsupported, Unit::BytesPerSec);
        c.totalRx = c.totalTx = Metric::notMeasured(Availability::Unsupported, Unit::Bytes);
    } else {
        c.totalRx = Metric::measured(rxTotal, Unit::Bytes);
        c.totalTx = Metric::measured(txTotal, Unit::Bytes);
    }
    if (physical == 0) {
        // rates already Unsupported above
    } else if (combinedPending) {
        c.rxBps = c.txBps = pending(Unit::BytesPerSec);
    } else {
        c.rxBps = Metric::measured(rxSum, Unit::BytesPerSec);
        c.txBps = Metric::measured(txSum, Unit::BytesPerSec);
    }

    prev_ = std::move(now);
    prevTime_ = ctx.mono;
}

void NetProvider::fillUnavailable(Frame& out, Availability why) const {
    out.net = {};
    auto& c = out.net.combined;
    c.name = "combined";
    c.rxBps = c.txBps = Metric::notMeasured(why, Unit::BytesPerSec);
    c.totalRx = c.totalTx = Metric::notMeasured(why, Unit::Bytes);
}

}  // namespace pmp::linux
