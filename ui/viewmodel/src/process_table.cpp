// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/vm/process_table.hpp>

#include <perfmonplus/core/format.hpp>

#include <algorithm>
#include <cctype>
#include <deque>
#include <map>
#include <optional>

namespace pmp::vm {
namespace {

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string rowId(const ProcessRecord& r) { return "p:" + std::to_string(r.key.pid) + ":" + std::to_string(r.key.startTicks); }

struct Node {
    const ProcessRecord* rec = nullptr;  // the process, or the aggregate of a group
    TableRow::Type type = TableRow::Type::Process;
    std::string id;
    bool pinned = false;
    std::vector<int> kids;
    int members = 0;
};

struct Builder {
    const Frame& f;
    const ProcessTableOptions& o;
    CellContext cc;
    std::string needle;  // lower-cased filter
    std::set<ProcessKey> pinnedKeys;
    double maxRead = 0, maxWrite = 0;
    std::deque<ProcessRecord> store;  // aggregates and pinned copies: stable addresses
    std::vector<Node> nodes;

    Builder(const Frame& frame, const ProcessTableOptions& opt, const CellContext& ctx)
        : f(frame), o(opt), cc(ctx), needle(lower(opt.filter)) {
        for (const auto& p : o.pinned) pinnedKeys.insert(p.key);
    }

    bool matches(const ProcessRecord& r) const {
        if (needle.empty()) return true;
        return lower(r.name).find(needle) != std::string::npos || lower(r.user).find(needle) != std::string::npos ||
               std::to_string(r.key.pid).find(needle) != std::string::npos;
    }

    // Live processes plus the tombstones that are still within the user's retention (or pinned).
    std::vector<const ProcessRecord*> collect() {
        std::vector<const ProcessRecord*> out;
        std::set<ProcessKey> seen;
        for (const auto& r : f.processes.processes) {
            if (r.exitedAt) {
                const bool pinned = pinnedKeys.count(r.key) > 0;
                const bool fresh = o.tombstoneSeconds > 0 && f.mono - *r.exitedAt <= std::chrono::seconds(o.tombstoneSeconds);
                if (!pinned && !fresh) continue;
            } else {
                if (r.cpu.isMeasured()) cc.maxCpuPct = std::max(cc.maxCpuPct, *r.cpu.value);
                if (r.diskReadBps.isMeasured()) maxRead = std::max(maxRead, *r.diskReadBps.value);
                if (r.diskWriteBps.isMeasured()) maxWrite = std::max(maxWrite, *r.diskWriteBps.value);
            }
            seen.insert(r.key);
            out.push_back(&r);
        }
        for (const auto& p : o.pinned) {
            if (seen.count(p.key)) continue;
            store.push_back(p);
            out.push_back(&store.back());
        }
        return out;
    }

    int add(const ProcessRecord* r, TableRow::Type type, std::string id) {
        Node n;
        n.rec = r;
        n.type = type;
        n.id = std::move(id);
        n.pinned = r->exitedAt && pinnedKeys.count(r->key) > 0;
        nodes.push_back(std::move(n));
        return static_cast<int>(nodes.size()) - 1;
    }
    int addProcess(const ProcessRecord* r) { return add(r, TableRow::Type::Process, rowId(*r)); }

