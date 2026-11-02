// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/vm/treemap.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <random>

using namespace pmp;
using namespace pmp::vm;
using namespace pmp::linux;
using Catch::Approx;

namespace {

ScanNode leaf(const std::string& name, std::uint64_t bytes) {
    ScanNode n;
    n.name = n.rawName = name;
    n.bytes = bytes;
    n.files = 1;
    return n;
}

ScanNode dir(const std::string& name, std::vector<ScanNode> kids) {
    ScanNode n;
    n.name = n.rawName = name;
    n.isDir = true;
    for (auto& k : kids) n.bytes += k.bytes;
    std::sort(kids.begin(), kids.end(), [](const ScanNode& a, const ScanNode& b) { return a.bytes > b.bytes; });
    n.children = std::move(kids);
    return n;
}

ScanNode randomTree(std::mt19937& rng, int depth) {
    std::vector<ScanNode> kids;
    const int n = 2 + static_cast<int>(rng() % 12);
    for (int i = 0; i < n; ++i) {
        const auto bytes = 1 + rng() % 1'000'000;
        if (depth > 0 && rng() % 3 == 0) kids.push_back(dir("d" + std::to_string(i), {randomTree(rng, depth - 1)}));
        else kids.push_back(leaf("f" + std::to_string(i), bytes));
    }
    return dir("root", std::move(kids));
}

double areaOf(const std::vector<TreemapRect>& rs, int depth, const ScanNode* parentNode = nullptr) {
    (void)parentNode;
    double a = 0;
    for (const auto& r : rs) {
        if (r.depth == depth) a += r.w * r.h;
    }
    return a;
}

bool overlap(const TreemapRect& a, const TreemapRect& b) {
    const double eps = 1e-6;
    return a.x < b.x + b.w - eps && b.x < a.x + a.w - eps && a.y < b.y + b.h - eps && b.y < a.y + a.h - eps;
}

}  // namespace

TEST_CASE("layout covers the area without overlap", "[treemap]") {
    for (unsigned seed = 0; seed < 200; ++seed) {
        std::mt19937 rng(seed);
        const auto tree = randomTree(rng, 0);
        const auto rects = layoutTreemap(tree, 640, 400, 0, 0.0);
        REQUIRE(areaOf(rects, 0) == Approx(640.0 * 400.0).epsilon(1e-9));
        for (std::size_t i = 0; i < rects.size(); ++i) {
            REQUIRE(rects[i].x >= -1e-9);
            REQUIRE(rects[i].y >= -1e-9);
            REQUIRE(rects[i].x + rects[i].w <= 640 + 1e-6);
            REQUIRE(rects[i].y + rects[i].h <= 400 + 1e-6);
            for (std::size_t j = i + 1; j < rects.size(); ++j) REQUIRE_FALSE(overlap(rects[i], rects[j]));
        }
    }
}

TEST_CASE("layout is deterministic", "[treemap]") {
    std::mt19937 rng(7);
    const auto tree = randomTree(rng, 2);
    const auto a = layoutTreemap(tree, 500, 300), b = layoutTreemap(tree, 500, 300);
    REQUIRE(a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        REQUIRE(a[i].x == b[i].x);
        REQUIRE(a[i].w == b[i].w);
        REQUIRE(a[i].node == b[i].node);
    }
}

TEST_CASE("areas are proportional to sizes within 1%", "[treemap]") {
    const auto tree = dir("root", {leaf("a", 6000), leaf("b", 3000), leaf("c", 1000)});
    const auto rects = layoutTreemap(tree, 1000, 600, 0, 0.0);
    REQUIRE(rects.size() == 3);
    for (const auto& r : rects) {
        const double share = r.w * r.h / (1000.0 * 600.0);
        const double want = static_cast<double>(r.node->bytes) / 10000.0;
        REQUIRE(share == Approx(want).epsilon(0.01));
    }
}

TEST_CASE("tiny items are merged into one rectangle", "[treemap]") {
    std::vector<ScanNode> kids = {leaf("big", 1'000'000)};
    for (int i = 0; i < 50; ++i) kids.push_back(leaf("tiny" + std::to_string(i), 1));
    const auto tree = dir("root", std::move(kids));
    const auto rects = layoutTreemap(tree, 400, 300, 0, 36.0);
    REQUIRE(rects.size() == 2);
    int merged = 0;
    for (const auto& r : rects) {
        if (!r.node) {
            ++merged;
            REQUIRE(r.label == "50 small items");
        }
    }
    REQUIRE(merged == 1);
    REQUIRE(areaOf(rects, 0) == Approx(400.0 * 300.0).epsilon(1e-9));
}

TEST_CASE("zero-byte nodes get no rectangle and a single child fills the area", "[treemap]") {
    const auto tree = dir("root", {leaf("only", 500), leaf("empty", 0)});
    const auto rects = layoutTreemap(tree, 200, 100, 0, 0.0);
    REQUIRE(rects.size() == 1);
    REQUIRE(rects[0].w == Approx(200));
    REQUIRE(rects[0].h == Approx(100));
    REQUIRE(layoutTreemap(dir("root", {}), 200, 100).empty());
    REQUIRE(layoutTreemap(dir("root", {leaf("a", 1)}), 0, 100).empty());
}

TEST_CASE("nested folders tile the inside of their parent", "[treemap]") {
    const auto tree = dir("root", {dir("sub", {leaf("x", 700), leaf("y", 300)}), leaf("file", 1000)});
    const auto rects = layoutTreemap(tree, 600, 400, 2, 0.0);
    const TreemapRect* sub = nullptr;
    for (const auto& r : rects) {
        if (r.depth == 0 && r.node && r.node->name == "sub") sub = &r;
    }
    REQUIRE(sub != nullptr);
    int inside = 0;
    for (const auto& r : rects) {
        if (r.depth == 1) {
            ++inside;
            REQUIRE(r.x >= sub->x);
            REQUIRE(r.y >= sub->y);
            REQUIRE(r.x + r.w <= sub->x + sub->w + 1e-6);
            REQUIRE(r.y + r.h <= sub->y + sub->h + 1e-6);
        }
    }
    REQUIRE(inside == 2);
}

TEST_CASE("hit test returns the deepest node", "[treemap]") {
    const auto tree = dir("root", {dir("sub", {leaf("x", 700), leaf("y", 300)}), leaf("file", 1000)});
    const auto rects = layoutTreemap(tree, 600, 400, 2, 0.0);
    const TreemapRect* x = nullptr;
    for (const auto& r : rects) {
        if (r.depth == 1 && r.node->name == "x") x = &r;
    }
    REQUIRE(x != nullptr);
    const auto* hit = hitTest(rects, x->x + x->w / 2, x->y + x->h / 2);
    REQUIRE(hit != nullptr);
    REQUIRE(hit->name == "x");
    REQUIRE(hitTest(rects, -5, -5) == nullptr);
    REQUIRE(hitTest(rects, 700, 10) == nullptr);
}

TEST_CASE("nested rectangles carry their top-level group", "[treemap]") {
    const auto tree = dir("root", {dir("sub", {leaf("x", 700), leaf("y", 300)}), leaf("file", 1000)});
    const auto rects = layoutTreemap(tree, 600, 400, 2, 0.0);
    int subGroup = -1;
    for (const auto& r : rects) {
        if (r.depth == 0 && r.node && r.node->name == "sub") subGroup = r.group;
    }
    REQUIRE(subGroup >= 0);
    int nested = 0;
    for (const auto& r : rects) {
        if (r.depth == 1) {
            REQUIRE(r.group == subGroup);
            ++nested;
        }
    }
    REQUIRE(nested == 2);
}
