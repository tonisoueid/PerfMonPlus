// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/linux/disk_scan.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>
#include <queue>
#include <set>
#include <sstream>
#include <utility>

#include <dirent.h>
#include <fcntl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace pmp::linux {
namespace {

namespace fs = std::filesystem;

constexpr std::size_t kNameBytes = 255;

std::string extensionOf(const std::string& name) {
    const auto dot = name.rfind('.');
    if (dot == std::string::npos || dot == 0 || dot + 1 >= name.size() || name.size() - dot - 1 > 16) return "(none)";
    std::string e = name.substr(dot + 1);
    for (auto& c : e) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return sanitizeText(e, 16);
}

void lowerPriority() {
    setpriority(PRIO_PROCESS, static_cast<id_t>(::syscall(SYS_gettid)), 19);
    // ioprio_set(IOPRIO_WHO_PROCESS, this thread, class IDLE): the scan only reads when nothing else wants the disk.
    constexpr int kWhoProcess = 1, kClassIdle = 3, kClassShift = 13;
    ::syscall(SYS_ioprio_set, kWhoProcess, 0, kClassIdle << kClassShift);
}

struct Frame {
    DIR* dir = nullptr;
    int fd = -1;
    ScanNode node;
    std::string path;  // exact path of this directory
};

struct FileGreater {  // min-heap on size, so the smallest of the largest is evicted first
    bool operator()(const FileEntry& a, const FileEntry& b) const { return a.bytes > b.bytes; }
};

}  // namespace

