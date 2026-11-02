// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/linux/disk_scan.hpp>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <fstream>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

using namespace pmp;
using namespace pmp::linux;
namespace fs = std::filesystem;
using namespace std::chrono_literals;

namespace {

class Tree {
public:
    Tree() {
        static int n = 0;
        dir_ = fs::temp_directory_path() / ("pmp-scan-" + std::to_string(::getpid()) + "-" + std::to_string(n++));
        fs::create_directories(dir_);
    }
    ~Tree() {
        std::error_code ec;
        fs::permissions(dir_, fs::perms::owner_all, fs::perm_options::add, ec);
        for (const auto& e : fs::recursive_directory_iterator(dir_, ec)) {
            if (e.is_directory(ec) && !e.is_symlink(ec)) fs::permissions(e.path(), fs::perms::owner_all, fs::perm_options::add, ec);
        }
        fs::remove_all(dir_, ec);
    }
    const fs::path& path() const { return dir_; }
    void file(const std::string& rel, std::size_t bytes) {
        fs::create_directories((dir_ / rel).parent_path());
        std::ofstream(dir_ / rel, std::ios::binary) << std::string(bytes, 'x');
    }
    std::string rawFile(const std::string& rawName, std::size_t bytes) {
        const auto p = dir_.string() + "/" + rawName;
        const int fd = ::open(p.c_str(), O_CREAT | O_WRONLY, 0644);
        REQUIRE(fd >= 0);
        const std::string data(bytes, 'x');
        REQUIRE(::write(fd, data.data(), data.size()) == static_cast<ssize_t>(bytes));
        ::close(fd);
        return p;
    }

private:
    fs::path dir_;
};

ScanOptions opts(const Tree& t) {
    ScanOptions o;
    o.root = t.path();
    o.lowPriority = false;  // the test thread keeps its priority
    return o;
}

std::uint64_t allocated(const fs::path& p) {
    struct stat st{};
    REQUIRE(::lstat(p.c_str(), &st) == 0);
    return static_cast<std::uint64_t>(st.st_blocks) * 512;
}

const ScanNode* child(const ScanNode& n, const std::string& raw) {
    for (const auto& c : n.children) {
        if (c.rawName == raw) return &c;
    }
    return nullptr;
}

std::atomic<bool> never{false};

}  // namespace

TEST_CASE("sums allocated sizes and counts files", "[disk-scan]") {
    Tree t;
    t.file("a.txt", 5000);
    t.file("sub/b.bin", 100000);
    t.file("sub/deep/c.bin", 30);
    const auto r = DiskScanner().scan(opts(t), never);
    REQUIRE_FALSE(r.cancelled);
    REQUIRE(r.root.files == 3);
    const auto expected = allocated(t.path()) + allocated(t.path() / "a.txt") + allocated(t.path() / "sub") + allocated(t.path() / "sub/b.bin") +
                          allocated(t.path() / "sub/deep") + allocated(t.path() / "sub/deep/c.bin");
    REQUIRE(r.root.bytes == expected);
    const auto* sub = child(r.root, "sub");
    REQUIRE(sub != nullptr);
    REQUIRE(sub->isDir);
    REQUIRE(sub->files == 2);
    REQUIRE(r.root.children[0].rawName == "sub");  // largest first
}

TEST_CASE("symlinks are not followed", "[disk-scan]") {
    Tree t;
    t.file("big/data.bin", 200000);
    fs::create_directory_symlink(t.path() / "big", t.path() / "link-to-big");
    fs::create_directories(t.path() / "loop/inner");
    fs::create_directory_symlink("..", t.path() / "loop/inner/up");  // a loop if it were followed
    const auto start = std::chrono::steady_clock::now();
    const auto r = DiskScanner().scan(opts(t), never);
    REQUIRE(std::chrono::steady_clock::now() - start < 1s);
    const auto* link = child(r.root, "link-to-big");
    REQUIRE(link != nullptr);
    REQUIRE_FALSE(link->isDir);
    REQUIRE(link->bytes < 4096);  // the link itself, not what it points at
    REQUIRE(r.root.files == 3);   // data.bin and the two links
}