    // ---- sorting ---------------------------------------------------------------------------------------------
    static std::optional<double> numeric(const ProcessRecord& r, ProcessColumn c) {
        const auto m = [](const Metric& x) -> std::optional<double> { return x.isMeasured() ? x.value : std::nullopt; };
        switch (c) {
            case ProcessColumn::Pid: return static_cast<double>(r.key.pid);
            case ProcessColumn::Cpu:
            case ProcessColumn::Energy: return m(r.cpu);
            case ProcessColumn::Memory: return m(r.memFootprint);
            case ProcessColumn::Gpu: return m(r.gpu);
            case ProcessColumn::DiskRead: return m(r.diskReadBps);
            case ProcessColumn::DiskWrite: return m(r.diskWriteBps);
            case ProcessColumn::Threads: return m(r.threads);
            case ProcessColumn::PrivateMemory: return m(r.memPrivate);
            case ProcessColumn::PageFaults: return m(r.pageFaults);
            case ProcessColumn::Priority: return m(r.priority);
            case ProcessColumn::CpuTime: return m(r.cpuTime);
            case ProcessColumn::StartTime:
                return r.started ? std::optional<double>(std::chrono::duration<double>(r.started->time_since_epoch()).count()) : std::nullopt;
            default: return std::nullopt;
        }
    }
    static bool isText(ProcessColumn c) {
        switch (c) {
            case ProcessColumn::Name:
            case ProcessColumn::Status:
            case ProcessColumn::User:
            case ProcessColumn::CommandLine:
            case ProcessColumn::Path:
            case ProcessColumn::Kind: return true;
            default: return false;
        }
    }
    static std::string text(const ProcessRecord& r, ProcessColumn c) {
        switch (c) {
            case ProcessColumn::Name: return lower(r.name);
            case ProcessColumn::Status: return statusText(r.state, r.exitedAt.has_value());
            case ProcessColumn::User: return lower(r.user);
            case ProcessColumn::CommandLine: return lower(r.cmdline);
            case ProcessColumn::Path: return lower(r.exe);
            case ProcessColumn::Kind: return kindText(r.kind);
            default: return {};
        }
    }

    bool before(int a, int b) const {
        const auto& ra = *nodes[static_cast<std::size_t>(a)].rec;
        const auto& rb = *nodes[static_cast<std::size_t>(b)].rec;
        if (isText(o.sortBy)) {
            const auto ta = text(ra, o.sortBy), tb = text(rb, o.sortBy);
            if (ta != tb) return o.descending ? ta > tb : ta < tb;
        } else {
            const auto va = numeric(ra, o.sortBy), vb = numeric(rb, o.sortBy);
            if (va.has_value() != vb.has_value()) return va.has_value();  // unmeasured last, either direction
            if (va && *va != *vb) return o.descending ? *va > *vb : *va < *vb;
        }
        return ra.key < rb.key;  // stable: always ascending
    }

    void sort(std::vector<int>& v) const {
        std::stable_sort(v.begin(), v.end(), [this](int a, int b) { return before(a, b); });
    }

    // ---- rows ------------------------------------------------------------------------------------------------
    TableRow makeRow(const Node& n, int depth, bool expandable, bool expanded) const {
        const auto& r = *n.rec;
        TableRow row;
        row.type = n.type;
        row.id = n.id;
        row.key = r.key;
        row.depth = depth;
        row.expandable = expandable;
        row.expanded = expanded;
        row.tombstone = r.exitedAt.has_value();
        row.pinned = n.pinned;
        row.cells = processCells(r, cc);
        row.heat.fill(-1);
        const auto frac = [&](ProcessColumn c, const Metric& m, double full) {
            if (m.isMeasured() && full > 0) row.heat[static_cast<std::size_t>(c)] = std::clamp(*m.value / full, 0.0, 1.0);
        };
        frac(ProcessColumn::Cpu, r.cpu, 100);
        frac(ProcessColumn::Memory, r.memFootprint, cc.totalMemoryBytes);
        frac(ProcessColumn::Gpu, r.gpu, 100);
        frac(ProcessColumn::DiskRead, r.diskReadBps, maxRead);
        frac(ProcessColumn::DiskWrite, r.diskWriteBps, maxWrite);
        if (row.tombstone) row.tooltip = exitText(r);
        return row;
    }

    std::string exitText(const ProcessRecord& r) const {
        const auto ago = std::chrono::duration_cast<std::chrono::seconds>(f.mono - *r.exitedAt).count();
        std::string t = "Exited " + std::to_string(std::max<long long>(0, static_cast<long long>(ago))) + " s ago";
        if (r.started) {
            const auto exitWall = cc.wallNow - std::chrono::seconds(std::max<long long>(0, static_cast<long long>(ago)));
            const long long ran = std::chrono::duration_cast<std::chrono::seconds>(exitWall - *r.started).count();
            if (ran >= 0) {
                char buf[48];
                std::snprintf(buf, sizeof buf, "%lld:%02lld:%02lld", ran / 3600, ran / 60 % 60, ran % 60);
                t += std::string(" · ran ") + buf;
            }
        }
        return t + " · exit status unavailable";
    }

