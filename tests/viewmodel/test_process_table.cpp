// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/vm/process_table.hpp>

#include <catch2/catch_test_macros.hpp>

#include <chrono>

using namespace pmp;
using namespace pmp::vm;
using namespace std::chrono_literals;

namespace {

MonoTime at(int s) { return MonoTime{} + std::chrono::seconds(s); }

struct B {  // a tiny record builder
    ProcessRecord r;
    B(int pid, std::string name, ProcessKind kind = ProcessKind::Background, double cpu = 1, double mem = 100) {
        r.key = {pid, static_cast<std::uint64_t>(pid) * 10};
        r.name = std::move(name);
        r.user = "dave";
        r.state = 'S';
        r.kind = kind;
        r.cpu = Metric::measured(cpu, Unit::Percent);
        r.memFootprint = Metric::measured(mem, Unit::Bytes);
        r.memPrivate = Metric::measured(mem / 2, Unit::Bytes);
        r.threads = Metric::measured(1, Unit::Count);
        r.diskReadBps = Metric::measured(0, Unit::BytesPerSec);
        r.diskWriteBps = Metric::measured(0, Unit::BytesPerSec);
        r.gpu = Metric::measured(0, Unit::Percent);
    }
    B& app(std::string scope) {
        r.appScope = std::move(scope);
        r.kind = ProcessKind::App;
        return *this;
    }
    B& user(std::string u) {
        r.user = std::move(u);
        return *this;
    }
    B& parent(int pid) {
        r.parent = ProcessKey{pid, static_cast<std::uint64_t>(pid) * 10};
        return *this;
    }
    B& cpu(double v) {
        r.cpu = Metric::measured(v, Unit::Percent);
        return *this;
    }
    B& noCpu() {
        r.cpu = Metric::notMeasured(Availability::Pending, Unit::Percent);
        return *this;
    }
    B& mem(double v) {
        r.memFootprint = Metric::measured(v, Unit::Bytes);
        return *this;
    }
    B& exited(int t) {
        r.exitedAt = at(t);
        r.state = 'S';
        return *this;
    }
    operator ProcessRecord() const { return r; }
};

Frame frameOf(std::vector<ProcessRecord> ps, int t = 100) {
    Frame f;
    f.mono = at(t);
    f.processes.processes = std::move(ps);
    return f;
}

CellContext cc() {
    CellContext c;
    c.totalMemoryBytes = 10000;
    c.wallNow = std::chrono::system_clock::time_point{} + 1700000000s;
    return c;
}

ProcessTableOptions opts(ProcessView v = ProcessView::Flat) {
    ProcessTableOptions o;
    o.view = v;
    o.sortBy = ProcessColumn::Pid;
    o.descending = false;
    return o;
}

std::string name(const TableRow& r) { return r.cells[static_cast<int>(ProcessColumn::Name)]; }

std::vector<std::string> names(const std::vector<TableRow>& rows) {
    std::vector<std::string> out;
    for (const auto& r : rows) out.push_back(r.type == TableRow::Type::Header ? r.title : name(r));
    return out;
}

const TableRow* byName(const std::vector<TableRow>& rows, const std::string& n) {
    for (const auto& r : rows)
        if (r.type != TableRow::Type::Header && name(r) == n) return &r;
    return nullptr;
}

}  // namespace

TEST_CASE("grouped view counts applications, background and system processes", "[process-table]") {
    const auto f = frameOf({B(1, "systemd", ProcessKind::System), B(2, "kthreadd", ProcessKind::System),
                            B(3, "gnome-shell", ProcessKind::Background), B(4, "gedit", ProcessKind::App),
                            B(5, "ff", ProcessKind::App).app("app-firefox.scope"), B(6, "ff-web", ProcessKind::App).app("app-firefox.scope")});
    auto o = opts(ProcessView::Grouped);
    const auto rows = buildProcessTable(f, o, cc());
    const auto n = names(rows);
    REQUIRE(n[0] == "Applications (2)");  // gedit (standalone) and the firefox group
    REQUIRE(rows[0].type == TableRow::Type::Header);
    REQUIRE(byName(rows, "ff")->type == TableRow::Type::Group);
    REQUIRE(std::find(n.begin(), n.end(), "Background processes (1)") != n.end());
    REQUIRE(std::find(n.begin(), n.end(), "System processes (2)") != n.end());
}

