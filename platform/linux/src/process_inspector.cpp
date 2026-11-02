// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/linux/process_inspector.hpp>

#include <perfmonplus/linux/identity.hpp>

#include <cerrno>
#include <filesystem>
#include <fstream>
#include <vector>

namespace pmp::linux {
namespace {

constexpr std::size_t kCmdlineMax = 8192;
constexpr std::size_t kEnvironMax = 64 * 1024;

bool denied(int err) { return err == EACCES || err == EPERM; }

std::string architectureOf(const std::filesystem::path& exe) {
    std::ifstream in(exe, std::ios::binary);
    unsigned char h[20] = {};
    if (!in.read(reinterpret_cast<char*>(h), sizeof h)) return {};
    if (h[0] != 0x7f || h[1] != 'E' || h[2] != 'L' || h[3] != 'F') return {};
    const unsigned machine = h[5] == 2 ? (static_cast<unsigned>(h[18]) << 8 | h[19]) : (static_cast<unsigned>(h[19]) << 8 | h[18]);
    switch (machine) {
        case 0x3e: return "x86_64";
        case 0xb7: return "arm64";
        case 0x03: return "i386";
        case 0x28: return "arm";
        case 0xf3: return "riscv64";
        default: return {};
    }
}

}  // namespace

std::optional<Inspection> ProcessInspector::inspect(ProcessKey key, bool withEnvironment) const {
    if (verifyIdentity(root_, key) != IdentityCheck::Alive) return std::nullopt;
    const auto dir = root_.proc / std::to_string(key.pid);
    Inspection out;

    const auto cmd = readText(dir / "cmdline");
    if (cmd.ok()) out.cmdline = sanitizeText(cmd.text, kCmdlineMax);
    else out.cmdlineRestricted = denied(cmd.error);

    if (withEnvironment) {
        const auto env = readText(dir / "environ");
        if (!env.ok()) {
            out.environmentRestricted = denied(env.error);
        } else {
            std::string_view text = env.text;
            if (text.size() > kEnvironMax) {
                text = text.substr(0, kEnvironMax);
                out.environmentTruncated = true;
            }
            std::string joined;
            while (!text.empty()) {
                const auto nul = text.find('\0');
                const auto var = text.substr(0, nul);
                if (!var.empty()) {
                    if (!joined.empty()) joined += '\n';
                    joined += sanitizeText(var, kEnvironMax);
                }
                if (nul == std::string_view::npos) break;
                text.remove_prefix(nul + 1);
            }
            out.environment = std::move(joined);
        }
    }

    std::error_code ec;
    int files = 0, sockets = 0;
    for (std::filesystem::directory_iterator it(dir / "fd", ec), end; !ec && it != end; it.increment(ec)) {
        ++files;
        std::error_code lec;
        const auto target = std::filesystem::read_symlink(it->path(), lec);
        if (!lec && target.string().starts_with("socket:[")) ++sockets;
    }
    if (!ec) {
        out.openFiles = files;
        out.sockets = sockets;
    }

    // Open the link itself, not its text: the kernel then opens the very file the process runs, even if it was deleted
    // or lives in another mount namespace, where the resolved path would name something else (or nothing).
    out.architecture = architectureOf(dir / "exe");
    if (beforeFinalCheck) beforeFinalCheck();
    // The reads above took time; if the PID was recycled meanwhile they describe two different processes.
    if (verifyIdentity(root_, key) != IdentityCheck::Alive) return std::nullopt;
    return out;
}

}  // namespace pmp::linux