    std::set<std::string> forcedOpen;  // ids opened because the user asked to reveal a process
    bool expanded(const Node& n, bool dflt, bool forced) const {
        return forced || forcedOpen.count(n.id) > 0 || (dflt != (o.toggled.count(n.id) > 0));
    }

    TableRow header(std::string id, std::string title, bool expandedState) const {
        TableRow r;
        r.type = TableRow::Type::Header;
        r.id = std::move(id);
        r.title = std::move(title);
        r.expandable = true;
        r.expanded = expandedState;
        r.heat.fill(-1);
        return r;
    }

    // ---- views -----------------------------------------------------------------------------------------------
    std::vector<TableRow> flat(const std::vector<const ProcessRecord*>& recs) {
        std::vector<int> idx;
        for (const auto* r : recs) {
            if (matches(*r)) idx.push_back(addProcess(r));
        }
        sort(idx);
        std::vector<TableRow> out;
        for (int i : idx) out.push_back(makeRow(nodes[static_cast<std::size_t>(i)], 0, false, false));
        return out;
    }

    std::vector<TableRow> byUser(const std::vector<const ProcessRecord*>& recs) {
        std::map<std::string, std::vector<int>> groups;
        for (const auto* r : recs) {
            if (matches(*r)) groups[r->user.empty() ? std::string("—") : r->user].push_back(addProcess(r));
        }
        if (o.reveal) {
            for (const auto* r : recs) {
                if (r->key == *o.reveal) forcedOpen.insert("u:" + (r->user.empty() ? std::string("—") : r->user));
            }
        }
        std::vector<TableRow> out;
        for (auto& [user, idx] : groups) {
            sort(idx);
            const std::string id = "u:" + user;
            Node probe;
            probe.id = id;
            const bool open = expanded(probe, true, !needle.empty());
            out.push_back(header(id, user + " (" + std::to_string(idx.size()) + ")", open));
            if (!open) continue;
            for (int i : idx) out.push_back(makeRow(nodes[static_cast<std::size_t>(i)], 1, false, false));
        }
        return out;
    }

    // A group's row is an aggregate: CPU, GPU, disk and threads add up; memory adds up *private* memory (shared
    // pages would be counted once per process) and is unknown if any member's is.
    const ProcessRecord* aggregate(const ProcessRecord& leader, const std::vector<const ProcessRecord*>& members) {
        ProcessRecord a = leader;
        a.exitedAt.reset();
        const auto sum = [&](Metric ProcessRecord::*field, Unit u) {
            double total = 0;
            bool any = false;
            for (const auto* m : members) {
                const auto& x = (*m).*field;
                if (x.isMeasured()) {
                    total += *x.value;
                    any = true;
                }
            }
            return any ? Metric::measured(total, u) : Metric::notMeasured(Availability::Pending, u);
        };
        a.cpu = sum(&ProcessRecord::cpu, Unit::Percent);
        a.gpu = sum(&ProcessRecord::gpu, Unit::Percent);
        a.diskReadBps = sum(&ProcessRecord::diskReadBps, Unit::BytesPerSec);
        a.diskWriteBps = sum(&ProcessRecord::diskWriteBps, Unit::BytesPerSec);
        a.threads = sum(&ProcessRecord::threads, Unit::Count);
        a.cpuTime = sum(&ProcessRecord::cpuTime, Unit::Seconds);
        a.pageFaults = sum(&ProcessRecord::pageFaults, Unit::Count);
        double priv = 0;
        bool allPrivate = true;
        for (const auto* m : members) {
            if (m->memPrivate.isMeasured()) priv += *m->memPrivate.value;
            else allPrivate = false;
        }
        a.memFootprint = allPrivate ? Metric::measured(priv, Unit::Bytes) : Metric::notMeasured(Availability::Unsupported, Unit::Bytes);
        a.memPrivate = a.memFootprint;
        for (const auto* m : members) {
            if (m->state == 'R') a.state = 'R';
        }
        store.push_back(std::move(a));
        return &store.back();
    }