TEST_CASE("an app row aggregates its members", "[process-table]") {
    const auto f = frameOf({B(5, "ff", ProcessKind::App, 3, 1000).app("s"), B(6, "ff-web", ProcessKind::App, 4, 2000).app("s").parent(5)});
    const auto rows = buildProcessTable(f, opts(ProcessView::Grouped), cc());
    const auto* g = byName(rows, "ff");
    REQUIRE(g);
    REQUIRE(g->type == TableRow::Type::Group);
    REQUIRE(g->cells[static_cast<int>(ProcessColumn::Cpu)] == "7.0%");
    REQUIRE(g->cells[static_cast<int>(ProcessColumn::Memory)] == "1 KB");  // 500 + 1000 private bytes
    REQUIRE(g->cells[static_cast<int>(ProcessColumn::Threads)] == "2");
    REQUIRE(g->expandable);
    REQUIRE_FALSE(g->expanded);
}

TEST_CASE("app memory is the sum of private memory or a dash", "[process-table]") {
    auto a = B(5, "ff", ProcessKind::App).app("s").r;
    auto b = B(6, "ff-web", ProcessKind::App).app("s").parent(5).r;
    b.memPrivate = Metric::notMeasured(Availability::Restricted, Unit::Bytes);
    const auto rows = buildProcessTable(frameOf({a, b}), opts(ProcessView::Grouped), cc());
    REQUIRE(byName(rows, "ff")->cells[static_cast<int>(ProcessColumn::Memory)] == "Unavailable");
}

TEST_CASE("expanding an app lists its members at depth 2", "[process-table]") {
    const auto f = frameOf({B(5, "ff", ProcessKind::App).app("s"), B(6, "ff-web", ProcessKind::App).app("s").parent(5)});
    auto o = opts(ProcessView::Grouped);
    REQUIRE(byName(buildProcessTable(f, o, cc()), "ff-web") == nullptr);
    o.toggled.insert("a:s");
    const auto rows = buildProcessTable(f, o, cc());
    REQUIRE(byName(rows, "ff-web")->depth == 2);
    REQUIRE(byName(rows, "ff")->expanded);
}

TEST_CASE("collapsing a header hides its rows", "[process-table]") {
    const auto f = frameOf({B(1, "systemd", ProcessKind::System)});
    auto o = opts(ProcessView::Grouped);
    o.toggled.insert("h:sys");
    const auto rows = buildProcessTable(f, o, cc());
    REQUIRE(rows.size() == 1);
    REQUIRE_FALSE(rows[0].expanded);
}

TEST_CASE("tree view nests children under parents and expands only the roots", "[process-table]") {
    const auto f = frameOf({B(1, "init"), B(2, "child").parent(1), B(3, "grandchild").parent(2), B(4, "other").parent(1)});
    auto rows = buildProcessTable(f, opts(ProcessView::Tree), cc());
    REQUIRE(names(rows) == std::vector<std::string>{"init", "child", "other"});  // the grandchild is collapsed
    REQUIRE(byName(rows, "child")->depth == 1);
    REQUIRE(byName(rows, "child")->expandable);
    auto o = opts(ProcessView::Tree);
    o.toggled.insert(byName(rows, "child")->id);
    rows = buildProcessTable(f, o, cc());
    REQUIRE(names(rows) == std::vector<std::string>{"init", "child", "grandchild", "other"});
    REQUIRE(byName(rows, "grandchild")->depth == 2);
}

TEST_CASE("flat view lists every process", "[process-table]") {
    const auto f = frameOf({B(1, "a"), B(2, "b").parent(1), B(3, "c", ProcessKind::App).app("s")});
    const auto rows = buildProcessTable(f, opts(ProcessView::Flat), cc());
    REQUIRE(names(rows) == std::vector<std::string>{"a", "b", "c"});
    for (const auto& r : rows) REQUIRE(r.depth == 0);
}

TEST_CASE("by user groups under user headers", "[process-table]") {
    const auto f = frameOf({B(1, "a").user("root"), B(2, "b").user("dave"), B(3, "c").user("dave")});
    const auto rows = buildProcessTable(f, opts(ProcessView::ByUser), cc());
    REQUIRE(names(rows) == std::vector<std::string>{"dave (2)", "b", "c", "root (1)", "a"});
}

