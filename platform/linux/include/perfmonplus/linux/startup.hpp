// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#pragma once

#include <perfmonplus/linux/command.hpp>

#include <filesystem>
#include <string>
#include <vector>

namespace pmp::linux {

enum class StartupSource { XdgUser, XdgSystem, SystemdUser };

struct StartupEntry {
    std::string id, name, comment, command, path;
    StartupSource source = StartupSource::XdgUser;
    bool enabled = true;
};

struct XdgDirs {
    std::filesystem::path userConfig;                // $XDG_CONFIG_HOME or ~/.config
    std::vector<std::filesystem::path> systemConfig; // $XDG_CONFIG_DIRS or /etc/xdg
    std::string currentDesktop;                      // $XDG_CURRENT_DESKTOP, ':'-separated
};
XdgDirs xdgDirsFromEnvironment();

// Autostart entries: XDG .desktop files (a user file overrides a system file of the same name) and enabled or
// disabled systemd user services. A system entry is never edited: disabling it writes a user override.
class StartupCollector {
public:
    explicit StartupCollector(XdgDirs dirs,
                              CommandRunner run = runDefault);

    std::vector<StartupEntry> collect() const;
    void setEnabled(const StartupEntry& e, bool enabled) const;  // throws std::runtime_error on failure

private:
    XdgDirs dirs_;
    CommandRunner run_;
};

// ~/.config/autostart/perfmonplus.desktop (Settings > Launch at login). false removes it.
// The value of an Exec= key for one program path: quoted when it holds a reserved character, with " ` $ \ escaped
// (and then every backslash doubled, as the string-value rule requires) and % written %%.
std::string desktopExecValue(const std::string& path);
// `exec` is the plain program path; it is quoted for the file here.
bool writeOwnAutostart(const XdgDirs& dirs, const std::string& exec, bool enabled);

}  // namespace pmp::linux
