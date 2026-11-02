// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/linux/installed_apps.hpp>

#include <perfmonplus/linux/proc_fs.hpp>

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <sstream>
#include <sys/stat.h>

namespace pmp::linux {
namespace {

namespace fs = std::filesystem;
constexpr std::size_t kText = 512;

std::string clean(std::string_view s) { return sanitizeText(s, kText); }

Metric bytesMetric(double v) { return Metric::measured(v, Unit::Bytes); }

std::vector<std::string> split(std::string_view s, char sep) {
    std::vector<std::string> out;
    std::size_t pos = 0;
    while (pos <= s.size()) {
        auto end = s.find(sep, pos);
        if (end == std::string_view::npos) end = s.size();
        out.emplace_back(s.substr(pos, end - pos));
        pos = end + 1;
    }
    return out;
}

std::optional<std::chrono::system_clock::time_point> mtimeOf(const fs::path& p) {
    struct stat st{};
    if (::stat(p.c_str(), &st) != 0) return std::nullopt;
    return std::chrono::system_clock::time_point(std::chrono::seconds(st.st_mtime));
}

}  // namespace

std::string sourceText(AppSource s) {
    switch (s) {
        case AppSource::Dpkg: return "apt";
        case AppSource::Rpm: return "rpm";
        case AppSource::Flatpak: return "Flatpak";
        case AppSource::Snap: return "Snap";
        case AppSource::AppImage: return "AppImage";
    }
    return "?";
}

AppsRoot AppsRoot::live() {
    AppsRoot r;
    if (const char* home = std::getenv("HOME"); home && *home) {
        const fs::path h = home;
        r.appImageDirs = {h / "Applications", h / ".local/bin", h / "Downloads"};
    }
    return r;
}

std::optional<double> parseHumanSize(std::string_view text) {
    while (!text.empty() && text.front() == ' ') text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\n' || text.back() == '\r')) text.remove_suffix(1);
    const auto sp = text.find(' ');
    const auto num = text.substr(0, sp);
    const auto unit = sp == std::string_view::npos ? std::string_view() : text.substr(sp + 1);
    // Flatpak prints "9.7 GB" through g_format_size, which is decimal; the number may use a comma in some locales.
    std::string digits(num);
    std::replace(digits.begin(), digits.end(), ',', '.');
    char* end = nullptr;
    const double v = std::strtod(digits.c_str(), &end);
    if (digits.empty() || end != digits.c_str() + digits.size() || v < 0) return std::nullopt;
    static const std::pair<std::string_view, double> units[] = {
        {"bytes", 1}, {"byte", 1}, {"B", 1}, {"kB", 1e3}, {"KB", 1e3}, {"MB", 1e6}, {"GB", 1e9}, {"TB", 1e12}, {"PB", 1e15},
        {"KiB", 1024.0}, {"MiB", 1048576.0}, {"GiB", 1073741824.0}, {"TiB", 1099511627776.0}};
    if (unit.empty()) return v;
    for (const auto& [name, mult] : units) {
        if (unit == name) return v * mult;
    }
    return std::nullopt;
}

std::vector<InstalledApp> parseDpkgStatus(std::string_view text) {
    std::vector<InstalledApp> out;
    std::map<std::string, std::string> f;
    std::string lastKey;
    auto flush = [&] {
        if (f.contains("Package") && f["Status"].find("install ok installed") != std::string::npos) {
            InstalledApp a;
            a.id = clean(f["Package"]);
            a.name = a.id;
            a.version = clean(f["Version"]);
            a.publisher = clean(f["Maintainer"]);
            a.source = AppSource::Dpkg;
            a.iconName = a.id;
            const auto size = f.find("Installed-Size");
            if (size != f.end()) {
                double kib = 0;
                const auto& s = size->second;
                if (std::from_chars(s.data(), s.data() + s.size(), kib).ec == std::errc{}) a.sizeBytes = bytesMetric(kib * 1024.0);
            }
            out.push_back(std::move(a));
        }
        f.clear();
        lastKey.clear();
    };
    std::istringstream in{std::string(text)};
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) {
            flush();
            continue;
        }
        if (line.front() == ' ' || line.front() == '\t') continue;  // a continuation (Description…): never a field
        const auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        auto value = line.substr(colon + 1);
        while (!value.empty() && value.front() == ' ') value.erase(value.begin());
        f[line.substr(0, colon)] = value;
    }
    flush();
    return out;
}

