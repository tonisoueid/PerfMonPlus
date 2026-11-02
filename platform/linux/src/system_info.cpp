// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/linux/system_info.hpp>

#include <perfmonplus/linux/drm.hpp>

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <sys/utsname.h>

#include <set>
#include <sstream>

namespace pmp::linux {
namespace {

std::string trimmed(std::string s) {
    while (!s.empty() && (s.back() == '\n' || s.back() == ' ' || s.back() == '\r')) s.pop_back();
    return s;
}

std::string readLine(const std::filesystem::path& p) { return trimmed(readText(p).text); }

std::set<std::string> listDir(const std::filesystem::path& dir) {
    std::set<std::string> out;
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(dir, ec)) out.insert(e.path().filename().string());
    return out;
}

std::string osPrettyName(const std::filesystem::path& etc) {
    std::istringstream in(readText(etc / "os-release").text);
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("PRETTY_NAME=", 0) != 0) continue;
        std::string v = line.substr(12);
        if (v.size() >= 2 && v.front() == '"' && v.back() == '"') v = v.substr(1, v.size() - 2);
        return v;
    }
    return {};
}

}  // namespace

SystemInfo collectSystemInfo(const ProcRoot& root, const std::filesystem::path& etc) {
    SystemInfo s;
    s.osName = osPrettyName(etc);
    s.kernel = readLine(root.proc / "sys/kernel/osrelease");
    s.hostname = readLine(root.proc / "sys/kernel/hostname");
    utsname u{};
    if (root.proc == "/proc" && uname(&u) == 0) s.architecture = u.machine;

    const auto dmi = root.sys / "class/dmi/id";
    s.vendor = readLine(dmi / "sys_vendor");
    s.model = readLine(dmi / "product_name");
    s.biosVersion = readLine(dmi / "bios_version");
    s.serial = readLine(dmi / "product_serial");  // an unreadable file simply yields ""

    {
        std::istringstream in(readText(root.proc / "stat").text);
        std::string line;
        s.bootTime = Metric::notMeasured(Availability::Unsupported, Unit::Seconds);
        while (std::getline(in, line)) {
            if (line.rfind("btime ", 0) == 0) {
                try {
                    s.bootTime = Metric::measured(std::stod(line.substr(6)), Unit::Seconds);
                } catch (...) {
                }
            }
        }
    }
    {
        const auto up = readText(root.proc / "uptime");
        s.uptime = Metric::notMeasured(up.ok() ? Availability::Failed : availabilityForErrno(up.error), Unit::Seconds);
        try {
            if (up.ok()) s.uptime = Metric::measured(std::stod(up.text), Unit::Seconds);
        } catch (...) {
        }
    }
    {
        const auto mem = parseColonValues(readText(root.proc / "meminfo").text);
        const auto kb = [&](const char* k) {
            const auto it = mem.find(k);
            return it == mem.end() ? Metric::notMeasured(Availability::Unsupported, Unit::Bytes)
                                   : Metric::measured(static_cast<double>(it->second) * 1024.0, Unit::Bytes);
        };
        s.memTotal = kb("MemTotal");
        s.swapTotal = kb("SwapTotal");
    }

    s.cpu = collectCpuFacts(root);

    for (const auto& name : listDir(root.sys / "class/drm")) {
        if (name.rfind("card", 0) != 0 || name.find('-') != std::string::npos) continue;
        std::error_code ec;
        if (std::filesystem::exists(root.sys / "class/drm" / name / "device", ec)) s.gpus.push_back(gpuName(root, name));
    }

    std::map<std::string, std::pair<std::string, std::string>> ips;
    if (root.proc == "/proc") {
        ifaddrs* list = nullptr;
        if (getifaddrs(&list) == 0) {
            for (auto* a = list; a; a = a->ifa_next) {
                if (!a->ifa_addr) continue;
                char buf[INET6_ADDRSTRLEN] = {};
                if (a->ifa_addr->sa_family == AF_INET &&
                    inet_ntop(AF_INET, &reinterpret_cast<sockaddr_in*>(a->ifa_addr)->sin_addr, buf, sizeof buf)) {
                    ips[a->ifa_name].first = buf;
                } else if (a->ifa_addr->sa_family == AF_INET6 &&
                           inet_ntop(AF_INET6, &reinterpret_cast<sockaddr_in6*>(a->ifa_addr)->sin6_addr, buf, sizeof buf) &&
                           ips[a->ifa_name].second.empty()) {
                    ips[a->ifa_name].second = buf;
                }
            }
            freeifaddrs(list);
        }
    }
    for (const auto& name : listDir(root.sys / "class/net")) {
        const auto dir = root.sys / "class/net" / name;
        NetAdapter a;
        a.name = name;
        a.mac = readLine(dir / "address");
        std::error_code ec;
        a.isVirtual = !std::filesystem::exists(dir / "device", ec);
        a.speedBps = readMetric(dir / "speed", Unit::BytesPerSec, 125000.0);  // Mb/s -> bytes/s
        if (a.speedBps.isMeasured() && *a.speedBps.value <= 0) {
            a.speedBps = Metric::notMeasured(Availability::Unsupported, Unit::BytesPerSec);  // "-1": no link
        }
        if (const auto ip = ips.find(name); ip != ips.end()) {
            a.ipv4 = ip->second.first;
            a.ipv6 = ip->second.second;
        }
        s.adapters.push_back(std::move(a));
    }

    const auto sb = readText(root.sys / "firmware/efi/efivars/SecureBoot-8be4df61-93ca-11d2-aa0d-00e098032b8c");
    if (sb.ok() && sb.text.size() >= 5) s.secureBoot = sb.text.back() == 1;
    return s;
}

}  // namespace pmp::linux
