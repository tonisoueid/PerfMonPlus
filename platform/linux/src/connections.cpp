// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/linux/connections.hpp>

#include <perfmonplus/linux/process_provider.hpp>

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <filesystem>
#include <sstream>

#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>

namespace pmp::linux {
namespace {

namespace fs = std::filesystem;

int hexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

std::optional<std::uint64_t> parseHex(std::string_view s) {
    std::uint64_t v = 0;
    if (s.empty() || s.size() > 16) return std::nullopt;
    for (const char c : s) {
        const int d = hexDigit(c);
        if (d < 0) return std::nullopt;
        v = v << 4 | static_cast<std::uint64_t>(d);
    }
    return v;
}

SockState stateFromCode(unsigned code) {
    switch (code) {
        case 0x01: return SockState::Established;
        case 0x02: return SockState::SynSent;
        case 0x03: return SockState::SynRecv;
        case 0x04: return SockState::FinWait1;
        case 0x05: return SockState::FinWait2;
        case 0x06: return SockState::TimeWait;
        case 0x07: return SockState::Close;
        case 0x08: return SockState::CloseWait;
        case 0x09: return SockState::LastAck;
        case 0x0A: return SockState::Listen;
        case 0x0B: return SockState::Closing;
        default: return SockState::Unknown;
    }
}

bool isV4Mapped(const IpAddr& a) {
    if (!a.v6) return false;
    for (int i = 0; i < 10; ++i) {
        if (a.bytes[static_cast<std::size_t>(i)] != 0) return false;
    }
    return a.bytes[10] == 0xFF && a.bytes[11] == 0xFF;
}

}  // namespace

std::optional<IpAddr> parseHexAddr(std::string_view hex) {
    IpAddr a;
    if (hex.size() == 8) {
        const auto w = parseHex(hex);
        if (!w) return std::nullopt;
        for (std::size_t i = 0; i < 4; ++i) a.bytes[i] = static_cast<std::uint8_t>(*w >> (8 * i));  // little-endian word
        return a;
    }
    if (hex.size() == 32) {
        a.v6 = true;
        for (std::size_t word = 0; word < 4; ++word) {
            const auto w = parseHex(hex.substr(word * 8, 8));
            if (!w) return std::nullopt;
            for (std::size_t i = 0; i < 4; ++i) a.bytes[word * 4 + i] = static_cast<std::uint8_t>(*w >> (8 * i));
        }
        return a;
    }
    return std::nullopt;
}

std::vector<Connection> parseNetTable(std::string_view text, Proto proto) {
    std::vector<Connection> out;
    std::size_t pos = 0;
    bool first = true;
    while (pos < text.size()) {
        auto end = text.find('\n', pos);
        if (end == std::string_view::npos) end = text.size();
        const auto line = text.substr(pos, end - pos);
        pos = end + 1;
        if (first) {  // the header line
            first = false;
            continue;
        }
        std::vector<std::string_view> f;
        std::size_t i = 0;
        while (i < line.size()) {
            while (i < line.size() && line[i] == ' ') ++i;
            const auto start = i;
            while (i < line.size() && line[i] != ' ') ++i;
            if (i > start) f.push_back(line.substr(start, i - start));
        }
        if (f.size() < 10) continue;  // a malformed line is skipped, the rest kept
        const auto colon = [](std::string_view s) { return s.find(':'); };
        const auto lc = colon(f[1]), rc = colon(f[2]);
        if (lc == std::string_view::npos || rc == std::string_view::npos) continue;
        const auto la = parseHexAddr(f[1].substr(0, lc)), ra = parseHexAddr(f[2].substr(0, rc));
        const auto lp = parseHex(f[1].substr(lc + 1)), rp = parseHex(f[2].substr(rc + 1));
        const auto st = parseHex(f[3]);
        const auto inode = [&]() -> std::optional<std::uint64_t> {
            std::uint64_t v = 0;
            const auto r = std::from_chars(f[9].data(), f[9].data() + f[9].size(), v);
            return r.ec == std::errc{} ? std::optional(v) : std::nullopt;
        }();
        std::uint64_t uid = 0;
        std::from_chars(f[7].data(), f[7].data() + f[7].size(), uid);
        if (!la || !ra || !lp || !rp || !st || !inode || *lp > 65535 || *rp > 65535) continue;
        Connection c;
        c.proto = proto;
        c.local = *la;
        c.remote = *ra;
        c.localPort = static_cast<std::uint16_t>(*lp);
        c.remotePort = static_cast<std::uint16_t>(*rp);
        c.state = stateFromCode(static_cast<unsigned>(*st));
        c.inode = *inode;
        c.uid = static_cast<std::uint32_t>(uid);
        out.push_back(c);
    }
    return out;
}

std::string formatAddr(const IpAddr& a) {
    char buf[64];
    if (!a.v6) {
        std::snprintf(buf, sizeof buf, "%u.%u.%u.%u", a.bytes[0], a.bytes[1], a.bytes[2], a.bytes[3]);
        return buf;
    }
    if (isV4Mapped(a)) {
        std::snprintf(buf, sizeof buf, "::ffff:%u.%u.%u.%u", a.bytes[12], a.bytes[13], a.bytes[14], a.bytes[15]);
        return buf;
    }
    std::uint16_t g[8];
    for (std::size_t i = 0; i < 8; ++i) g[i] = static_cast<std::uint16_t>(a.bytes[2 * i] << 8 | a.bytes[2 * i + 1]);
    // RFC 5952: compress the longest run (>= 2) of zero groups.
    int bestStart = -1, bestLen = 0;
    for (int i = 0; i < 8;) {
        if (g[i] != 0) {
            ++i;
            continue;
        }
        int j = i;
        while (j < 8 && g[j] == 0) ++j;
        if (j - i > bestLen) {
            bestStart = i;
            bestLen = j - i;
        }
        i = j;
    }
    if (bestLen < 2) bestStart = -1;
    std::string s;
    for (int i = 0; i < 8; ++i) {
        if (i == bestStart) {
            s += i == 0 ? "::" : ":";
            i += bestLen - 1;
            continue;
        }
        char grp[8];
        std::snprintf(grp, sizeof grp, "%x", g[i]);
        s += grp;
        if (i != 7) s += ':';
    }
    return s;
}

bool isWildcard(const IpAddr& a) {
    if (!a.v6) return a.bytes[0] == 0 && a.bytes[1] == 0 && a.bytes[2] == 0 && a.bytes[3] == 0;
    const auto zeroTail = [&](std::size_t from) {
        for (std::size_t i = from; i < 16; ++i) {
            if (a.bytes[i] != 0) return false;
        }
        return true;
    };
    return zeroTail(0) || (isV4Mapped(a) && a.bytes[12] == 0 && a.bytes[13] == 0 && a.bytes[14] == 0 && a.bytes[15] == 0);
}

bool isLoopback(const IpAddr& a) {
    if (!a.v6) return a.bytes[0] == 127;
    if (isV4Mapped(a)) return a.bytes[12] == 127;
    for (std::size_t i = 0; i < 15; ++i) {
        if (a.bytes[i] != 0) return false;
    }
    return a.bytes[15] == 1;
}

bool isTcp(Proto p) { return p == Proto::Tcp4 || p == Proto::Tcp6; }
bool isIpv6(Proto p) { return p == Proto::Tcp6 || p == Proto::Udp6; }

std::string protoText(Proto p) {
    switch (p) {
        case Proto::Tcp4: return "TCP";
        case Proto::Tcp6: return "TCP6";
        case Proto::Udp4: return "UDP";
        case Proto::Udp6: return "UDP6";
    }
    return "?";
}

std::string stateText(Proto p, SockState s) {
    if (!isTcp(p)) {
        if (s == SockState::Close) return "—";  // an unconnected UDP socket has no state worth showing
        if (s == SockState::Established) return "CONNECTED";
    }
    switch (s) {
        case SockState::Established: return "ESTABLISHED";
        case SockState::SynSent: return "SYN_SENT";
        case SockState::SynRecv: return "SYN_RECV";
        case SockState::FinWait1: return "FIN_WAIT1";
        case SockState::FinWait2: return "FIN_WAIT2";
        case SockState::TimeWait: return "TIME_WAIT";
        case SockState::Close: return "CLOSE";
        case SockState::CloseWait: return "CLOSE_WAIT";
        case SockState::LastAck: return "LAST_ACK";
        case SockState::Listen: return "LISTEN";
        case SockState::Closing: return "CLOSING";
        case SockState::Unknown: return "UNKNOWN";
    }
    return "UNKNOWN";
}

void ConnectionsCollector::refreshOwners(const std::vector<Connection>& rows) {
    const auto now = std::chrono::steady_clock::now();
    if (ownersValid_ && now - ownersAt_ < cacheFor_) {
        // Still fresh: scan again only for a socket the map has never heard of (a new connection).
        bool unseen = false;
        for (const auto& c : rows) {
            if (c.inode == 0 || unattributed_.contains(c.inode)) continue;
            const auto it = std::lower_bound(owners_.begin(), owners_.end(), c.inode, [](const auto& p, std::uint64_t inode) { return p.first < inode; });
            if (it == owners_.end() || it->first != c.inode) {
                unseen = true;
                break;
            }
        }
        if (!unseen) return;
    }
    owners_.clear();
    unattributed_.clear();
    unreadable_ = false;
    // Raw directory calls: a busy desktop has thousands of descriptors, and this runs every poll.
    DIR* procDir = ::opendir(root_.proc.c_str());
    if (procDir) {
        const int procFd = ::dirfd(procDir);
        while (const dirent* e = ::readdir(procDir)) {
            const char* name = e->d_name;
            if (name[0] < '0' || name[0] > '9') continue;
            int pid = 0;
            const auto len = std::strlen(name);
            if (std::from_chars(name, name + len, pid).ec != std::errc{}) continue;
            const std::string fdPath = std::string(name) + "/fd";
            const int fdDirFd = ::openat(procFd, fdPath.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
            if (fdDirFd < 0) {  // another user's process (or a protected one): we cannot see which sockets it holds
                if (errno == EACCES || errno == EPERM) unreadable_ = true;
                continue;
            }
            DIR* fdDir = ::fdopendir(fdDirFd);
            if (!fdDir) {
                ::close(fdDirFd);
                continue;
            }
            std::optional<ProcessKey> key;
            while (const dirent* f = ::readdir(fdDir)) {
                if (f->d_name[0] == '.') continue;
                char target[64];
                const auto n = ::readlinkat(::dirfd(fdDir), f->d_name, target, sizeof target - 1);
                if (n < 9) continue;  // "socket:[1]" is the shortest
                target[n] = 0;
                if (std::strncmp(target, "socket:[", 8) != 0 || target[n - 1] != ']') continue;
                std::uint64_t inode = 0;
                if (std::from_chars(target + 8, target + n - 1, inode).ec != std::errc{}) continue;
                if (!key) {
                    const auto stat = readText(root_.proc / name / "stat");
                    const auto parsed = stat.ok() ? parseProcStat(stat.text) : std::nullopt;
                    if (!parsed) break;  // the process went away while we looked
                    key = ProcessKey{pid, parsed->startTicks};
                }
                owners_.emplace_back(inode, *key);
            }
            ::closedir(fdDir);
        }
        ::closedir(procDir);
    }
    std::sort(owners_.begin(), owners_.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    ownersAt_ = now;
    ownersValid_ = true;
    for (const auto& c : rows) {
        if (c.inode == 0) continue;
        const auto it = std::lower_bound(owners_.begin(), owners_.end(), c.inode, [](const auto& p, std::uint64_t inode) { return p.first < inode; });
        if (it == owners_.end() || it->first != c.inode) unattributed_.insert(c.inode);
    }
}

ConnectionList ConnectionsCollector::collect() {
    ConnectionList out;
    struct Table {
        const char* file;
        Proto proto;
    };
    for (const auto& t : {Table{"net/tcp", Proto::Tcp4}, Table{"net/tcp6", Proto::Tcp6}, Table{"net/udp", Proto::Udp4},
                          Table{"net/udp6", Proto::Udp6}}) {
        const auto text = readText(root_.proc / t.file);
        if (!text.ok()) continue;  // e.g. IPv6 switched off
        for (auto& c : parseNetTable(text.text, t.proto)) {
            if (out.rows.size() >= kMaxConnections) {
                ++out.truncated;
                continue;
            }
            out.rows.push_back(std::move(c));
        }
    }
    refreshOwners(out.rows);
    out.anyUnreadableOwners = unreadable_;
    for (auto& c : out.rows) {
        if (c.inode == 0) {  // TIME_WAIT and friends belong to the kernel
            c.ownerAvailability = Availability::Unsupported;
            continue;
        }
        const auto it = std::lower_bound(owners_.begin(), owners_.end(), c.inode,
                                         [](const auto& p, std::uint64_t inode) { return p.first < inode; });
        if (it != owners_.end() && it->first == c.inode) {
            c.owner = it->second;
            c.ownerAvailability = Availability::Measured;
        } else {
            c.ownerAvailability = unreadable_ ? Availability::Restricted : Availability::Unsupported;
        }
    }
    return out;
}

}  // namespace pmp::linux
