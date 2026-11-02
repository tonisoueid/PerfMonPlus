// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/linux/installed_apps.hpp>
#include <perfmonplus/linux/kernel_modules.hpp>

#include "support/fake_root.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <sys/stat.h>
#include <unistd.h>

#include <fstream>

using namespace pmp;
using namespace pmp::linux;
using Catch::Approx;

namespace {

const char* kStatus =
    "Package: bash\n"
    "Status: install ok installed\n"
    "Installed-Size: 2048\n"
    "Maintainer: Ubuntu Developers <ubuntu-devel@lists.ubuntu.com>\n"
    "Version: 5.2-1\n"
    "Description: GNU Bourne Again SHell\n"
    " Package: this continuation line must not become a field\n"
    " Status: install ok installed\n"
    "\n"
    "Package: oldpkg\n"
    "Status: deinstall ok config-files\n"
    "Installed-Size: 99\n"
    "Version: 1\n"
    "\n"
    "Package: <b>evil</b>\n"
    "Status: install ok installed\n"
    "Version: 1.0\n"
    "Description: no size here\n"
    "\n"
    "this stanza is not a stanza\n"
    "\n"
    "Package: libc6\n"
    "Status: install ok installed\n"
    "Installed-Size: 13000\n"
    "Version: 2.39\n";

CommandRunner canned(std::map<std::string, CommandResult> byTool) {
    return [byTool](const std::vector<std::string>& argv) {
        const auto it = byTool.find(argv.front());
        if (it == byTool.end()) {
            CommandResult r;
            r.exitCode = 127;
            return r;
        }
        return it->second;
    };
}

CommandResult ok(std::string out) {
    CommandResult r;
    r.exitCode = 0;
    r.out = std::move(out);
    return r;
}

}  // namespace

TEST_CASE("dpkg: installed packages only, size in bytes", "[apps]") {
    const auto apps = parseDpkgStatus(kStatus);
    REQUIRE(apps.size() == 3);  // oldpkg (config-files) and the junk stanza are out
    REQUIRE(apps[0].id == "bash");
    REQUIRE(apps[0].version == "5.2-1");
    REQUIRE(apps[0].publisher == "Ubuntu Developers <ubuntu-devel@lists.ubuntu.com>");
    REQUIRE(*apps[0].sizeBytes.value == Approx(2048 * 1024.0));
    REQUIRE(apps[0].source == AppSource::Dpkg);
    REQUIRE(*apps[2].sizeBytes.value == Approx(13000 * 1024.0));
}

TEST_CASE("dpkg: continuation lines never become fields", "[apps]") {
    const auto apps = parseDpkgStatus(kStatus);
    REQUIRE(apps[0].id == "bash");  // the " Package: ..." line inside Description did not rename it
}

TEST_CASE("dpkg: unknown size is Unavailable, never 0, and names are sanitised", "[apps]") {
    const auto apps = parseDpkgStatus(kStatus);
    REQUIRE(apps[1].id == "<b>evil</b>");  // markup is data: it is shown as plain text by the page
    REQUIRE(apps[1].sizeBytes.availability == Availability::Unsupported);
    REQUIRE_FALSE(apps[1].sizeBytes.value.has_value());
    const auto ctl = parseDpkgStatus("Package: a\x01\x02" "b\nStatus: install ok installed\nVersion: 1\n");
    REQUIRE(ctl.size() == 1);
    REQUIRE(ctl[0].id == "a??b");
}

TEST_CASE("dpkg: a truncated final stanza without a blank line still counts", "[apps]") {
    const auto apps = parseDpkgStatus("Package: z\nStatus: install ok installed\nVersion: 3");
    REQUIRE(apps.size() == 1);
    REQUIRE(apps[0].version == "3");
}

TEST_CASE("human sizes parse as Flatpak prints them", "[apps]") {
    REQUIRE(*parseHumanSize("1.2 GB") == Approx(1.2e9));
    REQUIRE(*parseHumanSize("340.5 MB") == Approx(340.5e6));
    REQUIRE(*parseHumanSize("12 kB") == Approx(12e3));
    REQUIRE(*parseHumanSize("1,5 GB") == Approx(1.5e9));  // a comma decimal separator
    REQUIRE(*parseHumanSize("100 bytes") == 100);
    REQUIRE(*parseHumanSize("2 GiB") == Approx(2147483648.0));
    REQUIRE_FALSE(parseHumanSize("lots").has_value());
    REQUIRE_FALSE(parseHumanSize("").has_value());
    REQUIRE_FALSE(parseHumanSize("3 parsecs").has_value());
}

