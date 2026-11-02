// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/linux/connections.hpp>

#include "support/fake_root.hpp"

#include <catch2/catch_test_macros.hpp>

#include <unistd.h>

using namespace pmp;
using namespace pmp::linux;
using namespace std::chrono_literals;

namespace {

const char* kHeader =
    "  sl  local_address rem_address   st tx_queue rx_queue tr tm->when retrnsmt   uid  timeout inode\n";

std::string row(int sl, const std::string& local, const std::string& remote, const std::string& st, int uid,
                unsigned long inode) {
    return "   " + std::to_string(sl) + ": " + local + " " + remote + " " + st + " 00000000:00000000 00:00000000 00000000 " +
           std::to_string(uid) + "        0 " + std::to_string(inode) + " 1 0000000000000000 100 0 0 10 0\n";
}

std::string statWithStart(int start) {
    return "1 (x) S 1 1 1 0 -1 0 0 0 0 0 1 1 0 0 20 0 1 0 " + std::to_string(start) + " 1000 10 0";
}

}  // namespace

TEST_CASE("parses ipv4 listeners", "[connections]") {
    const auto rows = parseNetTable(std::string(kHeader) + row(0, "0100007F:0016", "00000000:0000", "0A", 0, 12345), Proto::Tcp4);
    REQUIRE(rows.size() == 1);
    REQUIRE(formatAddr(rows[0].local) == "127.0.0.1");
    REQUIRE(rows[0].localPort == 22);
    REQUIRE(rows[0].state == SockState::Listen);
    REQUIRE(rows[0].inode == 12345);
    REQUIRE(isLoopback(rows[0].local));
    REQUIRE(isWildcard(rows[0].remote));
}

TEST_CASE("parses ipv6 and mapped addresses", "[connections]") {
    const auto rows = parseNetTable(std::string(kHeader) + row(0, "00000000000000000000000001000000:0050", "00000000000000000000000000000000:0000", "0A", 33, 7) +
                                        row(1, "0000000000000000FFFF00000100007F:1F90", "0000000000000000FFFF0000010000C0:0000", "01", 1000, 8),
                                    Proto::Tcp6);
    REQUIRE(rows.size() == 2);
    REQUIRE(formatAddr(rows[0].local) == "::1");
    REQUIRE(rows[0].localPort == 80);
    REQUIRE(isLoopback(rows[0].local));
    REQUIRE(formatAddr(rows[1].local) == "::ffff:127.0.0.1");
    REQUIRE(isLoopback(rows[1].local));
    REQUIRE(rows[1].localPort == 8080);
    REQUIRE(formatAddr(rows[1].remote) == "::ffff:192.0.0.1");
    REQUIRE(rows[1].uid == 1000);
}

TEST_CASE("addresses print in canonical form", "[connections]") {
    // 2001:db8::1 as the kernel writes it: four little-endian words
    const auto a = parseHexAddr("B80D0120000000000000000001000000");
    REQUIRE(a.has_value());
    REQUIRE(formatAddr(*a) == "2001:db8::1");
    const auto any = parseHexAddr("00000000000000000000000000000000");
    REQUIRE(formatAddr(*any) == "::");
    REQUIRE_FALSE(parseHexAddr("0100007").has_value());
    REQUIRE_FALSE(parseHexAddr("ZZZZZZZZ").has_value());
}

TEST_CASE("states map correctly", "[connections]") {
    std::string text = kHeader;
    for (int code = 1; code <= 12; ++code) {
        char st[8];
        std::snprintf(st, sizeof st, "%02X", code);
        text += row(code, "0100007F:0001", "0100007F:0002", st, 0, static_cast<unsigned long>(code));
    }
    const auto rows = parseNetTable(text, Proto::Tcp4);
    REQUIRE(rows.size() == 12);
    const SockState want[] = {SockState::Established, SockState::SynSent, SockState::SynRecv, SockState::FinWait1,
                              SockState::FinWait2, SockState::TimeWait, SockState::Close, SockState::CloseWait,
                              SockState::LastAck, SockState::Listen, SockState::Closing, SockState::Unknown};
    for (std::size_t i = 0; i < 12; ++i) REQUIRE(rows[i].state == want[i]);
    REQUIRE(stateText(Proto::Tcp4, SockState::Listen) == "LISTEN");
    REQUIRE(stateText(Proto::Tcp4, SockState::TimeWait) == "TIME_WAIT");
}

