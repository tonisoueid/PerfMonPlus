// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/linux/startup.hpp>

#include "support/fake_root.hpp"

#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <sstream>

using namespace pmp::linux;

namespace {

std::string slurp(const std::filesystem::path& p) {
    std::ifstream in(p);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

constexpr const char* kGeoclue = "[Desktop Entry]\nType=Application\nName=Geoclue Demo agent\nExec=/usr/libexec/geoclue-2.0/demos/agent\n";
constexpr const char* kOrca = "[Desktop Entry]\nType=Application\nName=Screen Reader\nExec=orca\nX-GNOME-Autostart-enabled=false\n";
constexpr const char* kKde = "[Desktop Entry]\nType=Application\nName=KDE thing\nExec=kde\nOnlyShowIn=KDE;\n";
constexpr const char* kUserUnits =
    R"([{"unit_file":"app-x@autostart.service","state":"generated","preset":null},)"
    R"({"unit_file":"syncthing.service","state":"enabled","preset":null},{"unit_file":"foo@.service","state":"enabled","preset":null}])";

struct Env {
    FakeRoot fr;
    XdgDirs dirs;
    std::vector<std::vector<std::string>> calls;
    Env() {
        const auto base = fr.root().proc.parent_path();
        dirs.userConfig = base / "home/.config";
        dirs.systemConfig = {base / "etc/xdg"};
        dirs.currentDesktop = "ubuntu:GNOME";
        fr.write("etc/xdg/autostart/geoclue-demo-agent.desktop", kGeoclue);
        fr.write("etc/xdg/autostart/orca-autostart.desktop", kOrca);
        fr.write("etc/xdg/autostart/kde-only.desktop", kKde);
    }
    StartupCollector collector() {
        return StartupCollector(dirs, [this](const std::vector<std::string>& argv) {
            calls.push_back(argv);
            return CommandResult{0, argv.size() > 2 && argv[2] == "list-unit-files" ? kUserUnits : "", "", false};
        });
    }
};

const StartupEntry* find(const std::vector<StartupEntry>& v, const std::string& id) {
    for (const auto& e : v)
        if (e.id == id) return &e;
    return nullptr;
}

}  // namespace

TEST_CASE("startup entries are collected and filtered by desktop", "[startup]") {
    Env env;
    const auto entries = env.collector().collect();
    REQUIRE(find(entries, "geoclue-demo-agent.desktop")->enabled);
    REQUIRE(find(entries, "geoclue-demo-agent.desktop")->name == "Geoclue Demo agent");
    REQUIRE(find(entries, "geoclue-demo-agent.desktop")->command == "/usr/libexec/geoclue-2.0/demos/agent");
    REQUIRE(find(entries, "geoclue-demo-agent.desktop")->source == StartupSource::XdgSystem);
    REQUIRE_FALSE(find(entries, "orca-autostart.desktop")->enabled);
    REQUIRE(find(entries, "kde-only.desktop") == nullptr);
    REQUIRE(find(entries, "syncthing.service")->source == StartupSource::SystemdUser);
    REQUIRE(find(entries, "syncthing.service")->enabled);
    REQUIRE(find(entries, "app-x@autostart.service") == nullptr);
    REQUIRE(find(entries, "foo@.service") == nullptr);
}

TEST_CASE("a user entry overrides a system entry of the same name", "[startup]") {
    Env env;
    env.fr.write("home/.config/autostart/geoclue-demo-agent.desktop", "[Desktop Entry]\nName=Mine\nExec=mine\n");
    const auto entries = env.collector().collect();
    const auto* e = find(entries, "geoclue-demo-agent.desktop");
    REQUIRE(e->name == "Mine");
}

TEST_CASE("system entry disabled by user override", "[startup]") {
    Env env;
    const auto sysPath = env.dirs.systemConfig[0] / "autostart/geoclue-demo-agent.desktop";
    const auto before = slurp(sysPath);
    auto c = env.collector();
    c.setEnabled(*find(c.collect(), "geoclue-demo-agent.desktop"), false);

    const auto userPath = env.dirs.userConfig / "autostart/geoclue-demo-agent.desktop";
    REQUIRE(std::filesystem::exists(userPath));
    const auto text = slurp(userPath);
    REQUIRE(text.find("Hidden=true") != std::string::npos);
    REQUIRE(text.find("Exec=/usr/libexec/geoclue-2.0/demos/agent") != std::string::npos);
    REQUIRE(slurp(sysPath) == before);  // /etc is never touched
    REQUIRE_FALSE(find(c.collect(), "geoclue-demo-agent.desktop")->enabled);

    c.setEnabled(*find(c.collect(), "geoclue-demo-agent.desktop"), true);
    REQUIRE_FALSE(std::filesystem::exists(userPath));
    REQUIRE(find(c.collect(), "geoclue-demo-agent.desktop")->enabled);
}

TEST_CASE("enabling an entry the system disabled writes an override", "[startup]") {
    Env env;
    auto c = env.collector();
    c.setEnabled(*find(c.collect(), "orca-autostart.desktop"), true);
    const auto text = slurp(env.dirs.userConfig / "autostart/orca-autostart.desktop");
    REQUIRE(text.find("X-GNOME-Autostart-enabled=true") != std::string::npos);
    REQUIRE(find(c.collect(), "orca-autostart.desktop")->enabled);
}

TEST_CASE("a user's own entry is edited in place", "[startup]") {
    Env env;
    env.fr.write("home/.config/autostart/mine.desktop", "[Desktop Entry]\nName=Mine\nExec=mine\n");
    auto c = env.collector();
    c.setEnabled(*find(c.collect(), "mine.desktop"), false);
    REQUIRE_FALSE(find(c.collect(), "mine.desktop")->enabled);
    c.setEnabled(*find(c.collect(), "mine.desktop"), true);
    REQUIRE(find(c.collect(), "mine.desktop")->enabled);
}

TEST_CASE("user units toggle through systemctl", "[startup]") {
    Env env;
    auto c = env.collector();
    c.setEnabled(*find(c.collect(), "syncthing.service"), false);
    REQUIRE(env.calls.back() == std::vector<std::string>{"systemctl", "--user", "disable", "--", "syncthing.service"});
}

TEST_CASE("our own autostart entry", "[startup]") {
    Env env;
    REQUIRE(writeOwnAutostart(env.dirs, "/opt/pmp/perfmonplus", true));
    const auto path = env.dirs.userConfig / "autostart/perfmonplus.desktop";
    REQUIRE(slurp(path).find("Exec=/opt/pmp/perfmonplus") != std::string::npos);
    REQUIRE(writeOwnAutostart(env.dirs, "/opt/pmp/perfmonplus", false));
    REQUIRE_FALSE(std::filesystem::exists(path));
}

TEST_CASE("re-enabling keeps an override the user wrote themselves", "[startup]") {
    Env env;
    env.fr.write("home/.config/autostart/geoclue-demo-agent.desktop",
                 "[Desktop Entry]\nType=Application\nName=Geoclue Demo agent\nExec=/opt/my-geoclue --custom\n");
    auto c = env.collector();
    const auto userPath = env.dirs.userConfig / "autostart/geoclue-demo-agent.desktop";
    c.setEnabled(*find(c.collect(), "geoclue-demo-agent.desktop"), false);
    c.setEnabled(*find(c.collect(), "geoclue-demo-agent.desktop"), true);
    REQUIRE(std::filesystem::exists(userPath));
    const auto text = slurp(userPath);
    REQUIRE(text.find("Exec=/opt/my-geoclue --custom") != std::string::npos);
    REQUIRE(text.find("Hidden=false") != std::string::npos);
    REQUIRE(find(c.collect(), "geoclue-demo-agent.desktop")->enabled);
}

TEST_CASE("the Exec value is quoted and escaped as the Desktop Entry spec asks", "[startup][minor]") {
    REQUIRE(desktopExecValue("/opt/pmp/perfmonplus") == "/opt/pmp/perfmonplus");  // nothing special: as is
    REQUIRE(desktopExecValue("/opt/my app/pmp") == "\"/opt/my app/pmp\"");        // a space: quoted
    REQUIRE(desktopExecValue("/opt/50%/pmp") == "/opt/50%%/pmp");                   // % starts a field code: doubled
    // inside quotes " ` $ \ get a backslash, and the string-value rule then doubles every backslash in the file
    REQUIRE(desktopExecValue("/opt/a\"b") == "\"/opt/a\\\\\"b\"");
    REQUIRE(desktopExecValue("/opt/a$b c") == "\"/opt/a\\\\$b c\"");
    REQUIRE(desktopExecValue("/opt/a\\b c") == "\"/opt/a\\\\\\\\b c\"");
    REQUIRE(desktopExecValue("/opt/a'b") == "\"/opt/a'b\"");  // an apostrophe is reserved too: quoted, not escaped
}

TEST_CASE("our autostart entry uses the escaped value", "[startup][minor]") {
    Env env;
    REQUIRE(writeOwnAutostart(env.dirs, "/opt/my app/pmp", true));
    REQUIRE(slurp(env.dirs.userConfig / "autostart/perfmonplus.desktop").find("Exec=\"/opt/my app/pmp\"\n") != std::string::npos);
}
