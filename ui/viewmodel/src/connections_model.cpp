// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/vm/connections_model.hpp>

#include <algorithm>
#include <cctype>
#include <map>

namespace pmp::vm {
namespace {

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string endpoint(const linux::IpAddr& a, std::uint16_t port) {
    const auto addr = linux::formatAddr(a);
    return (a.v6 ? "[" + addr + "]" : addr) + ":" + std::to_string(port);
}

}  // namespace

bool isListening(const linux::Connection& c) {
    if (linux::isTcp(c.proto)) return c.state == linux::SockState::Listen;
    return c.state == linux::SockState::Close && c.remotePort == 0 && linux::isWildcard(c.remote);  // a bound UDP socket
}

bool isExposed(const linux::Connection& c) { return isListening(c) && linux::isWildcard(c.local); }

std::string exposureText(const linux::Connection& c) {
    if (!isListening(c)) return {};
    if (isExposed(c)) return "Reachable from other machines on any network this computer is on (bound to all interfaces).";
    if (linux::isLoopback(c.local)) return "Only this computer can connect.";
    return "Reachable via " + linux::formatAddr(c.local) + " only.";
}

std::string exposureExplanation() {
    return "Rows marked exposed accept connections on every network interface. A firewall may still block them: "
           "PerfMonPlus does not read firewall rules.";
}

std::vector<ConnectionRow> buildConnections(const std::vector<linux::Connection>& rows, const Frame& frame,
                                            const ConnectionOptions& opt, const HostLookups& hosts) {
    std::map<ProcessKey, const ProcessRecord*> byKey;
    for (const auto& p : frame.processes.processes) {
        if (!p.exitedAt) byKey[p.key] = &p;
    }
    std::vector<ConnectionRow> out;
    const auto needle = lower(opt.filter);
    for (const auto& c : rows) {
        if (opt.proto && c.proto != *opt.proto) continue;
        if (opt.state && c.state != *opt.state) continue;
        if (opt.listeningOnly && !isListening(c)) continue;
        ConnectionRow r;
        r.key = linux::protoText(c.proto) + "|" + endpoint(c.local, c.localPort) + "|" + endpoint(c.remote, c.remotePort) + "|" +
                std::to_string(c.inode);
        r.owner = c.owner;
        if (c.owner) {
            const auto it = byKey.find(*c.owner);
            r.process = it != byKey.end() ? it->second->name + " (PID " + std::to_string(c.owner->pid) + ")"
                                          : "PID " + std::to_string(c.owner->pid);
        } else {
            r.process = c.ownerAvailability == Availability::Restricted ? "Restricted" : "—";
        }
        r.protocol = linux::protoText(c.proto);
        r.local = endpoint(c.local, c.localPort);
        r.remote = isListening(c) ? "—" : endpoint(c.remote, c.remotePort);
        r.state = linux::stateText(c.proto, c.state);
        r.rx = r.tx = "Unavailable";  // per-connection counters need a privileged helper (D8)
        r.listening = isListening(c);
        r.exposed = isExposed(c);
        r.exposure = exposureText(c);
        if (!r.listening && !linux::isWildcard(c.remote)) r.remoteAddr = c.remote;
        if (opt.resolveHosts && r.remoteAddr) {
            if (const auto h = hosts.resolved.find(c.remote); h != hosts.resolved.end()) r.host = h->second;
            else if (hosts.failed.contains(c.remote)) r.host = "—";
            else r.host = "Resolving…";
        }
        if (!needle.empty()) {
            const auto hay = lower(r.process + " " + r.protocol + " " + r.local + " " + r.remote + " " + r.state + " " + r.host);
            if (hay.find(needle) == std::string::npos) continue;
        }
        out.push_back(std::move(r));
    }
    auto field = [&](const ConnectionRow& r) -> const std::string& {
        if (opt.sortBy == "protocol") return r.protocol;
        if (opt.sortBy == "local") return r.local;
        if (opt.sortBy == "remote") return r.remote;
        if (opt.sortBy == "state") return r.state;
        if (opt.sortBy == "host") return r.host;
        return r.process;
    };
    std::stable_sort(out.begin(), out.end(), [&](const ConnectionRow& a, const ConnectionRow& b) {
        if (opt.listeningOnly && a.exposed != b.exposed) return a.exposed;  // exposed listeners first
        const auto &x = lower(field(a)), &y = lower(field(b));
        if (x == y) return a.key < b.key;
        return opt.descending ? x > y : x < y;
    });
    return out;
}

}  // namespace pmp::vm