TEST_CASE("flatpak: user and system installs are both listed", "[apps]") {
    const auto apps = parseFlatpakList(
        "org.gimp.GIMP\tGIMP\t2.10\tflathub\tsystem\t850.5 MB\n"
        "io.x.Tool\tTool\t1.0\tflathub\tuser\t12 kB\n"
        "broken line\n"
        "org.nosize.App\tNoSize\t1\tflathub\tsystem\t?\n");
    REQUIRE(apps.size() == 3);
    REQUIRE(apps[0].name == "GIMP");
    REQUIRE_FALSE(apps[0].userInstall);
    REQUIRE(*apps[0].sizeBytes.value == Approx(850.5e6));
    REQUIRE(apps[1].userInstall);
    REQUIRE(apps[2].sizeBytes.availability == Availability::Unsupported);
    REQUIRE(apps[0].iconName == "org.gimp.GIMP");
}

TEST_CASE("rpm rows parse", "[apps]") {
    const auto apps = parseRpmList("bash\t5.2-1.fc40\tFedora Project\t7000000\t1700000000\nfoo\t1\t(none)\t10\tx\n");
    REQUIRE(apps.size() == 2);
    REQUIRE(apps[0].publisher == "Fedora Project");
    REQUIRE(*apps[0].sizeBytes.value == 7000000);
    REQUIRE(apps[0].installed.has_value());
    REQUIRE(apps[1].publisher.empty());
    REQUIRE_FALSE(apps[1].installed.has_value());
}

TEST_CASE("collect merges every source and notes what it skipped", "[apps]") {
    FakeRoot fr;
    fr.write("var/lib/dpkg/status", kStatus);
    fr.write("var/lib/dpkg/info/bash.list", "/bin/bash\n");
    fr.write("var/lib/snapd/snaps/core_100.snap", std::string(1000, 'x'));
    fr.write("var/lib/snapd/snaps/core_105.snap", std::string(2000, 'x'));
    fr.write("var/lib/snapd/snaps/bare_5.snap", std::string(10, 'x'));
    auto root = fr.root();
    AppsRoot ar;
    ar.dpkgStatus = root.proc.parent_path() / "var/lib/dpkg/status";
    ar.dpkgInfo = root.proc.parent_path() / "var/lib/dpkg/info";
    ar.snapFiles = root.proc.parent_path() / "var/lib/snapd/snaps";
    ar.snapMounts = "/snap";
    ar.rpmDb = root.proc.parent_path() / "no-rpm";
    InstalledAppsCollector c(ar, canned({{"flatpak", ok("org.x.App\tApp\t1\tflathub\tuser\t1 MB\n")}}));
    std::atomic<bool> cancel{false};
    std::vector<std::string> skipped;
    const auto apps = c.collect(cancel, &skipped);
    int dpkg = 0, snap = 0, flat = 0;
    const InstalledApp* core = nullptr;
    const InstalledApp* bash = nullptr;
    for (const auto& a : apps) {
        dpkg += a.source == AppSource::Dpkg;
        snap += a.source == AppSource::Snap;
        flat += a.source == AppSource::Flatpak;
        if (a.source == AppSource::Snap && a.id == "core") core = &a;
        if (a.source == AppSource::Dpkg && a.id == "bash") bash = &a;
    }
    REQUIRE(dpkg == 3);
    REQUIRE(snap == 2);
    REQUIRE(flat == 1);
    REQUIRE(core != nullptr);
    REQUIRE(core->version == "rev 105");          // the newest revision is the installed one
    REQUIRE(*core->sizeBytes.value == 2000);
    REQUIRE(bash->installed.has_value());          // from the package's file list
    REQUIRE(skipped.empty());
}

TEST_CASE("a failing tool is skipped with a note, not an error", "[apps]") {
    AppsRoot ar;
    ar.dpkgStatus = "/nonexistent/status";
    ar.snapFiles = "/nonexistent/snaps";
    CommandResult bad;
    bad.exitCode = 1;
    bad.err = "flatpak: boom";
    InstalledAppsCollector c(ar, canned({{"flatpak", bad}}));
    std::atomic<bool> cancel{false};
    std::vector<std::string> skipped;
    const auto apps = c.collect(cancel, &skipped);
    REQUIRE(apps.empty());
    REQUIRE(skipped.size() == 1);
    REQUIRE(skipped[0].starts_with("Flatpak:"));
}

