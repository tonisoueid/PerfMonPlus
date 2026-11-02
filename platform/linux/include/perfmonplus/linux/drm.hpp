// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#pragma once

#include <perfmonplus/linux/proc_fs.hpp>

#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace pmp::linux {

// "drm-pdev:\t0000:00:02.0" -> {"drm-pdev", "0000:00:02.0"}; lines without a colon are ignored.
std::map<std::string, std::string, std::less<>> parseColonStrings(std::string_view text);

// "394480 KiB" | "64 MiB" | "2 GiB" | "512" (bytes) -> bytes.
std::optional<double> parseDrmMemory(std::string_view value);

// Marketing name from pci.ids for /sys/class/drm/<card>/device; falls back to "<Vendor> GPU".
// `pciIds` empty -> /usr/share/misc/pci.ids, then /usr/share/hwdata/pci.ids.
std::string gpuName(const ProcRoot& root, const std::string& card, const std::filesystem::path& pciIds = {});

// One DRM client (a GPU context) as published in /proc/<pid>/fdinfo/<fd>.
struct DrmClient {
    std::string pdev;
    std::uint64_t clientId = 0;
    std::int32_t pid = 0;  // the lowest pid that holds the client
    std::vector<std::pair<std::string, std::uint64_t>> engineNs;  // in fdinfo order
    std::map<std::string, std::uint64_t> engineCapacity;
    double residentBytes = 0;
};

// Finds DRM clients without reading every process's fd table every second: new processes are always examined,
// the rest are visited in rotating slices, and fds already known to be DRM are re-read every scan.
class DrmClientScanner {
public:
    explicit DrmClientScanner(ProcRoot root, int slices = 10);

    // Clients are deduplicated by (drm-pdev, drm-client-id), so dup()'d fds and forked children count once.
    std::vector<DrmClient> scan(const std::vector<std::int32_t>& livePids, const std::set<std::int32_t>& newPids);

    bool sawUnreadable() const noexcept { return !unreadable_.empty(); }  // other users' fd tables we cannot see
    bool covers(std::int32_t pid) const { return covered_.contains(pid); }
    std::size_t lastScannedPidCount() const noexcept { return lastScanned_; }

private:
    ProcRoot root_;
    int slices_;
    std::size_t rotation_ = 0;
    std::size_t lastScanned_ = 0;
    std::map<std::int32_t, std::set<int>> known_;  // pid -> fds that are DRM
    std::set<std::int32_t> covered_, unreadable_;
};

}  // namespace pmp::linux