std::vector<InstalledApp> parseFlatpakList(std::string_view text) {
    std::vector<InstalledApp> out;
    std::istringstream in{std::string(text)};
    std::string line;
    while (std::getline(in, line)) {
        const auto c = split(line, '\t');
        if (c.size() < 6 || c[0].empty()) continue;
        InstalledApp a;
        a.id = clean(c[0]);
        a.name = clean(c[1].empty() ? c[0] : c[1]);
        a.version = clean(c[2]);
        a.publisher = clean(c[3]);  // the remote it came from (flathub…)
        a.source = AppSource::Flatpak;
        a.userInstall = c[4] == "user";
        a.iconName = a.id;
        if (const auto b = parseHumanSize(c[5])) a.sizeBytes = bytesMetric(*b);
        out.push_back(std::move(a));
    }
    return out;
}

std::vector<InstalledApp> parseRpmList(std::string_view text) {
    std::vector<InstalledApp> out;
    std::istringstream in{std::string(text)};
    std::string line;
    while (std::getline(in, line)) {
        const auto c = split(line, '\t');
        if (c.size() < 5 || c[0].empty()) continue;
        InstalledApp a;
        a.id = a.name = clean(c[0]);
        a.version = clean(c[1]);
        a.publisher = c[2] == "(none)" ? std::string() : clean(c[2]);
        a.source = AppSource::Rpm;
        a.iconName = a.id;
        double size = 0, when = 0;
        if (std::from_chars(c[3].data(), c[3].data() + c[3].size(), size).ec == std::errc{}) a.sizeBytes = bytesMetric(size);
        if (std::from_chars(c[4].data(), c[4].data() + c[4].size(), when).ec == std::errc{}) {
            a.installed = std::chrono::system_clock::time_point(std::chrono::seconds(static_cast<long long>(when)));
        }
        out.push_back(std::move(a));
    }
    return out;
}

static std::map<std::string, std::chrono::system_clock::time_point> firstInstallDates(const std::vector<std::filesystem::path>& logs) {
    std::map<std::string, std::chrono::system_clock::time_point> out;
    for (const auto& log : logs) {  // oldest first: the first "install" line seen is the real first install
        const auto t = readText(log);
        if (!t.ok()) continue;
        std::istringstream in(t.text);
        std::string line;
        while (std::getline(in, line)) {
            // 2026-10-20 14:03:09 install firefox:amd64 <none> 130.0
            if (line.size() < 28 || line[10] != ' ' || line.compare(20, 8, "install ") != 0) continue;
            int y = 0, mo = 0, d = 0, h = 0, mi = 0, s = 0;
            if (std::sscanf(line.c_str(), "%4d-%2d-%2d %2d:%2d:%2d", &y, &mo, &d, &h, &mi, &s) != 6) continue;
            auto name = line.substr(28, line.find(' ', 28) - 28);
            if (const auto colon = name.find(':'); colon != std::string::npos) name.resize(colon);  // the architecture
            const auto day = std::chrono::sys_days{std::chrono::year{y} / mo / d};
            out.emplace(name, day + std::chrono::hours(h) + std::chrono::minutes(mi) + std::chrono::seconds(s));
        }
    }
    return out;
}

