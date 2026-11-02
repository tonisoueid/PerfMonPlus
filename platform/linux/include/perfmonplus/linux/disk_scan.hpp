// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#pragma once

#include <perfmonplus/linux/proc_fs.hpp>

#include <sys/types.h>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace pmp::linux {

struct ScanOptions {
    std::filesystem::path root;
    bool crossFilesystems = false;        // SPC-04: other filesystems and mount points are skipped by default
    std::size_t keepChildren = 256;       // per directory; the rest is folded into one "other items" node
    std::size_t largestFiles = 500;
    std::size_t maxNodes = 2'000'000;     // memory bound: past it, directories are still summed but not kept
    bool lowPriority = true;              // nice 19 and idle I/O for the calling thread, so a scan never slows the desktop
    // Test hook: called with each entry's path just before it is examined (to delete it, cancel, ...).
    std::function<void(const std::filesystem::path&)> beforeStat;
    // Test hook: lets a test pretend a directory sits on another filesystem (a real one needs administrator rights).
    std::function<dev_t(const std::string& path, dev_t actual)> deviceOf;
};

struct ScanNode {
    std::string name;      // sanitised for display
    std::string rawName;   // the exact bytes, for building paths (never displayed)
    std::uint64_t bytes = 0;   // allocated size (st_blocks * 512); a hard-linked file counts once per scan
    std::uint64_t files = 0;
    bool isDir = false;
    bool isOther = false;  // the synthetic "n other items" node
    std::vector<ScanNode> children;  // largest first
};

struct FileEntry {
    std::string path;      // exact bytes
    std::string display;   // sanitised
    std::uint64_t bytes = 0;
    std::int64_t mtime = 0;
    std::string ext;       // lower-case, or "(none)"
};

struct ScanProgress {
    std::uint64_t files = 0, dirs = 0, bytes = 0;
    std::string current;   // sanitised path of the folder being read
};

struct ScanResult {
    ScanNode root;
    std::vector<FileEntry> largest;                                   // descending
    std::map<std::string, std::pair<std::uint64_t, std::uint64_t>> byType;  // ext -> {files, bytes}
    std::uint64_t dirs = 0;  // every folder read, kept or not
    std::uint64_t inaccessible = 0, skippedMounts = 0, vanished = 0;
    bool cancelled = false, truncated = false, rootUnreadable = false;
};

class DiskScanner {
public:
    // Reads `options.root` and everything below it. Never follows a symbolic link, never leaves the starting
    // filesystem unless asked, and checks `cancel` between entries. Progress is reported at most every 100 ms.
    ScanResult scan(const ScanOptions& options, const std::atomic<bool>& cancel,
                    const std::function<void(const ScanProgress&)>& progress = {}) const;
};

struct MountInfo {
    std::string mountPoint, device, fsType;
    std::uint64_t totalBytes = 0, freeBytes = 0;
    bool pseudo = false;  // proc, sysfs, cgroups, devices... not worth scanning
};
// /proc/self/mountinfo plus statvfs (skipped for pseudo and network filesystems, which may block).
std::vector<MountInfo> readMounts(const ProcRoot& root);
std::vector<MountInfo> parseMountInfo(std::string_view text);

}  // namespace pmp::linux