TEST_CASE("hard links are counted once", "[disk-scan]") {
    Tree t;
    t.file("one", 50000);
    fs::create_hard_link(t.path() / "one", t.path() / "two");
    fs::create_hard_link(t.path() / "one", t.path() / "three");
    const auto r = DiskScanner().scan(opts(t), never);
    REQUIRE(r.root.files == 1);
    REQUIRE(r.root.bytes == allocated(t.path()) + allocated(t.path() / "one"));
}

TEST_CASE("other filesystems are skipped by default", "[disk-scan]") {
    Tree t;
    t.file("here/a", 1000);
    t.file("elsewhere/b", 200000);
    auto o = opts(t);
    o.deviceOf = [](const std::string& path, dev_t actual) { return path.ends_with("/elsewhere") ? actual + 1 : actual; };
    const auto skipped = DiskScanner().scan(o, never);
    REQUIRE(skipped.skippedMounts == 1);
    REQUIRE(child(skipped.root, "elsewhere") == nullptr);
    REQUIRE(child(skipped.root, "here") != nullptr);
    o.crossFilesystems = true;  // asked for: it is included
    const auto all = DiskScanner().scan(o, never);
    REQUIRE(all.skippedMounts == 0);
    REQUIRE(child(all.root, "elsewhere") != nullptr);
    REQUIRE(all.root.bytes > skipped.root.bytes);
}

TEST_CASE("unreadable directories are counted, not fatal", "[disk-scan]") {
    if (::geteuid() == 0) SKIP("root can read everything");
    Tree t;
    t.file("ok/a", 3000);
    t.file("locked/b", 3000);
    ::chmod((t.path() / "locked").c_str(), 0);
    const auto r = DiskScanner().scan(opts(t), never);
    REQUIRE(r.inaccessible == 1);
    REQUIRE(child(r.root, "ok") != nullptr);
    REQUIRE(child(r.root, "ok")->files == 1);
    REQUIRE(r.root.files == 1);
}

TEST_CASE("a file that vanishes mid-scan is tolerated", "[disk-scan]") {
    Tree t;
    for (int i = 0; i < 20; ++i) t.file("f" + std::to_string(i), 100);
    auto o = opts(t);
    int calls = 0;
    o.beforeStat = [&](const fs::path& p) {
        if (++calls == 1) {  // delete everything else before it is looked at
            for (const auto& e : fs::directory_iterator(t.path())) {
                if (e.path() != p) fs::remove(e.path());
            }
        }
    };
    const auto r = DiskScanner().scan(o, never);
    REQUIRE(r.vanished >= 1);
    REQUIRE(r.root.files <= 20);
}

TEST_CASE("hostile names: newline, markup, invalid UTF-8, very long", "[disk-scan]") {
    Tree t;
    const std::string weird = "a\nb<b>x<b>";
    const std::string bad = std::string("bad") + "\xff\xfe" + "name";
    const std::string longName(240, 'z');
    t.rawFile(weird, 1000);
    const auto badPath = t.rawFile(bad, 90000);
    t.rawFile(longName, 10);
    const auto r = DiskScanner().scan(opts(t), never);
    const auto* w = child(r.root, weird);
    REQUIRE(w != nullptr);
    REQUIRE(w->name.find('\n') == std::string::npos);  // shown without the newline
    REQUIRE(w->name.find("<b>") != std::string::npos);  // markup is kept as characters, the page shows plain text
    const auto* b = child(r.root, bad);
    REQUIRE(b != nullptr);
    REQUIRE(b->name == "bad??name");
    REQUIRE(b->rawName == bad);  // the exact bytes survive for building paths
    REQUIRE(r.largest.size() == 3);
    REQUIRE(r.largest[0].path == badPath);  // the real path round-trips
    REQUIRE(fs::exists(r.largest[0].path));
    REQUIRE(r.largest[0].display.find("\xff") == std::string::npos);
}

