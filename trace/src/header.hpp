// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#pragma once

#include <perfmonplus/trace/container.hpp>
#include <perfmonplus/trace/trace_info.hpp>

#include <optional>
#include <span>
#include <string>

namespace pmp::trace {

inline constexpr char kMagic[] = "PMPTRACE";  // 8 bytes, no terminator written
inline constexpr char kFooterMagic[] = "PMPEND01";

Bytes encodeHeader(const TraceInfo& info);

enum class HeaderStatus { Ok, NotATrace, NewerVersion, Corrupt };
struct ParsedHeader {
    HeaderStatus status = HeaderStatus::NotATrace;
    TraceInfo info;
    std::size_t bytes = 0;  // header length including its CRC
};
ParsedHeader parseHeader(std::span<const std::byte> file);

}  // namespace pmp::trace
