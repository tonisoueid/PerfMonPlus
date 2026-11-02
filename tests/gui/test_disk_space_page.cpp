// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/gui/disk_space_page.hpp>

#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QClipboard>
#include <QEventLoop>
#include <QTimer>

#include <atomic>
#include <mutex>
#include <thread>

using namespace pmp;
using namespace pmp::gui;
using namespace pmp::linux;
using namespace std::chrono_literals;

namespace {

ScanNode leaf(const std::string& name, std::uint64_t bytes) {
    ScanNode n;
    n.name = n.rawName = name;
    n.bytes = bytes;
    n.files = 1;
    return n;
}

ScanNode dirNode(const std::string& name, std::vector<ScanNode> kids) {
    ScanNode n;
    n.name = n.rawName = name;
    n.isDir = true;
    for (auto& k : kids) {
        n.bytes += k.bytes;
        n.files += k.isDir ? k.files : 1;
    }
    std::sort(kids.begin(), kids.end(), [](const ScanNode& a, const ScanNode& b) { return a.bytes > b.bytes; });
    n.children = std::move(kids);
    return n;
}

FileEntry file(const std::string& path, std::uint64_t bytes, const std::string& ext) {
    FileEntry f;
    f.path = f.display = path;
    f.bytes = bytes;
    f.mtime = 1'700'000'000;
    f.ext = ext;
    return f;
}

ScanResult fixture(const std::string& root = "/data") {
    ScanResult r;
    auto movies = dirNode("movies", {leaf("film.mkv", 9000), leaf("clip.mp4", 3000)});
    r.root = dirNode("data", {movies, leaf("a.txt", 100), leaf("-dash.txt", 50), leaf("line\nbreak.txt", 40)});
    r.root.rawName = root;
    r.root.name = "data";
    r.largest = {file(root + "/movies/film.mkv", 9000, "mkv"), file(root + "/movies/clip.mp4", 3000, "mp4"), file(root + "/a.txt", 100, "txt"),
                 file(root + "/-dash.txt", 50, "txt"), file(root + "/line\nbreak.txt", 40, "txt")};
    r.byType = {{"mkv", {1, 9000}}, {"mp4", {1, 3000}}, {"txt", {3, 190}}};
    r.dirs = 2;
    return r;
}

template <class Cond>
bool waitFor(Cond cond, int ms = 3000) {
    QEventLoop loop;
    QTimer poll, limit;
    limit.setSingleShot(true);
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
        if (cond()) loop.quit();
    });
    QObject::connect(&limit, &QTimer::timeout, &loop, &QEventLoop::quit);
    poll.start(10);
    limit.start(ms);
    loop.exec();
    return cond();
}

struct Rec {
    std::mutex m;
    std::vector<std::vector<std::string>> commands;
    int confirms = 0;
    bool answer = true;
    int exitCode = 0;
    QStringList revealed;
};

DiskSpacePage::Hooks hooksFor(std::shared_ptr<Rec> r) {
    DiskSpacePage::Hooks h;
    h.confirm = [r](const QString&, const QString&) {
        ++r->confirms;
        return r->answer;
    };
    h.reveal = [r](const QString& d) { r->revealed << d; };
    h.run = [r](const std::vector<std::string>& argv) {
        std::lock_guard<std::mutex> lock(r->m);
        r->commands.push_back(argv);
        CommandResult c;
        c.exitCode = r->exitCode;
        c.err = r->exitCode ? "no trash here" : "";
        return c;
    };
    h.chooseFolder = [] { return QString("/picked/folder"); };
    return h;
}

DiskSpacePage::ScanFn quick(ScanResult r, std::atomic<int>* calls = nullptr) {
    return [r, calls](const ScanOptions&, const std::atomic<bool>&, const std::function<void(const ScanProgress&)>& progress) {
        if (calls) ++*calls;
        ScanProgress p;
        p.files = 3;
        p.dirs = 1;
        p.bytes = 100;
        p.current = "/data";
        if (progress) progress(p);
        return r;
    };
}

DiskSpacePage::MountsFn mounts() {
    return [] {
        std::vector<MountInfo> m(3);
        m[0] = {"/", "/dev/sda1", "ext4", 100'000'000'000, 40'000'000'000, false};
        m[1] = {"/proc", "proc", "proc", 0, 0, true};
        m[2] = {"/home", "/dev/sdb1", "ext4", 500'000'000'000, 100'000'000'000, false};
        return m;
    };
}

void scanAndWait(DiskSpacePage& page, const QString& path = "/data") {
    page.scanPath(path);
    REQUIRE(waitFor([&] { return !page.scanning() && page.result() != nullptr; }));
}

}  // namespace