    std::vector<TableRow> grouped(const std::vector<const ProcessRecord*>& recs) {
        struct Section {
            const char* id;
            const char* title;
            std::vector<int> entries;
            std::vector<TableRow> rows;
        };
        Section sections[3] = {{"h:apps", "Applications", {}, {}}, {"h:bg", "Background processes", {}, {}}, {"h:sys", "System processes", {}, {}}};
        const auto sectionOf = [](ProcessKind k) { return k == ProcessKind::App ? 0 : k == ProcessKind::Background ? 1 : 2; };

        std::map<std::string, std::vector<const ProcessRecord*>> scopes;  // live app processes by scope
        std::vector<const ProcessRecord*> loose;
        for (const auto* r : recs) {
            if (!r->exitedAt && r->kind == ProcessKind::App && !r->appScope.empty()) scopes[r->appScope].push_back(r);
            else loose.push_back(r);
        }
        std::map<int, std::vector<const ProcessRecord*>> groupMembers;  // node index -> members
        for (auto& [scope, members] : scopes) {
            if (members.size() == 1) {
                if (matches(*members[0])) sections[0].entries.push_back(addProcess(members[0]));
                continue;
            }
            std::set<ProcessKey> keys;
            for (const auto* m : members) keys.insert(m->key);
            const ProcessRecord* leader = members[0];
            for (const auto* m : members) {
                const bool inside = m->parent && keys.count(*m->parent);
                const bool leaderInside = leader->parent && keys.count(*leader->parent);
                if ((!inside && leaderInside) || (inside == leaderInside && m->key < leader->key)) leader = m;
            }
            const bool any = std::any_of(members.begin(), members.end(), [this](const ProcessRecord* m) { return matches(*m); });
            if (!any) continue;
            const int g = add(aggregate(*leader, members), TableRow::Type::Group, "a:" + scope);
            nodes[static_cast<std::size_t>(g)].members = static_cast<int>(members.size());
            groupMembers[g] = members;
            sections[0].entries.push_back(g);
        }
        for (const auto* r : loose) {
            if (matches(*r)) sections[sectionOf(r->kind)].entries.push_back(addProcess(r));
        }

        if (o.reveal) {
            for (const auto& [g, members] : groupMembers) {
                for (const auto* m : members) {
                    if (m->key != *o.reveal) continue;
                    forcedOpen.insert(nodes[static_cast<std::size_t>(g)].id);  // the app group...
                    forcedOpen.insert("h:apps");                               // ...and the header above it
                }
            }
            for (const auto* r : recs) {
                if (r->key == *o.reveal) forcedOpen.insert(r->kind == ProcessKind::App ? "h:apps" : r->kind == ProcessKind::Background ? "h:bg" : "h:sys");
            }
        }
        std::vector<TableRow> out;
        for (auto& s : sections) {
            if (s.entries.empty()) continue;
            sort(s.entries);
            std::size_t liveCount = 0;
            for (int e : s.entries) liveCount += nodes[static_cast<std::size_t>(e)].rec->exitedAt ? 0 : 1;
            Node probe;
            probe.id = s.id;
            const bool open = expanded(probe, true, false);
            out.push_back(header(s.id, std::string(s.title) + " (" + std::to_string(liveCount) + ")", open));
            if (!open) continue;
            for (int e : s.entries) {
                const Node& n = nodes[static_cast<std::size_t>(e)];
                if (n.type != TableRow::Type::Group) {
                    out.push_back(makeRow(n, 1, false, false));
                    continue;
                }
                const bool open2 = expanded(n, false, !needle.empty());
                out.push_back(makeRow(n, 1, true, open2));
                for (const auto* m : groupMembers[e]) out.back().members.push_back(m->key);
                if (!open2) continue;
                std::vector<int> kids;
                for (const auto* m : groupMembers[e]) {
                    if (matches(*m)) kids.push_back(addProcess(m));
                }
                sort(kids);
                for (int k : kids) out.push_back(makeRow(nodes[static_cast<std::size_t>(k)], 2, false, false));
            }
        }
        return out;
    }

