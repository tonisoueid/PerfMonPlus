// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include "header.hpp"

#include <nlohmann/json.hpp>

#include <cstring>

namespace pmp::trace {
namespace {

using json = nlohmann::json;

constexpr std::uint32_t kMaxHeaderJson = 1u << 20;

std::string getString(const json& j, const char* key) {
    const auto it = j.find(key);
    return it != j.end() && it->is_string() ? it->get<std::string>() : std::string();
}

template <class T>
T getNumber(const json& j, const char* key, T fallback) {
    const auto it = j.find(key);
    return it != j.end() && it->is_number() ? it->get<T>() : fallback;
}

}  // namespace

Bytes encodeHeader(const TraceInfo& info) {
    json host = {{"os", info.host.os},
                 {"kernel", info.host.kernel},
                 {"model", info.host.model},
                 {"cpuModel", info.host.cpuModel},
                 {"logicalCpus", info.host.logicalCpus},
                 {"memoryBytes", info.host.memoryBytes}};
    json j = {{"format", info.format},
              {"codec", info.codec},
              {"app", info.app},
              {"host", host},
              {"startWallMs", info.startWallMs},
              {"intervalMs", info.intervalMs},
              {"topProcesses", info.topProcesses}};
    const auto text = j.dump();  // keys are sorted, so the bytes are deterministic
    ByteWriter w;
    for (int i = 0; i < 8; ++i) w.u8(static_cast<std::uint8_t>(kMagic[i]));
    w.u32(info.format);
    w.u32(static_cast<std::uint32_t>(text.size()));
    for (const char c : text) w.u8(static_cast<std::uint8_t>(c));
    w.u32(crc32(w.data()));
    return w.take();
}

ParsedHeader parseHeader(std::span<const std::byte> file) {
    ParsedHeader out;
    if (file.size() < 8 || std::memcmp(file.data(), kMagic, 8) != 0) return out;  // NotATrace
    ByteReader r(file);
    r.take(8);
    const auto version = r.u32();
    const auto jsonLen = r.u32();
    if (r.failed()) {
        out.status = HeaderStatus::Corrupt;
        return out;
    }
    if (version > kFormatVersion) {
        out.status = HeaderStatus::NewerVersion;
        out.info.format = version;
        return out;
    }
    if (jsonLen > kMaxHeaderJson || jsonLen > r.remaining()) {
        out.status = HeaderStatus::Corrupt;
        return out;
    }
    const auto body = r.take(jsonLen);
    const auto crcAt = r.position();
    const auto crc = r.u32();
    if (r.failed() || crc != crc32(file.first(crcAt))) {
        out.status = HeaderStatus::Corrupt;
        return out;
    }
    const auto j = json::parse(reinterpret_cast<const char*>(body.data()),
                               reinterpret_cast<const char*>(body.data()) + body.size(), nullptr, false);
    if (!j.is_object()) {
        out.status = HeaderStatus::Corrupt;
        return out;
    }
    auto& i = out.info;
    i.format = version;
    i.codec = getString(j, "codec");
    i.app = getString(j, "app");
    i.startWallMs = getNumber<std::int64_t>(j, "startWallMs", 0);
    i.intervalMs = getNumber<int>(j, "intervalMs", 1000);
    i.topProcesses = getNumber<int>(j, "topProcesses", 0);
    if (const auto h = j.find("host"); h != j.end() && h->is_object()) {
        i.host.os = getString(*h, "os");
        i.host.kernel = getString(*h, "kernel");
        i.host.model = getString(*h, "model");
        i.host.cpuModel = getString(*h, "cpuModel");
        i.host.logicalCpus = getNumber<int>(*h, "logicalCpus", 0);
        i.host.memoryBytes = getNumber<std::uint64_t>(*h, "memoryBytes", 0);
    }
    if (i.codec != "deflate-shuffle8") {  // a codec this build cannot decode is a newer file, not a broken one
        out.status = HeaderStatus::NewerVersion;
        return out;
    }
    out.bytes = r.position();
    out.status = HeaderStatus::Ok;
    return out;
}

}  // namespace pmp::trace