TEST_CASE("pseudo filesystems are not offered; Choose folder accepts any directory", "[gui][disk]") {
    DiskSpacePage page(quick(fixture()), mounts());
    REQUIRE(page.volumeBox()->count() == 2);
    REQUIRE(page.volumeBox()->itemData(0).toString() == "/");
    REQUIRE(page.volumeBox()->itemData(1).toString() == "/home");
    REQUIRE(page.volumeBox()->itemText(0).contains("37.25 GB free"));
    auto rec = std::make_shared<Rec>();
    page.setHooks(hooksFor(rec));
    std::vector<std::string> roots;
    DiskSpacePage page2(
        [&](const ScanOptions& o, const std::atomic<bool>&, const std::function<void(const ScanProgress&)>&) {
            roots.push_back(o.root.string());
            return fixture();
        },
        mounts());
    page2.setHooks(hooksFor(rec));
    page2.chooseButton()->click();
    REQUIRE(waitFor([&] { return !roots.empty(); }));
    REQUIRE(roots[0] == "/picked/folder");
}

TEST_CASE("a scan shows progress, then results in the map, the list and the type table", "[gui][disk]") {
    DiskSpacePage page(quick(fixture()), mounts());
    page.scanButton()->click();
    REQUIRE(page.scanning());
    REQUIRE(page.scanButton()->text() == "Cancel");
    REQUIRE(waitFor([&] { return !page.scanning(); }));
    REQUIRE(page.scanButton()->text() == "Scan");
    REQUIRE(page.progressText().isEmpty());
    REQUIRE(page.summaryText() == "Scanned 5 files in 2 folders");
    REQUIRE(page.treemap()->node() != nullptr);
    REQUIRE_FALSE(page.treemap()->rects().empty());
    REQUIRE(page.largestTable()->rowCount() == 5);
    REQUIRE(page.largestTable()->item(0, 0)->text() == "film.mkv");
    REQUIRE(page.typesTable()->rowCount() == 3);
    REQUIRE(page.typesTable()->item(0, 0)->text() == "mkv");
}

TEST_CASE("the window keeps running while a scan is busy", "[gui][disk]") {
    std::atomic<bool> release{false};
    DiskSpacePage page(
        [&](const ScanOptions&, const std::atomic<bool>& cancel, const std::function<void(const ScanProgress&)>&) {
            while (!release && !cancel) std::this_thread::sleep_for(5ms);
            return fixture();
        },
        mounts());
    page.scanPath("/data");
    int ticks = 0;
    QTimer t;
    QObject::connect(&t, &QTimer::timeout, [&] { ++ticks; });
    t.start(10);
    waitFor([] { return false; }, 200);
    REQUIRE(ticks >= 5);  // the event loop was never blocked
    release = true;
    REQUIRE(waitFor([&] { return !page.scanning(); }));
}

TEST_CASE("cancel keeps the partial result and labels it partial", "[gui][disk]") {
    DiskSpacePage page(
        [&](const ScanOptions&, const std::atomic<bool>& cancel, const std::function<void(const ScanProgress&)>&) {
            while (!cancel) std::this_thread::sleep_for(5ms);
            auto r = fixture();
            r.cancelled = true;
            return r;
        },
        mounts());
    page.scanPath("/data");
    page.scanButton()->click();  // now reads Cancel
    REQUIRE(waitFor([&] { return !page.scanning(); }));
    REQUIRE(page.result()->cancelled);
    REQUIRE(page.summaryText().endsWith("results are partial"));
    REQUIRE(page.largestTable()->rowCount() == 5);
}

TEST_CASE("clicking a rectangle selects its row; double click opens a folder; up goes back", "[gui][disk]") {
    DiskSpacePage page(quick(fixture()), mounts());
    page.resize(800, 500);
    scanAndWait(page);
    const ScanNode* film = nullptr;
    const ScanNode* movies = nullptr;
    for (const auto& r : page.treemap()->rects()) {
        if (r.node && r.node->name == "movies" && r.depth == 0) movies = r.node;
        if (r.node && r.node->name == "film.mkv") film = r.node;
    }
    REQUIRE(movies != nullptr);
    page.openFolder(movies);
    REQUIRE(page.breadcrumbText() == "data › movies");
    REQUIRE(page.treemap()->node() == movies);
    REQUIRE(film != nullptr);
    emit page.treemap()->nodeSelected(film);
    REQUIRE(page.selectedPath() == "/data/movies/film.mkv");
    REQUIRE(page.largestTable()->selectionModel()->selectedRows().first().row() == 0);
    page.up();
    REQUIRE(page.breadcrumbText() == "data");
    page.up();  // already at the top: no effect
    REQUIRE(page.treemap()->node() == &page.result()->root);
}

