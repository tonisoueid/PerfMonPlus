// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/linux/startup.hpp>

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

namespace pmp::linux {
namespace {

std::string slurp(const std::filesystem::path& p) {
    std::ifstream in(p);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

void spit(const std::filesystem::path& p, const std::string& text) {
    std::error_code ec;
    std::filesystem::create_directories(p.parent_path(), ec);
    std::ofstream out(p, std::ios::trunc);
    out << text;
    if (!out) throw std::runtime_error("cannot write " + p.string());
}

std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> out;
    std::stringstream ss(s);
    std::string part;
    while (std::getline(ss, part, sep)) {
        if (!part.empty()) out.push_back(part);
    }
    return out;
}

// Keys of the [Desktop Entry] group (localised keys such as Name[fr] are ignored).
std::map<std::string, std::string> parseDesktop(const std::string& text) {
    std::map<std::string, std::string> kv;
    std::istringstream in(text);
    std::string line;
    bool inEntry = false;
    while (std::getline(in, line)) {
        if (!line.empty() && line[0] == '[') {
            inEntry = line == "[Desktop Entry]";
            continue;
        }
        const auto eq = line.find('=');
        if (!inEntry || eq == std::string::npos || line[0] == '#') continue;
        const auto key = line.substr(0, eq);
        if (key.find('[') == std::string::npos) kv[key] = line.substr(eq + 1);
    }
    return kv;
}

bool isDisabled(const std::map<std::string, std::string>& kv) {
    const auto hidden = kv.find("Hidden");
    if (hidden != kv.end() && hidden->second == "true") return true;
    const auto gnome = kv.find("X-GNOME-Autostart-enabled");
    return gnome != kv.end() && gnome->second == "false";
}

bool shownIn(const std::map<std::string, std::string>& kv, const std::string& currentDesktop) {
    const auto current = split(currentDesktop, ':');
    const auto has = [&](const std::string& list) {
        for (const auto& d : split(list, ';')) {
            for (const auto& c : current) {
                if (d == c) return true;
            }
        }
        return false;
    };
    if (const auto only = kv.find("OnlyShowIn"); only != kv.end() && !has(only->second)) return false;
    if (const auto not_ = kv.find("NotShowIn"); not_ != kv.end() && has(not_->second)) return false;
    return true;
}

// Sets (or adds) a key inside [Desktop Entry].
std::string setKey(const std::string& text, const std::string& key, const std::string& value) {
    std::vector<std::string> lines;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) lines.push_back(line);
    std::size_t header = lines.size(), end = lines.size();
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (lines[i] == "[Desktop Entry]") header = i;
        else if (header != lines.size() && !lines[i].empty() && lines[i][0] == '[') {
            end = i;
            break;
        }
    }
    if (header == lines.size()) {
        lines.insert(lines.begin(), "[Desktop Entry]");
        header = 0;
        end = lines.size();
    }
    for (std::size_t i = header + 1; i < end; ++i) {
        if (lines[i].rfind(key + "=", 0) == 0) {
            lines[i] = key + "=" + value;
            std::string out;
            for (const auto& l : lines) out += l + "\n";
            return out;
        }
    }
    lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(end), key + "=" + value);
    std::string out;
    for (const auto& l : lines) out += l + "\n";
    return out;
}

StartupEntry fromDesktop(const std::filesystem::path& file, StartupSource source) {
    const auto kv = parseDesktop(slurp(file));
    const auto get = [&](const char* k) { return kv.count(k) ? kv.at(k) : std::string(); };
    StartupEntry e;
    e.id = file.filename().string();
    e.name = get("Name").empty() ? e.id : get("Name");
    e.comment = get("Comment");
    e.command = get("Exec");
    e.path = file.string();
    e.source = source;
    e.enabled = !isDisabled(kv);
    return e;
}

std::set<std::filesystem::path> desktopFiles(const std::filesystem::path& dir) {
    std::set<std::filesystem::path> out;
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(dir, ec)) {
        if (e.path().extension() == ".desktop") out.insert(e.path());
    }
    return out;
}

}  // namespace

XdgDirs xdgDirsFromEnvironment() {
    XdgDirs d;
    const char* home = std::getenv("HOME");
    const char* cfg = std::getenv("XDG_CONFIG_HOME");
    d.userConfig = cfg && *cfg ? std::filesystem::path(cfg) : std::filesystem::path(home ? home : "") / ".config";
    const char* dirs = std::getenv("XDG_CONFIG_DIRS");
    for (const auto& p : split(dirs && *dirs ? dirs : "/etc/xdg", ':')) d.systemConfig.emplace_back(p);
    const char* desktop = std::getenv("XDG_CURRENT_DESKTOP");
    d.currentDesktop = desktop ? desktop : "";
    return d;
}

constexpr const char* kOverrideKey = "X-PerfMonPlus-Override";  // set on the user override files we create

StartupCollector::StartupCollector(XdgDirs dirs, CommandRunner run) : dirs_(std::move(dirs)), run_(std::move(run)) {}