std::vector<InstalledApp> InstalledAppsCollector::collect(const std::atomic<bool>& cancel, std::vector<std::string>* skipped) const {
    std::vector<InstalledApp> out;
    auto skip = [&](const std::string& why) {
        if (skipped) skipped->push_back(why);
    };

    // ---- apt / dpkg ----
    if (!cancel) {
        const auto status = readText(root_.dpkgStatus);
        if (status.ok()) {
            auto apps = parseDpkgStatus(status.text);
            const auto installs = firstInstallDates(root_.dpkgLogs);
            for (auto& a : apps) {
                if (cancel) break;
                if (const auto it = installs.find(a.id); it != installs.end()) {
                    a.installed = it->second;  // dpkg's own log: the day it was installed, not a file's timestamp
                    continue;
                }
                a.installedExact = false;
                // The install date is the package's file list (dpkg keeps no date); multi-arch packages carry an arch suffix.
                for (const auto& suffix : {std::string(".list"), std::string(":amd64.list"), std::string(":arm64.list"), std::string(":i386.list")}) {
                    if (const auto t = mtimeOf(root_.dpkgInfo / (a.id + suffix))) {
                        a.installed = t;
                        break;
                    }
                }
            }
            out.insert(out.end(), std::make_move_iterator(apps.begin()), std::make_move_iterator(apps.end()));
        } else if (fs::exists(root_.dpkgStatus.parent_path())) {
            skip("apt: " + root_.dpkgStatus.string() + " could not be read");
        }
    }

    // ---- rpm ----
    if (!cancel && fs::exists(root_.rpmDb)) {
        const auto r = run_({"rpm", "-qa", "--qf", "%{NAME}\\t%{VERSION}-%{RELEASE}\\t%{VENDOR}\\t%{SIZE}\\t%{INSTALLTIME}\\n"});
        if (r.exitCode == 0) {
            auto apps = parseRpmList(r.out);
            out.insert(out.end(), std::make_move_iterator(apps.begin()), std::make_move_iterator(apps.end()));
        } else {
            skip("rpm: " + describeFailure(r));
        }
    }

    // ---- Flatpak ----
    if (!cancel) {
        const auto r = run_({"flatpak", "list", "--app", "--columns=application,name,version,origin,installation,size"});
        if (r.exitCode == 0) {
            auto apps = parseFlatpakList(r.out);
            out.insert(out.end(), std::make_move_iterator(apps.begin()), std::make_move_iterator(apps.end()));
        } else if (r.exitCode != 127 && r.exitCode != -1) {
            skip("Flatpak: " + describeFailure(r));
        }
    }

    // ---- Snap: one .snap file per revision; the installed one is /snap/<name>/current ----
    if (!cancel) {
        std::error_code ec;
        std::map<std::string, std::pair<long long, fs::path>> best;
        for (const auto& e : fs::directory_iterator(root_.snapFiles, ec)) {
            const auto file = e.path().filename().string();
            if (!file.ends_with(".snap")) continue;
            const auto us = file.rfind('_');
            if (us == std::string::npos) continue;
            const auto name = file.substr(0, us);
            long long rev = 0;
            const auto revText = file.substr(us + 1, file.size() - us - 6);
            std::from_chars(revText.data(), revText.data() + revText.size(), rev);
            auto& b = best[name];
            if (rev >= b.first) b = {rev, e.path()};
        }
        for (const auto& [name, rev] : best) {
            if (cancel) break;
            InstalledApp a;
            a.id = a.name = clean(name);
            a.version = "rev " + std::to_string(rev.first);
            a.source = AppSource::Snap;
            a.iconName = a.id;
            std::error_code se;
            const auto sz = fs::file_size(rev.second, se);
            if (!se) a.sizeBytes = bytesMetric(static_cast<double>(sz));
            a.installed = mtimeOf(rev.second);
            a.location = (root_.snapMounts / name).string();
            out.push_back(std::move(a));
        }
        if (ec && fs::exists(root_.snapFiles.parent_path())) skip("Snap: " + root_.snapFiles.string() + " could not be read");
    }

    // ---- AppImage: executable regular *.AppImage files; symlinks are never followed ----
    if (!cancel) {
        for (const auto& dir : root_.appImageDirs) {
            std::error_code ec;
            for (const auto& e : fs::directory_iterator(dir, ec)) {
                if (cancel) break;
                const auto file = e.path().filename().string();
                if (!file.ends_with(".AppImage") && !file.ends_with(".appimage")) continue;
                struct stat st{};
                if (::lstat(e.path().c_str(), &st) != 0 || !S_ISREG(st.st_mode) || !(st.st_mode & S_IXUSR)) continue;
                InstalledApp a;
                a.id = clean(file);
                a.name = clean(file.substr(0, file.rfind('.')));
                a.source = AppSource::AppImage;
                a.sizeBytes = bytesMetric(static_cast<double>(st.st_size));
                a.installed = std::chrono::system_clock::time_point(std::chrono::seconds(st.st_mtime));
                a.location = e.path().string();
                a.userInstall = true;
                out.push_back(std::move(a));
            }
        }
    }
    return out;
}