TEST_CASE("move to trash asks first, defaults to Cancel, and runs exactly gio trash -- path", "[gui][disk]") {
    auto rec = std::make_shared<Rec>();
    DiskSpacePage page(quick(fixture()), mounts());
    page.setHooks(hooksFor(rec));
    scanAndWait(page);
    page.selectPath("/data/movies/film.mkv");
    rec->answer = false;
    page.trashButton()->click();
    REQUIRE(rec->confirms == 1);
    REQUIRE(rec->commands.empty());  // Cancel: nothing ran
    rec->answer = true;
    const auto totalBefore = page.result()->root.bytes;
    page.trashButton()->click();
    REQUIRE(rec->commands.size() == 1);
    REQUIRE(rec->commands[0] == std::vector<std::string>{"gio", "trash", "--", "/data/movies/film.mkv"});
    REQUIRE(page.result()->root.bytes == totalBefore - 9000);  // the totals shrank
    REQUIRE(page.largestTable()->rowCount() == 4);
    REQUIRE(page.largestTable()->item(0, 0)->text() == "clip.mp4");
    REQUIRE(page.typesTable()->item(0, 0)->text() == "mp4");  // mkv is gone from the largest type
    REQUIRE(page.statusText() == "Moved to the Trash.");
    REQUIRE(page.selectedPath().isEmpty());
}

TEST_CASE("odd paths go after -- unchanged", "[gui][disk]") {
    auto rec = std::make_shared<Rec>();
    DiskSpacePage page(quick(fixture()), mounts());
    page.setHooks(hooksFor(rec));
    scanAndWait(page);
    page.selectPath("/data/-dash.txt");
    page.trashSelected();
    page.selectPath(QString::fromUtf8("/data/line\nbreak.txt"));
    page.trashSelected();
    REQUIRE(rec->commands.size() == 2);
    REQUIRE(rec->commands[0] == std::vector<std::string>{"gio", "trash", "--", "/data/-dash.txt"});
    REQUIRE(rec->commands[1] == std::vector<std::string>{"gio", "trash", "--", "/data/line\nbreak.txt"});
}

TEST_CASE("a failed trash changes nothing and says why", "[gui][disk]") {
    auto rec = std::make_shared<Rec>();
    rec->exitCode = 1;
    DiskSpacePage page(quick(fixture()), mounts());
    page.setHooks(hooksFor(rec));
    scanAndWait(page);
    const auto total = page.result()->root.bytes;
    page.selectPath("/data/a.txt");
    page.trashSelected();
    REQUIRE(page.statusText().startsWith("Could not move it to the Trash: "));
    REQUIRE(page.result()->root.bytes == total);
    REQUIRE(page.largestTable()->rowCount() == 5);
}

TEST_CASE("trashing a folder removes everything under it from the list", "[gui][disk]") {
    auto rec = std::make_shared<Rec>();
    DiskSpacePage page(quick(fixture()), mounts());
    page.setHooks(hooksFor(rec));
    scanAndWait(page);
    page.selectPath("/data/movies");
    page.trashSelected();
    REQUIRE(page.largestTable()->rowCount() == 3);  // film.mkv and clip.mp4 left with their folder
    REQUIRE(page.result()->root.bytes == 100 + 50 + 40 + 0);
}

TEST_CASE("there is no permanent delete anywhere", "[gui][disk]") {
    DiskSpacePage page(quick(fixture()), mounts());
    for (auto* b : page.findChildren<QPushButton*>()) {
        REQUIRE_FALSE(b->text().contains("Delete", Qt::CaseInsensitive));
        REQUIRE_FALSE(b->text().contains("Remove", Qt::CaseInsensitive));
    }
    REQUIRE(page.trashButton()->text() == "Move to Trash…");
}

TEST_CASE("reveal opens the folder; copy puts the exact path on the clipboard", "[gui][disk]") {
    auto rec = std::make_shared<Rec>();
    DiskSpacePage page(quick(fixture()), mounts());
    page.setHooks(hooksFor(rec));
    scanAndWait(page);
    page.selectPath("/tmp");  // a real directory: revealed as itself
    page.revealButton()->click();
    REQUIRE(rec->revealed == QStringList{"/tmp"});
    page.selectPath("/data/-dash.txt");
    page.copyButton()->click();
    REQUIRE(QApplication::clipboard()->text() == "/data/-dash.txt");
}