TEST_CASE("children are capped and folded without changing the total", "[disk-scan]") {
    Tree t;
    for (int i = 0; i < 1000; ++i) t.file("d/f" + std::to_string(i), 10);
    auto all = opts(t);
    all.keepChildren = 5000;
    const auto full = DiskScanner().scan(all, never);
    auto o = opts(t);
    o.keepChildren = 10;
    const auto r = DiskScanner().scan(o, never);
    const auto* d = child(r.root, "d");
    REQUIRE(d != nullptr);
    REQUIRE(d->children.size() == 11);
    REQUIRE(d->children.back().isOther);
    REQUIRE(d->children.back().name == "990 other items");
    REQUIRE(d->files == 1000);
    REQUIRE(r.root.bytes == full.root.bytes);
    REQUIRE(r.root.files == full.root.files);
}

TEST_CASE("largest files list is exact", "[disk-scan]") {
    Tree t;
    for (int i = 0; i < 600; ++i) t.file("f" + std::to_string(i), 4096u * (1 + static_cast<unsigned>(i)));
    const auto r = DiskScanner().scan(opts(t), never);
    REQUIRE(r.largest.size() == 500);
    for (std::size_t i = 1; i < r.largest.size(); ++i) REQUIRE(r.largest[i - 1].bytes >= r.largest[i].bytes);
    REQUIRE(r.largest.front().path == (t.path() / "f599").string());
    // the smallest kept is at least as big as every excluded file (files f0..f99 are the 100 smallest)
    const auto smallestKept = r.largest.back().bytes;
    for (int i = 0; i < 100; ++i) REQUIRE(allocated(t.path() / ("f" + std::to_string(i))) <= smallestKept);
}

TEST_CASE("file types group by lower-cased extension", "[disk-scan]") {
    Tree t;
    t.file("a.JPG", 5000);
    t.file("b.jpg", 5000);
    t.file("Makefile", 100);
    t.file(".hidden", 100);
    t.file("archive.tar.gz", 100);
    const auto r = DiskScanner().scan(opts(t), never);
    REQUIRE(r.byType.at("jpg").first == 2);
    REQUIRE(r.byType.at("(none)").first == 2);  // Makefile and .hidden
    REQUIRE(r.byType.at("gz").first == 1);
}

TEST_CASE("cancel is prompt and the partial result is consistent", "[disk-scan]") {
    Tree t;
    for (int d = 0; d < 40; ++d) {
        for (int f = 0; f < 500; ++f) t.file("d" + std::to_string(d) + "/f" + std::to_string(f), 1);
    }
    std::atomic<bool> cancel{false};
    auto o = opts(t);
    int seen = 0;
    o.beforeStat = [&](const fs::path&) {
        if (++seen == 50) cancel = true;
    };
    const auto start = std::chrono::steady_clock::now();
    const auto r = DiskScanner().scan(o, cancel);
    REQUIRE(std::chrono::steady_clock::now() - start < 200ms);
    REQUIRE(r.cancelled);
    REQUIRE(r.root.bytes > 0);
    REQUIRE(r.root.files < 20000);
    std::uint64_t sum = r.root.children.empty() ? 0 : 0;
    for (const auto& c : r.root.children) sum += c.bytes;
    REQUIRE(r.root.bytes >= sum);  // the root's total includes its own blocks plus the children
}

TEST_CASE("the node cap bounds memory but not the total", "[disk-scan]") {
    Tree t;
    for (int d = 0; d < 100; ++d) {
        for (int f = 0; f < 50; ++f) t.file("d" + std::to_string(d) + "/f" + std::to_string(f), 10);
    }
    auto full = opts(t);
    full.keepChildren = 5000;
    const auto whole = DiskScanner().scan(full, never);
    auto o = opts(t);
    o.keepChildren = 5000;
    o.maxNodes = 1000;
    const auto r = DiskScanner().scan(o, never);
    REQUIRE(r.truncated);
    REQUIRE(r.root.bytes == whole.root.bytes);
}

