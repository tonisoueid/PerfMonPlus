// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/vm/connections_model.hpp>

#include <catch2/catch_test_macros.hpp>

using namespace pmp;
using namespace pmp::vm;
using namespace pmp::linux;

namespace {

IpAddr v4(int a, int b, int c, int d) {
    IpAddr x;
    x.bytes = {static_cast<std::uint8_t>(a), static_cast<std::uint8_t>(b), static_cast<std::uint8_t>(c), static_cast<std::uint8_t>(d)};
    return x;
}

Connection listener(IpAddr local, std::uint16_t port, int pid = 100) {
    Connection c;
    c.proto = Proto::Tcp4;
    c.local = local;
    c.localPort = port;
    c.state = SockState::Listen;
    c.inode = 1000 + port;
    c.owner = ProcessKey{pid, 5};
    c.ownerAvailability = Availability::Measured;
    return c;
}

Connection established(std::uint16_t lport, IpAddr remote, std::uint16_t rport) {
    Connection c;
    c.proto = Proto::Tcp4;
    c.local = v4(10, 0, 0, 2);
    c.localPort = lport;
    c.remote = remote;
    c.remotePort = rport;
    c.state = SockState::Established;
    c.inode = 5000 + lport;
    c.owner = ProcessKey{200, 6};
    c.ownerAvailability = Availability::Measured;
    return c;
}

Frame procs() {
    Frame f;
    for (auto [pid, start, name] : {std::tuple{100, 5u, "sshd"}, std::tuple{200, 6u, "firefox"}}) {
        ProcessRecord r;
        r.key = {pid, start};
        r.name = name;
        f.processes.processes.push_back(r);
    }
    return f;
}

}  // namespace

TEST_CASE("rx and tx are unavailable, not zero", "[connections-vm]") {
    const auto rows = buildConnections({established(5000, v4(1, 2, 3, 4), 443)}, procs(), {}, {});
    REQUIRE(rows.size() == 1);
    REQUIRE(rows[0].rx == "Unavailable");
    REQUIRE(rows[0].tx == "Unavailable");
}

TEST_CASE("the owner column names the process or says why not", "[connections-vm]") {
    auto restricted = established(6000, v4(1, 2, 3, 4), 80);
    restricted.owner.reset();
    restricted.ownerAvailability = Availability::Restricted;
    auto none = established(6001, v4(1, 2, 3, 4), 80);
    none.owner.reset();
    auto gone = established(6002, v4(1, 2, 3, 4), 80);
    gone.owner = ProcessKey{999, 1};
    const auto rows = buildConnections({established(5000, v4(1, 2, 3, 4), 443), restricted, none, gone}, procs(), {.sortBy = "local"}, {});
    REQUIRE(rows[0].process == "firefox (PID 200)");
    REQUIRE(rows[1].process == "Restricted");
    REQUIRE(rows[2].process == "—");
    REQUIRE(rows[3].process == "PID 999");
    REQUIRE(rows[0].owner.has_value());
    REQUIRE_FALSE(rows[1].owner.has_value());
}

TEST_CASE("loopback is not exposed", "[connections-vm]") {
    const auto rows = buildConnections({listener(v4(127, 0, 0, 1), 631), listener(v4(0, 0, 0, 0), 22), listener(v4(192, 168, 1, 5), 8080)},
                                       procs(), {.listeningOnly = true}, {});
    REQUIRE(rows.size() == 3);
    REQUIRE(rows[0].local == "0.0.0.0:22");  // exposed first
    REQUIRE(rows[0].exposed);
    REQUIRE(rows[0].exposure.find("any network") != std::string::npos);
    for (std::size_t i = 1; i < 3; ++i) REQUIRE_FALSE(rows[i].exposed);
    for (const auto& r : rows) {
        if (r.local == "127.0.0.1:631") REQUIRE(r.exposure == "Only this computer can connect.");
        if (r.local == "192.168.1.5:8080") REQUIRE(r.exposure == "Reachable via 192.168.1.5 only.");
    }
}

TEST_CASE("wildcard variants are exposed", "[connections-vm]") {
    Connection mapped = listener(*parseHexAddr("0000000000000000FFFF000000000000"), 9000);
    mapped.proto = Proto::Tcp6;
    Connection any6 = listener(*parseHexAddr("00000000000000000000000000000000"), 9001);
    any6.proto = Proto::Tcp6;
    REQUIRE(isExposed(mapped));
    REQUIRE(isExposed(any6));
    REQUIRE_FALSE(isExposed(established(1, v4(0, 0, 0, 0), 1)));  // established is never exposed
}