TEST_CASE("inaccessible, skipped and vanished counts are explained", "[gui][disk]") {
    auto r = fixture();
    r.inaccessible = 14;
    r.skippedMounts = 2;
    r.vanished = 1;
    DiskSpacePage page(quick(r), mounts());
    scanAndWait(page);
    REQUIRE(page.summaryText() == "Scanned 5 files in 2 folders · 14 inaccessible · 2 mount points skipped · 1 changed while scanning");
    REQUIRE(page.summaryTip().contains("Inaccessible"));
    REQUIRE(page.summaryTip().contains("Mount points skipped"));
}

TEST_CASE("a new scan replaces the previous result and frees it", "[gui][disk]") {
    DiskSpacePage page(quick(fixture()), mounts());
    scanAndWait(page);
    std::weak_ptr<const ScanResult> old = page.result();
    REQUIRE_FALSE(old.expired());
    page.scanPath("/data");
    REQUIRE(old.expired());  // freed before the new scan even starts
    REQUIRE(waitFor([&] { return !page.scanning(); }));
    REQUIRE(page.result() != nullptr);
}

TEST_CASE("hostile file names are plain text", "[gui][disk]") {
    auto r = fixture();
    r.largest[0].path = "/data/<b>bold<b>.txt";
    r.largest[0].display = "/data/<b>bold<b>.txt";
    DiskSpacePage page(quick(r), mounts());
    scanAndWait(page);
    REQUIRE(page.largestTable()->item(0, 0)->text() == "<b>bold<b>.txt");
    REQUIRE(page.largestTable()->item(0, 2)->text() == "/data");
}

TEST_CASE("a blocked scan cannot start and says why", "[gui][disk]") {
    std::atomic<int> calls{0};
    DiskSpacePage page(quick(fixture(), &calls), mounts());
    page.setScanBlocked(true, "A benchmark is running.");
    REQUIRE_FALSE(page.scanButton()->isEnabled());
    REQUIRE(page.scanButton()->toolTip() == "A benchmark is running.");
    page.scanPath("/data");
    waitFor([] { return false; }, 100);
    REQUIRE(calls.load() == 0);
    page.setScanBlocked(false, {});
    REQUIRE(page.scanButton()->isEnabled());
}

TEST_CASE("the map describes itself for assistive technology", "[gui][disk]") {
    DiskSpacePage page(quick(fixture()), mounts());
    scanAndWait(page);
    REQUIRE(page.treemap()->accessibleName() == "Folder size map");
    REQUIRE(page.treemap()->descriptionText().startsWith("movies "));
    REQUIRE(page.treemap()->tooltipFor(&page.result()->root.children[0]).contains("% of this folder"));
}

TEST_CASE("a hostile name in the map's tooltip is escaped, not interpreted", "[gui][disk][plain]") {
    auto r = fixture();
    r.root.children[0].name = "<b>bold<b>";
    DiskSpacePage page(quick(r), mounts());
    scanAndWait(page);
    const auto tip = page.treemap()->tooltipFor(&page.result()->root.children[0]);
    REQUIRE(tip.startsWith("<span>"));  // Qt shows a tooltip as rich text when it looks like markup, so it is wrapped and escaped
    REQUIRE(tip.contains("&lt;b&gt;bold&lt;b&gt;"));
    REQUIRE_FALSE(tip.contains("<b>"));
}

TEST_CASE("trashing a file the scan folded into 'other items' still shrinks the totals", "[gui][disk][minor]") {
    auto r = fixture();
    // /data/movies has 3 kept files and the rest folded; the folded ones are known only from the largest list
    ScanNode other;
    other.isOther = true;
    other.name = other.rawName = "2 other items";
    other.bytes = 700;
    other.files = 2;
    r.root.children[0].children.push_back(other);  // inside "movies"
    r.root.children[0].bytes += 700;
    r.root.children[0].files += 2;
    r.root.bytes += 700;
    r.root.files += 2;
    r.largest.push_back(file("/data/movies/folded.bin", 500, "bin"));
    auto rec = std::make_shared<Rec>();
    DiskSpacePage page(quick(r), mounts());
    page.setHooks(hooksFor(rec));
    scanAndWait(page);
    const auto rootBefore = page.result()->root.bytes;
    page.selectPath("/data/movies/folded.bin");
    page.trashSelected();
    REQUIRE(page.statusText() == "Moved to the Trash.");  // not "scan again": the map could be corrected
    REQUIRE(page.result()->root.bytes == rootBefore - 500);
    const auto& movies = page.result()->root.children[0];
    REQUIRE(movies.children.back().bytes == 200);
    REQUIRE(movies.children.back().files == 1);
}