TEST_CASE("a very deep tree does not overflow the stack", "[disk-scan]") {
    Tree t;
    const int fd0 = ::open(t.path().c_str(), O_RDONLY | O_DIRECTORY);
    REQUIRE(fd0 >= 0);
    int fd = fd0;
    constexpr int kDepth = 3000;
    for (int i = 0; i < kDepth; ++i) {
        REQUIRE(::mkdirat(fd, "d", 0755) == 0);
        const int next = ::openat(fd, "d", O_RDONLY | O_DIRECTORY);
        REQUIRE(next >= 0);
        ::close(fd);
        fd = next;
    }
    ::close(fd);
    auto o = opts(t);
    const auto r = DiskScanner().scan(o, never);
    REQUIRE_FALSE(r.cancelled);
    std::size_t depth = 0;
    for (const ScanNode* n = &r.root; !n->children.empty(); n = &n->children[0]) ++depth;
    REQUIRE(depth == static_cast<std::size_t>(kDepth));
    // removing it needs the same fd-relative walk
    std::vector<int> fds;
    int cur = ::open(t.path().c_str(), O_RDONLY | O_DIRECTORY);
    for (int i = 0; i < kDepth; ++i) {
        fds.push_back(cur);
        cur = ::openat(cur, "d", O_RDONLY | O_DIRECTORY);
    }
    ::close(cur);
    for (int i = kDepth - 1; i >= 0; --i) {
        ::unlinkat(fds[static_cast<std::size_t>(i)], "d", AT_REMOVEDIR);
        ::close(fds[static_cast<std::size_t>(i)]);
    }
}

TEST_CASE("an unreadable or missing root is reported, not thrown", "[disk-scan]") {
    ScanOptions o;
    o.root = "/definitely/not/a/folder";
    o.lowPriority = false;
    const auto r = DiskScanner().scan(o, never);
    REQUIRE(r.rootUnreadable);
    REQUIRE(r.inaccessible == 1);
}

TEST_CASE("progress is reported and ends with a final report", "[disk-scan]") {
    Tree t;
    for (int i = 0; i < 2000; ++i) t.file("f" + std::to_string(i), 1);
    int calls = 0;
    ScanProgress last;
    DiskScanner().scan(opts(t), never, [&](const ScanProgress& p) {
        ++calls;
        last = p;
    });
    REQUIRE(calls >= 1);
    REQUIRE(last.files == 2000);
}

TEST_CASE("mounts: pseudo filesystems are flagged and paths unescaped", "[disk-scan][mounts]") {
    const auto m = parseMountInfo(
        "22 1 8:2 / / rw,relatime shared:1 - ext4 /dev/sda2 rw\n"
        "23 22 0:21 / /proc rw,nosuid shared:5 - proc proc rw\n"
        "24 22 0:22 / /sys rw shared:6 - sysfs sysfs rw\n"
        "25 22 0:6 / /dev rw shared:2 - devtmpfs udev rw\n"
        "26 22 0:25 / /run rw shared:3 - tmpfs tmpfs rw\n"
        "27 22 0:30 / /tmp rw shared:4 - tmpfs tmpfs rw\n"
        "28 22 8:3 / /mnt/my\\040disk rw - ext4 /dev/sdb1 rw\n"
        "29 22 0:40 / /mnt/share rw - nfs4 server:/x rw\n"
        "broken\n");
    REQUIRE(m.size() == 8);
    REQUIRE_FALSE(m[0].pseudo);
    REQUIRE(m[1].pseudo);
    REQUIRE(m[2].pseudo);
    REQUIRE(m[3].pseudo);
    REQUIRE(m[4].pseudo);   // tmpfs under /run
    REQUIRE_FALSE(m[5].pseudo);  // /tmp on tmpfs is real, scannable space
    REQUIRE(m[6].mountPoint == "/mnt/my disk");
    REQUIRE_FALSE(m[7].pseudo);
    REQUIRE(m[7].fsType == "nfs4");
}