std::vector<StartupEntry> StartupCollector::collect() const {
    std::map<std::string, StartupEntry> byId;
    for (const auto& dir : dirs_.systemConfig) {  // earlier system dirs take precedence
        for (const auto& f : desktopFiles(dir / "autostart")) {
            const auto id = f.filename().string();
            if (!byId.count(id)) byId[id] = fromDesktop(f, StartupSource::XdgSystem);
        }
    }
    for (const auto& f : desktopFiles(dirs_.userConfig / "autostart")) {
        const auto id = f.filename().string();
        const bool overrides = byId.count(id) > 0;
        auto e = fromDesktop(f, overrides ? StartupSource::XdgSystem : StartupSource::XdgUser);
        byId[id] = std::move(e);
    }

    std::vector<StartupEntry> out;
    for (auto& [id, e] : byId) {
        if (shownIn(parseDesktop(slurp(e.path)), dirs_.currentDesktop)) out.push_back(std::move(e));
    }

    const auto r = run_({"systemctl", "--user", "list-unit-files", "--type=service", "--state=enabled,disabled",
                         "--output=json", "--no-pager"});
    if (r.exitCode == 0) {
        const auto j = nlohmann::json::parse(r.out, nullptr, false);
        if (j.is_array()) {
            for (const auto& u : j) {
                const std::string name = u.value("unit_file", std::string());
                const std::string state = u.value("state", std::string());
                if (name.empty() || name.find("@.service") != std::string::npos) continue;
                if (state != "enabled" && state != "disabled") continue;  // generated units are XDG entries seen again
                StartupEntry e;
                e.id = name;
                e.name = name;
                e.source = StartupSource::SystemdUser;
                e.enabled = state == "enabled";
                out.push_back(std::move(e));
            }
        }
    }
    return out;
}

void StartupCollector::setEnabled(const StartupEntry& e, bool enabled) const {
    if (e.source == StartupSource::SystemdUser) {
        const auto r = run_({"systemctl", "--user", enabled ? "enable" : "disable", "--", e.id});
        if (r.exitCode != 0) throw std::runtime_error(describeFailure(r));
        return;
    }

    const auto userPath = dirs_.userConfig / "autostart" / e.id;
    std::filesystem::path systemPath;
    for (const auto& dir : dirs_.systemConfig) {
        if (std::filesystem::exists(dir / "autostart" / e.id)) {
            systemPath = dir / "autostart" / e.id;
            break;
        }
    }

    const auto apply = [&](std::string text) {
        text = setKey(text, "Hidden", enabled ? "false" : "true");
        if (enabled && parseDesktop(text).count("X-GNOME-Autostart-enabled")) text = setKey(text, "X-GNOME-Autostart-enabled", "true");
        return text;
    };

    if (systemPath.empty()) {  // the user's own entry: edit in place
        spit(userPath, apply(slurp(userPath)));
        return;
    }
    const bool haveUser = std::filesystem::exists(userPath);
    const auto withMarker = [](std::string text) { return setKey(text, kOverrideKey, "true"); };
    if (!enabled) {  // never edit /etc: write (or update) a user override, marking the ones we create
        spit(userPath, haveUser ? apply(slurp(userPath)) : withMarker(apply(slurp(systemPath))));
        return;
    }
    const auto existing = haveUser ? slurp(userPath) : std::string();
    const bool ours = haveUser && parseDesktop(existing).count(kOverrideKey) > 0;
    if (!isDisabled(parseDesktop(slurp(systemPath))) && (!haveUser || ours)) {  // the system entry is on: drop our override
        std::error_code ec;
        std::filesystem::remove(userPath, ec);
        return;
    }
    // Either the system entry is off, or the override is the user's own: edit it, never delete it.
    spit(userPath, haveUser ? apply(existing) : withMarker(apply(slurp(systemPath))));
}

std::string desktopExecValue(const std::string& path) {
    std::string s;
    for (const char c : path) {
        if (c == '%') s += "%%";
        else s += c;
    }
    const bool reserved = s.find_first_of(" \t\n\"'\\><~|&;$*?#()`") != std::string::npos;
    if (!reserved) return s;
    std::string q = "\"";
    for (const char c : s) {
        if (c == '"' || c == '`' || c == '$' || c == '\\') q += '\\';  // the quoting rule's escape
        q += c;
    }
    q += '"';
    std::string out;  // the string-value rule applies on top: every backslash is written twice
    for (const char c : q) {
        out += c;
        if (c == '\\') out += '\\';
    }
    return out;
}

bool writeOwnAutostart(const XdgDirs& dirs, const std::string& exec, bool enabled) {
    const auto path = dirs.userConfig / "autostart" / "perfmonplus.desktop";
    std::error_code ec;
    if (!enabled) {
        std::filesystem::remove(path, ec);
        return true;
    }
    try {
        spit(path, "[Desktop Entry]\nType=Application\nName=PerfMonPlus\nExec=" + desktopExecValue(exec) + "\nX-GNOME-Autostart-enabled=true\n");
    } catch (const std::exception&) {
        return false;
    }
    return true;
}

}  // namespace pmp::linux