TEST_CASE("cancel stops promptly", "[apps]") {
    FakeRoot fr;
    fr.write("var/lib/dpkg/status", kStatus);
    AppsRoot ar;
    ar.dpkgStatus = fr.root().proc.parent_path() / "var/lib/dpkg/status";
    int calls = 0;
    InstalledAppsCollector c(ar, [&](const std::vector<std::string>&) {
        ++calls;
        return CommandResult{};
    });
    std::atomic<bool> cancel{true};
    REQUIRE(c.collect(cancel).empty());
    REQUIRE(calls == 0);
}

TEST_CASE("appimage: only executable regular files, symlinks are not followed", "[apps]") {
    const auto dir = std::filesystem::temp_directory_path() / ("pmp-appimg-" + std::to_string(::getpid()));
    std::filesystem::create_directories(dir);
    auto make = [&](const std::string& name, mode_t mode, std::size_t size) {
        std::ofstream(dir / name) << std::string(size, 'x');
        ::chmod((dir / name).c_str(), mode);
    };
    make("Good-1.0.AppImage", 0755, 5000);
    make("NotExec.AppImage", 0644, 10);
    make("readme.txt", 0755, 10);
    std::filesystem::create_symlink(dir / "Good-1.0.AppImage", dir / "Link.AppImage");
    AppsRoot ar;
    ar.dpkgStatus = "/nonexistent";
    ar.snapFiles = "/nonexistent";
    ar.appImageDirs = {dir, dir / "missing"};
    InstalledAppsCollector c(ar, canned({}));
    std::atomic<bool> cancel{false};
    const auto apps = c.collect(cancel);
    REQUIRE(apps.size() == 1);
    REQUIRE(apps[0].name == "Good-1.0");
    REQUIRE(apps[0].source == AppSource::AppImage);
    REQUIRE(*apps[0].sizeBytes.value == 5000);
    REQUIRE(apps[0].userInstall);
    std::filesystem::remove_all(dir);
}

TEST_CASE("uninstall hints are plain text and user flatpaks are the only thing PerfMonPlus runs", "[apps]") {
    InstalledApp a;
    a.id = "bash";
    a.source = AppSource::Dpkg;
    REQUIRE(InstalledAppsCollector::uninstallHint(a) == "sudo apt remove -- bash");
    REQUIRE_FALSE(InstalledAppsCollector::userUninstallCommand(a).has_value());
    a.source = AppSource::Snap;
    REQUIRE(InstalledAppsCollector::uninstallHint(a) == "sudo snap remove -- bash");
    REQUIRE_FALSE(InstalledAppsCollector::userUninstallCommand(a).has_value());
    a.source = AppSource::Flatpak;
    a.id = "org.x.App";
    a.userInstall = false;
    REQUIRE(InstalledAppsCollector::uninstallHint(a) == "sudo flatpak uninstall -- org.x.App");
    REQUIRE_FALSE(InstalledAppsCollector::userUninstallCommand(a).has_value());
    a.userInstall = true;
    REQUIRE(InstalledAppsCollector::uninstallHint(a) == "flatpak uninstall --user -- org.x.App");
    const auto cmd = InstalledAppsCollector::userUninstallCommand(a);
    REQUIRE(cmd.has_value());
    REQUIRE(*cmd == std::vector<std::string>{"flatpak", "uninstall", "--user", "-y", "--", "org.x.App"});
    a.source = AppSource::AppImage;
    REQUIRE(InstalledAppsCollector::uninstallHint(a).empty());
}

TEST_CASE("kernel modules parse", "[apps][modules]") {
    const auto m = parseModules(
        "snd_hda_intel 61440 3 snd_hda_codec,snd_hda_core, Live 0xffffffffc0a00000\n"
        "bridge 425984 0 - Live 0x0000000000000000\n"
        "bad line\n"
        "huge 99999999999999999999 1 - Live 0x0\n"
        "loading 4096 0 - Loading 0x0\n");
    REQUIRE(m.size() == 3);
    REQUIRE(m[0].name == "snd_hda_intel");
    REQUIRE(m[0].sizeBytes == 61440);
    REQUIRE(m[0].refCount == 3);
    REQUIRE(m[0].usedBy == std::vector<std::string>{"snd_hda_codec", "snd_hda_core"});
    REQUIRE(m[0].state == "Live");
    REQUIRE(m[1].usedBy.empty());
    REQUIRE(m[2].state == "Loading");
}