    std::vector<TableRow> tree(const std::vector<const ProcessRecord*>& recs) {
        std::map<ProcessKey, int> byKey;
        for (const auto* r : recs) byKey[r->key] = addProcess(r);
        std::vector<int> roots;
        for (const auto* r : recs) {
            const int me = byKey[r->key];
            const auto p = r->parent ? byKey.find(*r->parent) : byKey.end();
            if (p != byKey.end() && p->second != me && !r->exitedAt) nodes[static_cast<std::size_t>(p->second)].kids.push_back(me);
            else roots.push_back(me);
        }
        // visible[i]: the node matches, or something below it does
        std::vector<char> visible(nodes.size(), 0), anyBelow(nodes.size(), 0);
        std::vector<int> order;  // post-order
        std::vector<char> seen(nodes.size(), 0);
        std::vector<std::pair<int, std::size_t>> stack;
        for (int root : roots) {
            stack.push_back({root, 0});
            seen[static_cast<std::size_t>(root)] = 1;
            while (!stack.empty()) {
                auto& [n, i] = stack.back();
                auto& kids = nodes[static_cast<std::size_t>(n)].kids;
                if (i < kids.size()) {
                    const int k = kids[i++];
                    if (!seen[static_cast<std::size_t>(k)]) {
                        seen[static_cast<std::size_t>(k)] = 1;
                        stack.push_back({k, 0});
                    }
                } else {
                    order.push_back(n);
                    stack.pop_back();
                }
            }
        }
        for (int n : order) {
            bool below = false;
            for (int k : nodes[static_cast<std::size_t>(n)].kids) below = below || visible[static_cast<std::size_t>(k)];
            anyBelow[static_cast<std::size_t>(n)] = below;
            visible[static_cast<std::size_t>(n)] = matches(*nodes[static_cast<std::size_t>(n)].rec) || below;
        }
        if (o.reveal) {
            if (const auto it = byKey.find(*o.reveal); it != byKey.end()) {
                std::vector<int> parentOf(nodes.size(), -1);
                for (std::size_t n = 0; n < nodes.size(); ++n) {
                    for (int k : nodes[n].kids) parentOf[static_cast<std::size_t>(k)] = static_cast<int>(n);
                }
                for (int p = parentOf[static_cast<std::size_t>(it->second)]; p >= 0; p = parentOf[static_cast<std::size_t>(p)]) {
                    forcedOpen.insert(nodes[static_cast<std::size_t>(p)].id);
                }
            }
        }
        std::vector<TableRow> out;
        const auto emit = [&](auto&& self, int n, int depth) -> void {
            const Node& node = nodes[static_cast<std::size_t>(n)];
            std::vector<int> kids;
            for (int k : node.kids) {
                if (visible[static_cast<std::size_t>(k)]) kids.push_back(k);
            }
            sort(kids);
            const bool open = !kids.empty() && expanded(node, depth == 0, !needle.empty() && anyBelow[static_cast<std::size_t>(n)]);
            out.push_back(makeRow(node, depth, !kids.empty(), open));
            if (!open) return;
            for (int k : kids) self(self, k, depth + 1);
        };
        std::vector<int> top;
        for (int r : roots) {
            if (visible[static_cast<std::size_t>(r)]) top.push_back(r);
        }
        sort(top);
        for (int r : top) emit(emit, r, 0);
        return out;
    }
};

}  // namespace

std::vector<TableRow> buildProcessTable(const Frame& f, const ProcessTableOptions& o, const CellContext& ctx) {
    Builder b(f, o, ctx);
    b.nodes.reserve(f.processes.processes.size() + o.pinned.size() + 16);
    const auto recs = b.collect();
    switch (o.view) {
        case ProcessView::Grouped: return b.grouped(recs);
        case ProcessView::Tree: return b.tree(recs);
        case ProcessView::Flat: return b.flat(recs);
        case ProcessView::ByUser: return b.byUser(recs);
    }
    return {};
}

}  // namespace pmp::vm