TEST_CASE("filter by name, user and pid is case-insensitive", "[process-table]") {
    const auto f = frameOf({B(11, "Firefox"), B(2, "bash").user("alice"), B(311, "zsh")});
    auto o = opts();
    o.filter = "FIRE";
    REQUIRE(names(buildProcessTable(f, o, cc())) == std::vector<std::string>{"Firefox"});
    o.filter = "ALI";
    REQUIRE(names(buildProcessTable(f, o, cc())) == std::vector<std::string>{"bash"});
    o.filter = "11";
    REQUIRE(names(buildProcessTable(f, o, cc())) == std::vector<std::string>{"Firefox", "zsh"});  // pids 11 and 311
}

TEST_CASE("a matching child keeps its ancestors visible and expanded", "[process-table]") {
    const auto f = frameOf({B(1, "init"), B(2, "mid").parent(1), B(3, "needle").parent(2), B(4, "hay").parent(1)});
    auto o = opts(ProcessView::Tree);
    o.filter = "needle";
    const auto rows = buildProcessTable(f, o, cc());
    REQUIRE(names(rows) == std::vector<std::string>{"init", "mid", "needle"});
    REQUIRE(byName(rows, "init")->expanded);
}

TEST_CASE("a matching app member shows its group expanded", "[process-table]") {
    const auto f = frameOf({B(5, "ff", ProcessKind::App).app("s"), B(6, "renderer", ProcessKind::App).app("s").parent(5)});
    auto o = opts(ProcessView::Grouped);
    o.filter = "render";
    const auto rows = buildProcessTable(f, o, cc());
    REQUIRE(byName(rows, "renderer"));
    REQUIRE(byName(rows, "ff")->expanded);
    REQUIRE(rows[0].title == "Applications (1)");
}

TEST_CASE("regex characters in the filter are literal", "[process-table]") {
    const auto f = frameOf({B(1, "a.b"), B(2, "axb")});
    auto o = opts();
    o.filter = "a.b";
    REQUIRE(names(buildProcessTable(f, o, cc())) == std::vector<std::string>{"a.b"});
    o.filter = ".*";
    REQUIRE(buildProcessTable(f, o, cc()).empty());
}

TEST_CASE("sort is stable and unmeasured sorts last in both directions", "[process-table]") {
    const auto f = frameOf({B(3, "c").cpu(5), B(1, "a").cpu(5), B(2, "b").noCpu(), B(4, "d").cpu(9)});
    auto o = opts();
    o.sortBy = ProcessColumn::Cpu;
    o.descending = true;
    REQUIRE(names(buildProcessTable(f, o, cc())) == std::vector<std::string>{"d", "a", "c", "b"});  // ties by pid
    o.descending = false;
    REQUIRE(names(buildProcessTable(f, o, cc())) == std::vector<std::string>{"a", "c", "d", "b"});
}

TEST_CASE("sort by name is case-insensitive", "[process-table]") {
    const auto f = frameOf({B(1, "bravo"), B(2, "Alpha"), B(3, "charlie")});
    auto o = opts();
    o.sortBy = ProcessColumn::Name;
    REQUIRE(names(buildProcessTable(f, o, cc())) == std::vector<std::string>{"Alpha", "bravo", "charlie"});
}

TEST_CASE("tombstones appear for the retention period and then vanish", "[process-table]") {
    const auto f = frameOf({B(1, "live"), B(2, "gone").exited(96)}, 100);
    auto o = opts();
    o.tombstoneSeconds = 10;
    auto rows = buildProcessTable(f, o, cc());
    const auto* t = byName(rows, "gone");
    REQUIRE(t);
    REQUIRE(t->tombstone);
    REQUIRE(t->cells[static_cast<int>(ProcessColumn::Status)] == "exited");
    o.tombstoneSeconds = 3;  // it exited 4 s ago
    REQUIRE(byName(buildProcessTable(f, o, cc()), "gone") == nullptr);
    o.tombstoneSeconds = 0;
    REQUIRE(byName(buildProcessTable(f, o, cc()), "gone") == nullptr);
}

TEST_CASE("a tombstone explains its exit", "[process-table]") {
    auto gone = B(2, "gone").exited(96).r;
    gone.started = std::chrono::system_clock::time_point{} + (1700000000s - 4s - 12s);
    const auto rows = buildProcessTable(frameOf({gone}, 100), opts(), cc());
    REQUIRE(rows[0].tooltip == "Exited 4 s ago · ran 0:00:12 · exit status unavailable");
}

