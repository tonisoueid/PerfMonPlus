// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/linux/proc_fs.hpp>

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <charconv>

namespace pmp::linux {

ProcRoot ProcRoot::live() {
    ProcRoot r;
    r.clockTicksPerSec = sysconf(_SC_CLK_TCK);
    r.pageSize = sysconf(_SC_PAGESIZE);
    return r;
}

FileText readText(const std::filesystem::path& p) {
    FileText out;
    const int fd = ::open(p.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        out.error = errno;
        return out;
    }
    char buf[8192];
    for (;;) {
        const ssize_t n = ::read(fd, buf, sizeof buf);
        if (n > 0) {
            out.text.append(buf, static_cast<std::size_t>(n));
        } else if (n == 0) {
            break;
        } else if (errno != EINTR) {
            out.error = errno;
            break;
        }
    }
    ::close(fd);
    return out;
}

NumberRead readNumber(const std::filesystem::path& p) {
    const auto f = readText(p);
    NumberRead r;
    if (!f.ok()) {
        r.error = f.error;
        return r;
    }
    double v = 0;
    const auto* b = f.text.data();
    const auto* e = b + f.text.size();
    while (b < e && (*b == ' ' || *b == '\n' || *b == '\t')) ++b;
    const auto res = std::from_chars(b, e, v);
    if (res.ec != std::errc{}) {
        r.error = EINVAL;
        return r;
    }
    r.value = v;
    return r;
}

Metric readMetric(const std::filesystem::path& p, Unit unit, double scale) {
    const auto n = readNumber(p);
    if (n.value) return Metric::measured(*n.value * scale, unit);
    return Metric::notMeasured(availabilityForErrno(n.error), unit);
}

Availability availabilityForErrno(int err) {
    switch (err) {
        case ENOENT:
        case ENOTDIR:
        case EOPNOTSUPP:  // e.g. /proc/pressure/* when the kernel booted with psi=0
        case ENOSYS: return Availability::Unsupported;
        case EACCES:
        case EPERM: return Availability::Restricted;
        default: return Availability::Failed;
    }
}

namespace {

std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
    return s;
}

bool parseLeadingUint(std::string_view s, std::uint64_t& out) {
    s = trim(s);
    const auto r = std::from_chars(s.data(), s.data() + s.size(), out);
    return r.ec == std::errc{} && r.ptr != s.data();
}

template <class SplitFn>
std::map<std::string, std::uint64_t, std::less<>> parseLines(std::string_view text, SplitFn split) {
    std::map<std::string, std::uint64_t, std::less<>> out;
    while (!text.empty()) {
        const auto nl = text.find('\n');
        const std::string_view line = text.substr(0, nl);
        text = nl == std::string_view::npos ? std::string_view{} : text.substr(nl + 1);
        std::string_view key, rest;
        std::uint64_t v = 0;
        if (split(line, key, rest) && parseLeadingUint(rest, v)) out[std::string(key)] = v;
    }
    return out;
}

}  // namespace

std::map<std::string, std::uint64_t, std::less<>> parseColonValues(std::string_view text) {
    return parseLines(text, [](std::string_view line, std::string_view& k, std::string_view& r) {
        const auto c = line.find(':');
        if (c == std::string_view::npos) return false;
        k = line.substr(0, c);
        r = line.substr(c + 1);
        return true;
    });
}

std::map<std::string, std::uint64_t, std::less<>> parseSpaceValues(std::string_view text) {
    return parseLines(text, [](std::string_view line, std::string_view& k, std::string_view& r) {
        const auto sp = line.find(' ');
        if (sp == std::string_view::npos) return false;
        k = line.substr(0, sp);
        r = line.substr(sp + 1);
        return true;
    });
}

std::vector<int> parseCpuList(std::string_view text) {
    std::vector<int> out;
    text = trim(text);
    while (!text.empty()) {
        const auto comma = text.find(',');
        const std::string_view tok = text.substr(0, comma);
        text = comma == std::string_view::npos ? std::string_view{} : text.substr(comma + 1);
        int lo = 0, hi = 0;
        const auto dash = tok.find('-');
        const auto parse = [](std::string_view s, int& v) {
            return std::from_chars(s.data(), s.data() + s.size(), v).ec == std::errc{};
        };
        if (dash == std::string_view::npos) {
            if (!parse(tok, lo)) continue;
            hi = lo;
        } else if (!parse(tok.substr(0, dash), lo) || !parse(tok.substr(dash + 1), hi)) {
            continue;
        }
        for (int i = lo; i <= hi; ++i) out.push_back(i);
    }
    return out;
}

namespace {

// U+061C, U+200E/F, U+2028/9, U+202A-E, U+2066-9: text direction controls and line separators.
bool isBidiOrLineSeparator(std::string_view seq) {
    const auto b = [&](std::size_t i) { return static_cast<unsigned char>(seq[i]); };
    if (seq.size() == 2) return b(0) == 0xD8 && b(1) == 0x9C;
    if (seq.size() != 3 || b(0) != 0xE2) return false;
    if (b(1) == 0x80) return b(2) == 0x8E || b(2) == 0x8F || b(2) == 0xA8 || b(2) == 0xA9 || (b(2) >= 0xAA && b(2) <= 0xAE);
    if (b(1) == 0x81) return b(2) >= 0xA6 && b(2) <= 0xA9;
    return false;
}

}  // namespace

std::string sanitizeText(std::string_view raw, std::size_t maxBytes) {
    std::string out;
    out.reserve(std::min(raw.size(), maxBytes) + 4);
    std::size_t i = 0;
    bool cut = false;
    while (i < raw.size()) {
        const auto c = static_cast<unsigned char>(raw[i]);
        std::size_t len = 1;
        bool valid = true;
        if (c < 0x80) {
            valid = true;
        } else if (c >= 0xC2 && c <= 0xDF) {
            len = 2;
        } else if (c >= 0xE0 && c <= 0xEF) {
            len = 3;
        } else if (c >= 0xF0 && c <= 0xF4) {
            len = 4;
        } else {
            valid = false;
        }
        if (valid && len > 1) {
            if (i + len > raw.size()) {
                valid = false;
            } else {
                for (std::size_t k = 1; k < len; ++k) {
                    if ((static_cast<unsigned char>(raw[i + k]) & 0xC0) != 0x80) valid = false;
                }
            }
        }
        std::string piece;
        if (!valid) {
            piece = "?";
            len = 1;
        } else if (c == 0) {
            piece = " ";
        } else if (c < 0x20 || c == 0x7f) {
            piece = "?";
        } else if (isBidiOrLineSeparator(raw.substr(i, len))) {
            piece = "?";  // a right-to-left override in a name would reverse the text printed next to it
        } else {
            piece = std::string(raw.substr(i, len));
        }
        if (out.size() + piece.size() > maxBytes) {
            cut = true;
            break;
        }
        out += piece;
        i += len;
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    if (cut) out += "\xE2\x80\xA6";  // …
    return out;
}

}  // namespace pmp::linux