TEST_CASE("a bound udp socket is a listener", "[connections-vm]") {
    Connection u;
    u.proto = Proto::Udp4;
    u.local = v4(0, 0, 0, 0);
    u.localPort = 5353;
    u.state = SockState::Close;
    REQUIRE(isListening(u));
    REQUIRE(isExposed(u));
    u.remote = v4(1, 2, 3, 4);
    u.remotePort = 53;  // connected: not a listener
    REQUIRE_FALSE(isListening(u));
}

TEST_CASE("listening-only lists only listeners", "[connections-vm]") {
    const auto rows = buildConnections({listener(v4(0, 0, 0, 0), 22), established(5000, v4(1, 2, 3, 4), 443)}, procs(),
                                       {.listeningOnly = true}, {});
    REQUIRE(rows.size() == 1);
    REQUIRE(rows[0].listening);
    REQUIRE(rows[0].remote == "—");
}

TEST_CASE("filters by process, port, state and protocol", "[connections-vm]") {
    const std::vector<Connection> all = {listener(v4(0, 0, 0, 0), 22), established(5000, v4(1, 2, 3, 4), 443), established(5001, v4(5, 6, 7, 8), 80)};
    REQUIRE(buildConnections(all, procs(), {.filter = "443"}, {}).size() == 1);  // a remote port
    REQUIRE(buildConnections(all, procs(), {.filter = "5001"}, {}).size() == 1); // a local port
    REQUIRE(buildConnections(all, procs(), {.filter = "SSHD"}, {}).size() == 1);
    REQUIRE(buildConnections(all, procs(), {.filter = "listen"}, {}).size() == 1);
    REQUIRE(buildConnections(all, procs(), {.filter = "firefox"}, {}).size() == 2);
    REQUIRE(buildConnections(all, procs(), {.proto = Proto::Udp4}, {}).empty());
    REQUIRE(buildConnections(all, procs(), {.state = SockState::Established}, {}).size() == 2);
    REQUIRE(buildConnections(all, procs(), {.filter = "nothing like this"}, {}).empty());
}

TEST_CASE("remote host is empty unless resolving is on", "[connections-vm]") {
    const auto remote = v4(1, 2, 3, 4);
    HostLookups hosts;
    const std::vector<Connection> rows = {established(5000, remote, 443)};
    REQUIRE(buildConnections(rows, procs(), {}, hosts)[0].host.empty());
    REQUIRE(buildConnections(rows, procs(), {.resolveHosts = true}, hosts)[0].host == "Resolving…");
    hosts.resolved[remote] = "example.org";
    REQUIRE(buildConnections(rows, procs(), {.resolveHosts = true}, hosts)[0].host == "example.org");
    hosts.resolved.clear();
    hosts.failed.insert(remote);
    REQUIRE(buildConnections(rows, procs(), {.resolveHosts = true}, hosts)[0].host == "—");
    // listeners have no remote to resolve
    REQUIRE(buildConnections({listener(v4(0, 0, 0, 0), 22)}, procs(), {.resolveHosts = true}, {})[0].host.empty());
}

TEST_CASE("sorting and stable keys", "[connections-vm]") {
    const std::vector<Connection> all = {established(5002, v4(9, 9, 9, 9), 1), established(5001, v4(1, 1, 1, 1), 2), listener(v4(0, 0, 0, 0), 22)};
    auto rows = buildConnections(all, procs(), {.sortBy = "remote"}, {});
    REQUIRE(rows[0].remote == "1.1.1.1:2");
    rows = buildConnections(all, procs(), {.sortBy = "remote", .descending = true}, {});
    REQUIRE(rows[0].remote == "—");  // "—" sorts after the digits
    const auto again = buildConnections(all, procs(), {.sortBy = "remote"}, {});
    REQUIRE(again[0].key == buildConnections(all, procs(), {.sortBy = "remote"}, {})[0].key);
    REQUIRE(rows[0].key.find("TCP|") == 0);
}

TEST_CASE("ipv6 endpoints are bracketed", "[connections-vm]") {
    Connection c = established(5000, *parseHexAddr("B80D0120000000000000000001000000"), 443);
    c.proto = Proto::Tcp6;
    c.local = *parseHexAddr("00000000000000000000000001000000");
    const auto rows = buildConnections({c}, procs(), {}, {});
    REQUIRE(rows[0].local == "[::1]:5000");
    REQUIRE(rows[0].remote == "[2001:db8::1]:443");
}