TEST_CASE("udp rows have no state worth showing", "[connections]") {
    REQUIRE(stateText(Proto::Udp4, SockState::Close) == "—");
    REQUIRE(stateText(Proto::Udp6, SockState::Established) == "CONNECTED");
    REQUIRE(protoText(Proto::Udp6) == "UDP6");
    REQUIRE(protoText(Proto::Tcp4) == "TCP");
}

TEST_CASE("a malformed line is skipped, the rest kept", "[connections]") {
    const auto rows = parseNetTable(std::string(kHeader) + "this is not a socket line\n" +
                                        row(1, "0100007F:0016", "00000000:0000", "0A", 0, 5) + "   2: ZZZZZZZZ:0016 00000000:0000 0A x x x 0 0 9\n",
                                    Proto::Tcp4);
    REQUIRE(rows.size() == 1);
    REQUIRE(rows[0].inode == 5);
}

TEST_CASE("wildcard and loopback classification", "[connections]") {
    struct Case {
        const char* hex;
        bool wildcard, loopback;
    };
    const Case cases[] = {
        {"00000000", true, false},                              // 0.0.0.0
        {"0100007F", false, true},                              // 127.0.0.1
        {"0201A8C0", false, false},                             // 192.168.1.2
        {"0300007F", false, true},                              // 127.0.0.3
        {"00000000000000000000000000000000", true, false},      // ::
        {"00000000000000000000000001000000", false, true},      // ::1
        {"0000000000000000FFFF000000000000", true, false},      // ::ffff:0.0.0.0
        {"0000000000000000FFFF00000100007F", false, true},      // ::ffff:127.0.0.1
        {"000080FE000000000000000001000000", false, false},     // fe80::1
        {"B80D0120000000000000000001000000", false, false},     // 2001:db8::1
    };
    for (const auto& c : cases) {
        const auto a = parseHexAddr(c.hex);
        REQUIRE(a.has_value());
        INFO(c.hex);
        REQUIRE(isWildcard(*a) == c.wildcard);
        REQUIRE(isLoopback(*a) == c.loopback);
    }
}

TEST_CASE("owner is resolved through fd links", "[connections]") {
    FakeRoot fr;
    fr.write("proc/net/tcp", std::string(kHeader) + row(0, "00000000:0050", "00000000:0000", "0A", 1000, 12345));
    fr.write("proc/100/stat", statWithStart(777));
    fr.makeDir("proc/100/fd");
    fr.symlink("socket:[12345]", "proc/100/fd/3");
    fr.symlink("/dev/null", "proc/100/fd/4");
    ConnectionsCollector c(fr.root(), 0ms);
    const auto list = c.collect();
    REQUIRE(list.rows.size() == 1);
    REQUIRE(list.rows[0].owner.has_value());
    REQUIRE(list.rows[0].owner->pid == 100);
    REQUIRE(list.rows[0].owner->startTicks == 777);
    REQUIRE(list.rows[0].ownerAvailability == Availability::Measured);
    REQUIRE_FALSE(list.anyUnreadableOwners);
}

TEST_CASE("other users' sockets are restricted, not guessed", "[connections]") {
    if (::geteuid() == 0) SKIP("root can read every fd table");
    FakeRoot fr;
    fr.write("proc/net/tcp", std::string(kHeader) + row(0, "00000000:0016", "00000000:0000", "0A", 0, 777));
    fr.write("proc/200/stat", statWithStart(1));
    fr.makeDir("proc/200/fd");
    fr.symlink("socket:[777]", "proc/200/fd/3");
    fr.makeUnreadable("proc/200/fd");
    ConnectionsCollector c(fr.root(), 0ms);
    const auto list = c.collect();
    REQUIRE(list.rows.size() == 1);
    REQUIRE_FALSE(list.rows[0].owner.has_value());
    REQUIRE(list.rows[0].ownerAvailability == Availability::Restricted);
    REQUIRE(list.anyUnreadableOwners);
}