TEST_CASE("unreadable /proc/modules gives an empty list", "[apps][modules]") {
    FakeRoot fr;
    REQUIRE(readKernelModules(fr.root()).empty());
    fr.write("proc/modules", "bridge 425984 0 - Live 0x0\n");
    REQUIRE(readKernelModules(fr.root()).size() == 1);
}

TEST_CASE("an apt package's folder is worked out from its file list", "[apps][minor]") {
    FakeRoot fr;
    auto root = fr.root();
    AppsRoot ar;
    ar.dpkgInfo = root.proc.parent_path() / "info";
    InstalledAppsCollector c(ar, canned({}));
    auto app = [](const std::string& id) {
        InstalledApp a;
        a.id = id;
        a.source = AppSource::Dpkg;
        return a;
    };
    fr.write("info/firefox.list", "/.\n/usr\n/usr/bin\n/usr/bin/firefox\n/usr/lib\n/usr/lib/firefox\n/usr/lib/firefox/firefox\n/usr/share/doc/firefox\n");
    REQUIRE(c.locate(app("firefox")) == "/usr/lib/firefox");
    fr.write("info/vendor-app.list", "/.\n/opt\n/opt/VendorApp\n/opt/VendorApp/bin\n/opt/VendorApp/bin/run\n/usr/bin\n/usr/bin/vendor\n");
    REQUIRE(c.locate(app("vendor-app")) == "/opt/VendorApp");  // /opt/<dir> even when it is named differently
    fr.write("info/tool.list", "/usr/lib/tool-plugins/a.so\n/usr/lib/tool-plugins/b.so\n/usr/share/man/man1/tool.1.gz\n");
    REQUIRE(c.locate(app("tool")) == "/usr/lib/tool-plugins");  // the folder all its files share, manuals aside
    fr.write("info/coreutils.list", "/usr/bin/ls\n/usr/bin/cp\n/etc/foo\n");
    REQUIRE(c.locate(app("coreutils")).empty());  // spread across the system: no folder of its own
    REQUIRE(c.locate(app("missing")).empty());
    fr.write("info/libfoo:amd64.list", "/usr/lib/x86_64-linux-gnu/libfoo/x.so\n");
    REQUIRE(c.locate(app("libfoo")) == "/usr/lib/x86_64-linux-gnu/libfoo");  // multi-arch list names
    InstalledApp snap;
    snap.source = AppSource::Snap;
    snap.location = "/snap/firefox";
    REQUIRE(c.locate(snap) == "/snap/firefox");  // other sources already know theirs
}

TEST_CASE("apt install dates come from dpkg's own log when it has the package", "[apps][minor]") {
    FakeRoot fr;
    auto root = fr.root();
    const auto base = root.proc.parent_path();
    fr.write("var/lib/dpkg/status", kStatus);
    fr.write("var/lib/dpkg/info/bash.list", "/bin/bash\n");  // its file's timestamp is "now": the estimate
    fr.write("var/log/dpkg.log.1", "2026-01-02 03:04:05 install bash:amd64 <none> 5.2-1\n2026-01-02 03:04:09 status installed bash:amd64 5.2-1\n");
    fr.write("var/log/dpkg.log", "2026-09-30 10:00:00 upgrade bash:amd64 5.2-1 5.2-2\n2026-10-01 11:00:00 install libc6:amd64 <none> 2.39\n");
    AppsRoot ar;
    ar.dpkgStatus = base / "var/lib/dpkg/status";
    ar.dpkgInfo = base / "var/lib/dpkg/info";
    ar.snapFiles = "/nonexistent";
    ar.dpkgLogs = {base / "var/log/dpkg.log.1", base / "var/log/dpkg.log"};
    InstalledAppsCollector c(ar, canned({}));
    std::atomic<bool> cancel{false};
    const auto apps = c.collect(cancel);
    const InstalledApp *bash = nullptr, *libc = nullptr, *evil = nullptr;
    for (const auto& a : apps) {
        if (a.id == "bash") bash = &a;
        if (a.id == "libc6") libc = &a;
        if (a.id == "<b>evil</b>") evil = &a;
    }
    REQUIRE(bash != nullptr);
    REQUIRE(bash->installedExact);
    REQUIRE(std::chrono::duration_cast<std::chrono::seconds>(bash->installed->time_since_epoch()).count() == 1767323045);  // 2026-01-02 03:04:05 UTC
    REQUIRE(libc->installedExact);                          // found in the newer log
    REQUIRE_FALSE(evil->installedExact);                    // not in either log, no file list: an unknown date
}