ScanResult DiskScanner::scan(const ScanOptions& opt, const std::atomic<bool>& cancel,
                             const std::function<void(const ScanProgress&)>& progress) const {
    ScanResult res;
    if (opt.lowPriority) lowerPriority();
    using clock = std::chrono::steady_clock;
    auto lastReport = clock::now();
    ScanProgress prog;
    std::size_t nodeCount = 0;
    std::set<std::pair<dev_t, ino_t>> seenLinks;
    std::priority_queue<FileEntry, std::vector<FileEntry>, FileGreater> heap;

    const std::string rootPath = opt.root.string();
    struct stat rootSt{};
    const int rootFd = ::open(rootPath.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (rootFd < 0 || ::fstat(rootFd, &rootSt) != 0) {
        if (rootFd >= 0) ::close(rootFd);
        res.rootUnreadable = true;
        res.root.name = sanitizeText(rootPath, kNameBytes);
        res.root.rawName = rootPath;
        res.root.isDir = true;
        ++res.inaccessible;
        return res;
    }
    const dev_t rootDev = rootSt.st_dev;

    std::vector<Frame> stack;
    auto openFrame = [&](int fd, const std::string& name, const std::string& path, const struct stat& st) -> bool {
        DIR* d = ::fdopendir(fd);
        if (!d) {
            ::close(fd);
            return false;
        }
        Frame f;
        f.dir = d;
        f.fd = ::dirfd(d);
        f.node.name = sanitizeText(name, kNameBytes);
        f.node.rawName = name;
        f.node.isDir = true;
        f.node.bytes = static_cast<std::uint64_t>(st.st_blocks) * 512;  // the directory's own blocks, as du counts them
        f.path = path;
        stack.push_back(std::move(f));
        ++nodeCount;
        ++prog.dirs;
        prog.bytes += static_cast<std::uint64_t>(st.st_blocks) * 512;
        return true;
    };

    // Folds a finished directory into its parent (or into the result when it is the root).
    auto finish = [&](Frame&& f) {
        ::closedir(f.dir);
        auto& kids = f.node.children;
        std::sort(kids.begin(), kids.end(), [](const ScanNode& a, const ScanNode& b) { return a.bytes > b.bytes; });
        if (kids.size() > opt.keepChildren) {
            ScanNode other;
            other.isOther = true;
            for (std::size_t i = opt.keepChildren; i < kids.size(); ++i) {
                other.bytes += kids[i].bytes;
                other.files += kids[i].isDir ? kids[i].files : 1;
            }
            const auto folded = kids.size() - opt.keepChildren;
            other.name = std::to_string(folded) + (folded == 1 ? " other item" : " other items");
            other.rawName = other.name;
            nodeCount -= std::min<std::size_t>(nodeCount, folded);
            kids.resize(opt.keepChildren);
            kids.push_back(std::move(other));
            ++nodeCount;
        }
        std::uint64_t total = f.node.bytes, files = 0;
        for (const auto& k : kids) {
            total += k.bytes;
            files += k.isDir || k.isOther ? k.files : 1;
        }
        f.node.bytes = total;
        f.node.files = files;
        if (stack.empty()) {
            res.root = std::move(f.node);
        } else {
            stack.back().node.children.push_back(std::move(f.node));
        }
    };

    openFrame(rootFd, rootPath, rootPath, rootSt);
    std::uint64_t sinceCheck = 0;
    while (!stack.empty()) {
        if (cancel.load(std::memory_order_relaxed)) {
            res.cancelled = true;
            break;
        }
        Frame& top = stack.back();
        errno = 0;
        dirent* e = ::readdir(top.dir);
        if (!e) {
            Frame done = std::move(stack.back());
            stack.pop_back();
            finish(std::move(done));
            continue;
        }
        const char* name = e->d_name;
        if (name[0] == '.' && (name[1] == 0 || (name[1] == '.' && name[2] == 0))) continue;
        const std::string nameStr = name;
        const std::string path = top.path + (top.path.size() > 1 || top.path != "/" ? "/" : "") + nameStr;
        if (opt.beforeStat) opt.beforeStat(path);
        struct stat st{};
        if (::fstatat(top.fd, name, &st, AT_SYMLINK_NOFOLLOW) != 0) {
            if (errno == ENOENT) ++res.vanished;
            else ++res.inaccessible;
            continue;
        }
        if (S_ISDIR(st.st_mode)) {
            const dev_t dev = opt.deviceOf ? opt.deviceOf(path, st.st_dev) : st.st_dev;
            if (!opt.crossFilesystems && dev != rootDev) {
                ++res.skippedMounts;
                continue;
            }
            const int fd = ::openat(top.fd, name, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
            if (fd < 0) {
                if (errno == ENOENT) ++res.vanished;
                else ++res.inaccessible;  // permission denied is counted, not fatal
                continue;
            }
            if (!openFrame(fd, nameStr, path, st)) ++res.inaccessible;
        } else {
            std::uint64_t bytes = static_cast<std::uint64_t>(st.st_blocks) * 512;
            bool counted = true;
            if (S_ISREG(st.st_mode) && st.st_nlink > 1) counted = seenLinks.emplace(st.st_dev, st.st_ino).second;  // once per scan
            if (!counted) continue;
            ++prog.files;
            prog.bytes += bytes;
            if (S_ISREG(st.st_mode)) {
                const auto ext = extensionOf(nameStr);
                auto& t = res.byType[ext];
                ++t.first;
                t.second += bytes;
                if (opt.largestFiles > 0 && (heap.size() < opt.largestFiles || bytes > heap.top().bytes)) {
                    FileEntry fe;
                    fe.path = path;
                    fe.display = sanitizeText(path, 4096);
                    fe.bytes = bytes;
                    fe.mtime = st.st_mtime;
                    fe.ext = ext;
                    heap.push(std::move(fe));
                    if (heap.size() > opt.largestFiles) heap.pop();
                }
            }
            auto& node = stack.back().node;
            if (nodeCount < opt.maxNodes) {
                ScanNode n;
                n.name = sanitizeText(nameStr, kNameBytes);
                n.rawName = nameStr;
                n.bytes = bytes;
                n.files = 1;
                node.children.push_back(std::move(n));
                ++nodeCount;
            } else {
                node.bytes += bytes;  // counted in the directory's total but not kept as a node
                res.truncated = true;
            }
        }
        if (progress && (++sinceCheck & 0xFF) == 0) {
            const auto now = clock::now();
            if (now - lastReport >= std::chrono::milliseconds(100)) {
                lastReport = now;
                prog.current = stack.empty() ? std::string() : sanitizeText(stack.back().path, 512);
                progress(prog);
            }
        }
    }
    // A cancelled scan still folds what it has read, so the partial totals are consistent.
    while (!stack.empty()) {
        Frame done = std::move(stack.back());
        stack.pop_back();
        finish(std::move(done));
    }
    res.dirs = prog.dirs;
    if (progress) {
        prog.current.clear();
        progress(prog);
    }
    while (!heap.empty()) {
        res.largest.push_back(heap.top());
        heap.pop();
    }
    std::reverse(res.largest.begin(), res.largest.end());
    return res;
}

// ---- mounts -----------------------------------------------------------------------------------------------------

namespace {

std::string unescapeMount(std::string_view s) {  // the kernel writes space, tab, newline and backslash as \040 \011 \012 \134
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 3 < s.size() + 0 && std::isdigit(static_cast<unsigned char>(s[i + 1])) && std::isdigit(static_cast<unsigned char>(s[i + 2])) &&
            std::isdigit(static_cast<unsigned char>(s[i + 3]))) {
            out.push_back(static_cast<char>((s[i + 1] - '0') * 64 + (s[i + 2] - '0') * 8 + (s[i + 3] - '0')));
            i += 3;
        } else {
            out.push_back(s[i]);
        }
    }
    return out;
}

bool isPseudo(const std::string& type, const std::string& mountPoint) {
    static const std::set<std::string> pseudo = {"proc", "sysfs", "devtmpfs", "devpts", "cgroup", "cgroup2", "securityfs", "debugfs", "tracefs",
                                                 "pstore", "bpf", "configfs", "fusectl", "mqueue", "hugetlbfs", "autofs", "binfmt_misc",
                                                 "efivarfs", "nsfs", "rpc_pipefs", "selinuxfs", "squashfs"};
    if (pseudo.contains(type)) return true;
    if (type == "tmpfs" && (mountPoint.starts_with("/dev") || mountPoint.starts_with("/sys") || mountPoint.starts_with("/run"))) return true;
    return false;
}

bool isNetwork(const std::string& type) {
    return type.starts_with("nfs") || type == "cifs" || type == "smb3" || type == "smbfs" || type.starts_with("fuse.sshfs") || type == "9p" ||
           type == "ceph" || type == "glusterfs";
}

}  // namespace