TEST_CASE("a socket nobody we can see holds is unsupported, and kernel-owned ones too", "[connections]") {
    FakeRoot fr;
    fr.write("proc/net/tcp", std::string(kHeader) + row(0, "0100007F:0016", "00000000:0000", "0A", 1000, 999) +
                                 row(1, "0100007F:0017", "0100007F:9999", "06", 0, 0));
    fr.write("proc/100/stat", statWithStart(1));
    fr.makeDir("proc/100/fd");
    ConnectionsCollector c(fr.root(), 0ms);
    const auto list = c.collect();
    REQUIRE(list.rows.size() == 2);
    REQUIRE(list.rows[0].ownerAvailability == Availability::Unsupported);  // maybe another network namespace
    REQUIRE(list.rows[1].ownerAvailability == Availability::Unsupported);  // TIME_WAIT: inode 0
    REQUIRE_FALSE(list.rows[0].owner.has_value());
}

TEST_CASE("a missing tcp6 file is fine", "[connections]") {
    FakeRoot fr;
    fr.write("proc/net/tcp", std::string(kHeader) + row(0, "0100007F:0016", "00000000:0000", "0A", 0, 5));
    ConnectionsCollector c(fr.root(), 0ms);
    REQUIRE(c.collect().rows.size() == 1);
    FakeRoot none;
    ConnectionsCollector d(none.root(), 0ms);
    REQUIRE(d.collect().rows.empty());
}

TEST_CASE("rows beyond the cap are counted, not silently dropped", "[connections]") {
    FakeRoot fr;
    std::string text = kHeader;
    for (std::size_t i = 0; i < kMaxConnections + 25; ++i) text += row(static_cast<int>(i), "0100007F:0016", "00000000:0000", "0A", 0, i + 1);
    fr.write("proc/net/tcp", text);
    ConnectionsCollector c(fr.root(), 0ms);
    const auto list = c.collect();
    REQUIRE(list.rows.size() == kMaxConnections);
    REQUIRE(list.truncated == 25);
}

TEST_CASE("the owner map is cached between polls", "[connections]") {
    FakeRoot fr;
    fr.write("proc/net/tcp", std::string(kHeader) + row(0, "00000000:0050", "00000000:0000", "0A", 1000, 12345));
    fr.write("proc/100/stat", statWithStart(777));
    fr.makeDir("proc/100/fd");
    fr.symlink("socket:[12345]", "proc/100/fd/3");
    ConnectionsCollector c(fr.root(), 10s);
    REQUIRE(c.collect().rows[0].owner.has_value());
    fr.remove("proc/100");
    REQUIRE(c.collect().rows[0].owner.has_value());  // still the cached answer within the window
}

TEST_CASE("a new socket triggers a scan, an unchanged set does not", "[connections]") {
    FakeRoot fr;
    fr.write("proc/net/tcp", std::string(kHeader) + row(0, "00000000:0050", "00000000:0000", "0A", 1000, 12345));
    fr.write("proc/100/stat", statWithStart(777));
    fr.makeDir("proc/100/fd");
    fr.symlink("socket:[12345]", "proc/100/fd/3");
    ConnectionsCollector c(fr.root(), 10s);
    REQUIRE(c.collect().rows[0].owner.has_value());
    fr.remove("proc/100");  // the process goes away, but nothing in the socket table changed
    REQUIRE(c.collect().rows[0].owner.has_value());
    // a socket nobody holds appears: the map is rebuilt once, and now process 100 really is gone
    fr.write("proc/net/tcp", std::string(kHeader) + row(0, "00000000:0050", "00000000:0000", "0A", 1000, 12345) +
                                 row(1, "00000000:0051", "00000000:0000", "0A", 1000, 999));
    const auto after = c.collect();
    REQUIRE_FALSE(after.rows[0].owner.has_value());
    REQUIRE_FALSE(after.rows[1].owner.has_value());
    // that unattributable socket is now known, so it alone does not force another scan
    fr.makeDir("proc/101/fd");
    fr.write("proc/101/stat", statWithStart(5));
    fr.symlink("socket:[12345]", "proc/101/fd/3");
    REQUIRE_FALSE(c.collect().rows[0].owner.has_value());  // not rescanned: the cache is still fresh
}
