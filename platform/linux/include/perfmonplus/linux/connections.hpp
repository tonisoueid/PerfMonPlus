// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#pragma once

#include <perfmonplus/core/frame.hpp>
#include <perfmonplus/linux/proc_fs.hpp>

#include <array>
#include <chrono>
#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace pmp::linux {

enum class Proto { Tcp4, Tcp6, Udp4, Udp6 };
enum class SockState {
    Established, SynSent, SynRecv, FinWait1, FinWait2, TimeWait, Close, CloseWait, LastAck, Listen, Closing, Unknown
};

// An address as 16 bytes; IPv4 uses the first four and has v6 == false.
struct IpAddr {
    std::array<std::uint8_t, 16> bytes{};
    bool v6 = false;
    friend auto operator<=>(const IpAddr&, const IpAddr&) = default;
};

struct Connection {
    Proto proto = Proto::Tcp4;
    IpAddr local, remote;
    std::uint16_t localPort = 0, remotePort = 0;
    SockState state = SockState::Unknown;
    std::uint64_t inode = 0;
    std::uint32_t uid = 0;
    std::optional<ProcessKey> owner;            // nullopt: the owning process is not known (never guessed)
    Availability ownerAvailability = Availability::Unsupported;  // Measured | Restricted | Unsupported
};

std::string formatAddr(const IpAddr& a);       // "127.0.0.1", "::1", "::ffff:192.0.2.1" (IPv4-mapped), "2001:db8::1"
bool isWildcard(const IpAddr& a);              // 0.0.0.0, ::, ::ffff:0.0.0.0
bool isLoopback(const IpAddr& a);              // 127.0.0.0/8, ::1, ::ffff:127.0.0.0/104
bool isTcp(Proto p);
bool isIpv6(Proto p);
std::string protoText(Proto p);                // "TCP", "TCP6", "UDP", "UDP6"
std::string stateText(Proto p, SockState s);   // "LISTEN", "ESTABLISHED"…; "—" for an unconnected UDP socket

struct ConnectionList {
    std::vector<Connection> rows;
    int truncated = 0;                          // rows beyond kMaxConnections that were not kept
    bool anyUnreadableOwners = false;           // some process's fd table could not be read
};
inline constexpr std::size_t kMaxConnections = 20'000;

// Reads /proc/net/{tcp,tcp6,udp,udp6} and finds each socket's process through /proc/[pid]/fd. Never throws; an
// unreadable file means no rows from it. The inode -> process map costs a directory scan per process, so it is kept
// and rebuilt only when a socket appears that it has never seen, or when it is `ownerCacheFor` old.
class ConnectionsCollector {
public:
    explicit ConnectionsCollector(ProcRoot root, std::chrono::milliseconds ownerCacheFor = std::chrono::milliseconds(6000))
        : root_(std::move(root)), cacheFor_(ownerCacheFor) {}
    ConnectionList collect();

private:
    struct Owner {
        ProcessKey key;
    };
    void refreshOwners(const std::vector<Connection>& rows);

    ProcRoot root_;
    std::chrono::milliseconds cacheFor_;
    std::chrono::steady_clock::time_point ownersAt_{};
    bool ownersValid_ = false;
    std::vector<std::pair<std::uint64_t, ProcessKey>> owners_;  // sorted by inode
    std::set<std::uint64_t> unattributed_;  // sockets the last scan could not attribute: not a reason to scan again
    bool unreadable_ = false;
};

// Exposed for tests.
std::optional<IpAddr> parseHexAddr(std::string_view hex);  // 8 hex digits (IPv4) or 32 (IPv6), kernel byte order
std::vector<Connection> parseNetTable(std::string_view text, Proto proto);

}  // namespace pmp::linux