TEST_CASE("a pinned tombstone stays after the provider drops it", "[process-table]") {
    auto gone = B(2, "gone").exited(50).r;
    ProcessTableOptions o = opts();
    o.tombstoneSeconds = 10;
    o.pinned = {gone};
    const auto rows = buildProcessTable(frameOf({B(1, "live")}, 100), o, cc());
    const auto* t = byName(rows, "gone");
    REQUIRE(t);
    REQUIRE(t->pinned);
    REQUIRE(t->tombstone);
}

TEST_CASE("a recycled pid makes two rows", "[process-table]") {
    auto old = B(7, "old").exited(98).r;
    auto fresh = B(7, "new").r;
    fresh.key.startTicks = 999;
    const auto rows = buildProcessTable(frameOf({old, fresh}), opts(), cc());
    REQUIRE(rows.size() == 2);
    REQUIRE(rows[0].id != rows[1].id);
}

TEST_CASE("heat is a fraction for cpu and memory and -1 for others", "[process-table]") {
    const auto f = frameOf({B(1, "a").cpu(25).mem(5000)});
    const auto rows = buildProcessTable(f, opts(), cc());
    REQUIRE(rows[0].heat[static_cast<int>(ProcessColumn::Cpu)] == 0.25);
    REQUIRE(rows[0].heat[static_cast<int>(ProcessColumn::Memory)] == 0.5);
    REQUIRE(rows[0].heat[static_cast<int>(ProcessColumn::Name)] == -1);
    REQUIRE(rows[0].heat[static_cast<int>(ProcessColumn::Threads)] == -1);
}

TEST_CASE("unmeasured cells are not heat", "[process-table]") {
    const auto f = frameOf({B(1, "a").noCpu()});
    REQUIRE(buildProcessTable(f, opts(), cc())[0].heat[static_cast<int>(ProcessColumn::Cpu)] == -1);
}

TEST_CASE("the same data builds the same rows", "[process-table]") {
    const auto f = frameOf({B(1, "a"), B(2, "b")});
    const auto a = buildProcessTable(f, opts(), cc());
    const auto b = buildProcessTable(f, opts(), cc());
    REQUIRE(a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); ++i) REQUIRE(a[i].id == b[i].id);
}

#ifdef PMP_OPTIMIZED
TEST_CASE("2,000 processes build quickly", "[process-table][perf]") {
    std::vector<ProcessRecord> ps;
    for (int i = 1; i <= 2000; ++i) ps.push_back(B(i, "proc" + std::to_string(i), i % 3 == 0 ? ProcessKind::App : ProcessKind::Background, i % 17).r);
    const auto f = frameOf(std::move(ps));
    auto o = opts(ProcessView::Flat);
    o.sortBy = ProcessColumn::Cpu;
    o.descending = true;
    (void)buildProcessTable(f, o, cc());  // warm up
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < 10; ++i) (void)buildProcessTable(f, o, cc());
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / 10;
    REQUIRE(ms < 15.0);
}
#endif

TEST_CASE("revealing a process opens the group, tree path or user header that hides it", "[process-table]") {
    // grouped: a member of a collapsed app group
    {
        const auto f = frameOf({B(5, "ff", ProcessKind::App).app("s"), B(6, "renderer", ProcessKind::App).app("s").parent(5)});
        auto o = opts(ProcessView::Grouped);
        REQUIRE(byName(buildProcessTable(f, o, cc()), "renderer") == nullptr);
        o.reveal = ProcessKey{6, 60};
        const auto rows = buildProcessTable(f, o, cc());
        REQUIRE(byName(rows, "renderer"));
        REQUIRE(byName(rows, "ff")->expanded);
    }
    // grouped: inside a header the user collapsed
    {
        auto o = opts(ProcessView::Grouped);
        o.toggled.insert("h:sys");
        const auto f = frameOf({B(1, "systemd", ProcessKind::System)});
        REQUIRE(byName(buildProcessTable(f, o, cc()), "systemd") == nullptr);
        o.reveal = ProcessKey{1, 10};
        REQUIRE(byName(buildProcessTable(f, o, cc()), "systemd"));
    }
    // tree: a grandchild
    {
        const auto f = frameOf({B(1, "init"), B(2, "child").parent(1), B(3, "grandchild").parent(2)});
        auto o = opts(ProcessView::Tree);
        o.reveal = ProcessKey{3, 30};
        REQUIRE(names(buildProcessTable(f, o, cc())) == std::vector<std::string>{"init", "child", "grandchild"});
    }
}
