// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/linux/drm.hpp>

#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <charconv>
#include <sstream>

namespace pmp::linux {
namespace {

std::string trimmed(std::string s) {
    while (!s.empty() && (s.back() == '\n' || s.back() == ' ' || s.back() == '\r' || s.back() == '\t')) s.pop_back();
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.erase(s.begin());
    return s;
}

std::optional<std::uint64_t> leadingUint(std::string_view s) {
    while (!s.empty() && s.front() == ' ') s.remove_prefix(1);
    std::uint64_t v = 0;
    const auto r = std::from_chars(s.data(), s.data() + s.size(), v);
    if (r.ec != std::errc{}) return std::nullopt;
    return v;
}

bool ownedByRoot(const std::filesystem::path& p) {
    struct stat st{};
    return ::stat(p.c_str(), &st) == 0 && st.st_uid == 0;
}

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

}  // namespace

std::map<std::string, std::string, std::less<>> parseColonStrings(std::string_view text) {
    std::map<std::string, std::string, std::less<>> out;
    std::istringstream in{std::string(text)};
    std::string line;
    while (std::getline(in, line)) {
        const auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        out[trimmed(line.substr(0, colon))] = trimmed(line.substr(colon + 1));
    }
    return out;
}

std::optional<double> parseDrmMemory(std::string_view value) {
    double v = 0;
    const auto r = std::from_chars(value.data(), value.data() + value.size(), v);
    if (r.ec != std::errc{}) return std::nullopt;
    std::string unit = trimmed(std::string(value.substr(static_cast<std::size_t>(r.ptr - value.data()))));
    if (unit.empty() || unit == "B") return v;
    if (unit == "KiB") return v * 1024.0;
    if (unit == "MiB") return v * 1024.0 * 1024.0;
    if (unit == "GiB") return v * 1024.0 * 1024.0 * 1024.0;
    return std::nullopt;
}

std::string gpuName(const ProcRoot& root, const std::string& card, const std::filesystem::path& pciIds) {
    auto strip = [](std::string s) {
        s = trimmed(s);
        if (s.rfind("0x", 0) == 0) s = s.substr(2);
        return lower(s);
    };
    const auto base = root.sys / "class/drm" / card / "device";
    const std::string vendor = strip(readText(base / "vendor").text);
    const std::string device = strip(readText(base / "device").text);

    std::vector<std::filesystem::path> candidates;
    if (!pciIds.empty()) candidates = {pciIds};
    else candidates = {"/usr/share/misc/pci.ids", "/usr/share/hwdata/pci.ids"};
    for (const auto& c : candidates) {
        const auto f = readText(c);
        if (!f.ok()) continue;
        std::istringstream in(f.text);
        std::string line;
        bool inVendor = false;
        while (std::getline(in, line)) {
            if (line.empty() || line[0] == '#') continue;
            if (line[0] != '\t') {
                inVendor = line.rfind(vendor, 0) == 0 && line.size() > vendor.size() && line[vendor.size()] == ' ';
            } else if (inVendor && line.size() > 1 && line[1] != '\t' && line.rfind("\t" + device, 0) == 0) {
                return trimmed(line.substr(1 + device.size()));
            }
        }
        break;
    }
    if (vendor == "8086") return "Intel GPU";
    if (vendor == "1002") return "AMD GPU";
    if (vendor == "10de") return "NVIDIA GPU";
    return "GPU";
}

DrmClientScanner::DrmClientScanner(ProcRoot root, int slices) : root_(std::move(root)), slices_(std::max(1, slices)) {}

std::vector<DrmClient> DrmClientScanner::scan(const std::vector<std::int32_t>& livePids,
                                              const std::set<std::int32_t>& newPids) {
    const std::set<std::int32_t> alive(livePids.begin(), livePids.end());
    for (auto it = known_.begin(); it != known_.end();) it = alive.contains(it->first) ? std::next(it) : known_.erase(it);
    std::erase_if(covered_, [&](std::int32_t p) { return !alive.contains(p); });
    std::erase_if(unreadable_, [&](std::int32_t p) { return !alive.contains(p); });

    // Discovery: every new pid, plus this scan's rotating share of the rest.
    std::set<std::int32_t> toScan;
    for (std::size_t i = 0; i < livePids.size(); ++i) {
        if (static_cast<int>(i % static_cast<std::size_t>(slices_)) == static_cast<int>(rotation_ % static_cast<std::size_t>(slices_))) {
            toScan.insert(livePids[i]);
        }
    }
    for (auto p : newPids) {
        if (alive.contains(p)) toScan.insert(p);
    }
    ++rotation_;
    lastScanned_ = toScan.size();

    for (auto pid : toScan) {
        const auto fdDir = root_.proc / std::to_string(pid) / "fd";
        std::error_code ec;
        std::filesystem::directory_iterator it(fdDir, ec);
        if (ec) {
            if (availabilityForErrno(ec.value()) == Availability::Restricted && !(ownedByRoot(root_.proc / std::to_string(pid)) && geteuid() != 0)) {
                unreadable_.insert(pid);
            }
            continue;
        }
        unreadable_.erase(pid);
        covered_.insert(pid);
        for (std::filesystem::directory_iterator end; it != end; it.increment(ec)) {
            std::error_code lec;
            const auto target = std::filesystem::read_symlink(it->path(), lec);
            if (lec || target.string().rfind("/dev/dri/", 0) != 0) continue;
            if (const auto fd = leadingUint(it->path().filename().string())) known_[pid].insert(static_cast<int>(*fd));
        }
    }

    // Read every known DRM fd and deduplicate clients (lowest pid wins: known_ is ordered by pid).
    std::map<std::pair<std::string, std::uint64_t>, DrmClient> clients;
    for (auto& [pid, fds] : known_) {
        for (auto fdIt = fds.begin(); fdIt != fds.end();) {
            const auto info = readText(root_.proc / std::to_string(pid) / "fdinfo" / std::to_string(*fdIt));
            if (!info.ok()) {
                fdIt = fds.erase(fdIt);  // fd closed
                continue;
            }
            ++fdIt;
            DrmClient c;
            c.pid = pid;
            std::istringstream in(info.text);
            std::string line;
            bool haveId = false;
            while (std::getline(in, line)) {
                const auto colon = line.find(':');
                if (colon == std::string::npos) continue;
                const std::string key = line.substr(0, colon);
                const std::string value = trimmed(line.substr(colon + 1));
                if (key == "drm-pdev") c.pdev = value;
                else if (key == "drm-client-id") {
                    if (const auto id = leadingUint(value)) {
                        c.clientId = *id;
                        haveId = true;
                    }
                } else if (key.rfind("drm-engine-capacity-", 0) == 0) {
                    if (const auto n = leadingUint(value)) c.engineCapacity[key.substr(20)] = *n;
                } else if (key.rfind("drm-engine-", 0) == 0) {
                    if (const auto n = leadingUint(value)) c.engineNs.emplace_back(key.substr(11), *n);
                } else if (key.rfind("drm-resident-", 0) == 0) {
                    c.residentBytes += parseDrmMemory(value).value_or(0.0);
                }
            }
            if (c.pdev.empty() || !haveId) continue;
            clients.try_emplace({c.pdev, c.clientId}, std::move(c));
        }
    }
    std::vector<DrmClient> out;
    for (auto& [key, c] : clients) out.push_back(std::move(c));
    return out;
}

}  // namespace pmp::linux