std::string InstalledAppsCollector::locate(const InstalledApp& a) const {
    if (a.source != AppSource::Dpkg) return a.location;
    std::string text;
    for (const auto& suffix : {std::string(".list"), std::string(":amd64.list"), std::string(":arm64.list"), std::string(":i386.list")}) {
        if (const auto t = readText(root_.dpkgInfo / (a.id + suffix)); t.ok()) {
            text = t.text;
            break;
        }
    }
    if (text.empty()) return {};
    std::vector<std::string> entries;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line != "/.") entries.push_back(line);
    }
    auto has = [&](const std::string& dir) { return std::find(entries.begin(), entries.end(), dir) != entries.end(); };
    for (const auto& dir : {"/opt/" + a.id, "/usr/lib/" + a.id, "/usr/share/" + a.id, "/usr/lib/x86_64-linux-gnu/" + a.id}) {
        if (has(dir)) return dir;
    }
    for (const auto& e : entries) {  // an /opt package named differently from its directory
        if (e.starts_with("/opt/")) return e.substr(0, e.find('/', 5));
    }
    // Otherwise the deepest directory that holds everything except documentation and manuals.
    std::vector<std::string> parts;
    std::string prefix;
    bool first = true;
    for (const auto& e : entries) {
        if (e.starts_with("/usr/share/doc") || e.starts_with("/usr/share/man") || e.starts_with("/usr/share/locale") ||
            e.starts_with("/usr/share/lintian") || e.starts_with("/usr/share/bug") || e.starts_with("/usr/share/info"))
            continue;
        const auto dir = e.substr(0, e.rfind('/'));
        if (first) {
            prefix = dir;
            first = false;
            continue;
        }
        while (!prefix.empty() && dir.compare(0, prefix.size(), prefix) != 0) prefix = prefix.substr(0, prefix.rfind('/'));
    }
    const auto depth = static_cast<std::size_t>(std::count(prefix.begin(), prefix.end(), '/'));
    return depth >= 3 ? prefix : std::string();  // "/usr" or "/usr/bin" is everyone's, not this package's
}

std::string InstalledAppsCollector::uninstallHint(const InstalledApp& a) {
    switch (a.source) {
        case AppSource::Dpkg: return "sudo apt remove -- " + a.id;
        case AppSource::Rpm: return "sudo dnf remove -- " + a.id;
        case AppSource::Flatpak: return a.userInstall ? "flatpak uninstall --user -- " + a.id : "sudo flatpak uninstall -- " + a.id;
        case AppSource::Snap: return "sudo snap remove -- " + a.id;
        case AppSource::AppImage: return {};
    }
    return {};
}

std::optional<std::vector<std::string>> InstalledAppsCollector::userUninstallCommand(const InstalledApp& a) {
    if (a.source == AppSource::Flatpak && a.userInstall) return std::vector<std::string>{"flatpak", "uninstall", "--user", "-y", "--", a.id};
    return std::nullopt;
}

}  // namespace pmp::linux
