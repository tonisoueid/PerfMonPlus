// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#pragma once

#include <perfmonplus/core/reading.hpp>

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pmp::linux {

// Where to find /proc and /sys, so tests can point providers at a fake tree.
struct ProcRoot {
    std::filesystem::path proc{"/proc"};
    std::filesystem::path sys{"/sys"};
    long clockTicksPerSec = 100;
    long pageSize = 4096;

    static ProcRoot live();
};

struct FileText {
    std::string text;
    int error = 0;  // errno, 0 on success
    bool ok() const noexcept { return error == 0; }
};

// Reads a whole file (procfs files report size 0, so this reads until EOF).
FileText readText(const std::filesystem::path& p);

// One number from a sysfs/procfs file. `error` is errno (0 = read and parsed); a parse failure is EINVAL.
struct NumberRead {
    std::optional<double> value;
    int error = 0;
};
NumberRead readNumber(const std::filesystem::path& p);

// A reading of `scale` x the file's number. ENOENT/ENOTDIR -> Unsupported, EACCES/EPERM -> Restricted, any other
// error (ENODATA, EAGAIN, EIO, EINVAL, ...) -> Failed. Never throws: one sensor must not take down a provider.
Metric readMetric(const std::filesystem::path& p, Unit unit, double scale = 1.0);

// ENOENT/ENOTDIR -> Unsupported, EACCES/EPERM -> Restricted, else Failed (INT-04).
Availability availabilityForErrno(int err);

// "Key:  123 kB" -> {"Key", 123}. Lines whose value isn't numeric are skipped.
std::map<std::string, std::uint64_t, std::less<>> parseColonValues(std::string_view text);
// "pgpgin 152" -> {"pgpgin", 152}.
std::map<std::string, std::uint64_t, std::less<>> parseSpaceValues(std::string_view text);
// Text that came from another program (a command line, an environment): NUL becomes a space, control characters
// and invalid UTF-8 become '?', and anything past `maxBytes` is cut on a character boundary and ends with "…".
std::string sanitizeText(std::string_view raw, std::size_t maxBytes);
// "0-3,8" -> {0,1,2,3,8}.
std::vector<int> parseCpuList(std::string_view text);

}  // namespace pmp::linux