std::vector<MountInfo> parseMountInfo(std::string_view text) {
    std::vector<MountInfo> out;
    std::istringstream in{std::string(text)};
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream f(line);
        std::vector<std::string> pre;
        std::string tok;
        while (f >> tok && tok != "-") pre.push_back(tok);
        std::string type, source;
        if (tok != "-" || !(f >> type >> source) || pre.size() < 5) continue;
        MountInfo m;
        m.mountPoint = unescapeMount(pre[4]);
        m.device = unescapeMount(source);
        m.fsType = type;
        m.pseudo = isPseudo(type, m.mountPoint);
        out.push_back(std::move(m));
    }
    return out;
}

std::vector<MountInfo> readMounts(const ProcRoot& root) {
    const auto t = readText(root.proc / "self/mountinfo");
    if (!t.ok()) return {};
    auto mounts = parseMountInfo(t.text);
    for (auto& m : mounts) {
        if (m.pseudo || isNetwork(m.fsType)) continue;  // statvfs on a dead network share can block
        struct statvfs sv{};
        if (::statvfs(m.mountPoint.c_str(), &sv) == 0) {
            m.totalBytes = static_cast<std::uint64_t>(sv.f_blocks) * sv.f_frsize;
            m.freeBytes = static_cast<std::uint64_t>(sv.f_bavail) * sv.f_frsize;
        }
    }
    return mounts;
}

}  // namespace pmp::linux
