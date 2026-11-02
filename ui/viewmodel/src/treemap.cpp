// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/vm/treemap.hpp>

#include <algorithm>
#include <cmath>

namespace pmp::vm {
namespace {

struct Item {
    double value = 0;  // area in pixels²
    const linux::ScanNode* node = nullptr;
    std::string label;
};

struct Box {
    double x, y, w, h;
};

// The worst aspect ratio of a row of areas laid along a side of length `side`.
double worst(const std::vector<Item>& row, std::size_t from, std::size_t to, double sum, double side) {
    double lo = row[from].value, hi = row[from].value;
    for (std::size_t i = from; i < to; ++i) {
        lo = std::min(lo, row[i].value);
        hi = std::max(hi, row[i].value);
    }
    const double s2 = side * side, sum2 = sum * sum;
    return std::max(s2 * hi / sum2, sum2 / (s2 * lo));
}

void squarify(const std::vector<Item>& items, Box box, int depth, std::vector<TreemapRect>& out) {
    std::size_t i = 0;
    while (i < items.size()) {
        if (box.w <= 0 || box.h <= 0) break;
        const bool horizontal = box.w >= box.h;  // lay the row along the shorter side
        const double side = horizontal ? box.h : box.w;
        std::size_t j = i + 1;
        double sum = items[i].value;
        double current = worst(items, i, j, sum, side);
        while (j < items.size()) {
            const double next = worst(items, i, j + 1, sum + items[j].value, side);
            if (next > current) break;
            sum += items[j].value;
            current = next;
            ++j;
        }
        if (j == items.size()) {  // the last row takes everything that is left, so the tiling is exact
            double rest = 0;
            for (std::size_t k = i; k < j; ++k) rest += items[k].value;
            sum = rest;
        }
        // The row occupies a strip of thickness sum / side.
        const double thick = sum / side;
        double pos = horizontal ? box.y : box.x;
        for (std::size_t k = i; k < j; ++k) {
            const double len = items[k].value / sum * side;
            TreemapRect r;
            r.depth = depth;
            r.node = items[k].node;
            r.label = items[k].label;
            if (horizontal) {
                r.x = box.x;
                r.y = pos;
                r.w = thick;
                r.h = len;
            } else {
                r.x = pos;
                r.y = box.y;
                r.w = len;
                r.h = thick;
            }
            pos += len;
            out.push_back(std::move(r));
        }
        if (horizontal) {
            box.x += thick;
            box.w -= thick;
        } else {
            box.y += thick;
            box.h -= thick;
        }
        i = j;
    }
}

void layoutLevel(const linux::ScanNode& node, Box box, int depth, int maxDepth, double minArea, int parentGroup, std::vector<TreemapRect>& out) {
    double total = 0;
    for (const auto& c : node.children) total += static_cast<double>(c.bytes);
    if (total <= 0 || box.w <= 0 || box.h <= 0) return;
    const double area = box.w * box.h;
    std::vector<Item> items;
    double smallArea = 0;
    std::size_t smallCount = 0;
    for (const auto& c : node.children) {  // already largest first
        if (c.bytes == 0) continue;
        const double a = static_cast<double>(c.bytes) / total * area;
        if (a < minArea) {
            smallArea += a;
            ++smallCount;
        } else {
            items.push_back({a, &c, c.name});
        }
    }
    if (smallCount > 0) items.push_back({smallArea, nullptr, std::to_string(smallCount) + (smallCount == 1 ? " small item" : " small items")});
    const auto first = out.size();
    squarify(items, box, depth, out);
    const auto last = out.size();
    for (auto i = first; i < last; ++i) out[i].group = depth == 0 ? static_cast<int>(i - first) : parentGroup;
    if (depth + 1 > maxDepth) return;
    for (auto i = first; i < last; ++i) {
        const auto r = out[i];  // copy: `out` grows below
        if (!r.node || !r.node->isDir || r.node->children.empty()) continue;
        constexpr double inset = 2.0, header = 12.0;
        Box inner{r.x + inset, r.y + header, r.w - 2 * inset, r.h - header - inset};
        if (inner.w < 8 || inner.h < 8) continue;
        layoutLevel(*r.node, inner, depth + 1, maxDepth, minArea, r.group, out);
    }
}

}  // namespace

std::vector<TreemapRect> layoutTreemap(const linux::ScanNode& root, double w, double h, int maxDepth, double minArea) {
    std::vector<TreemapRect> out;
    layoutLevel(root, {0, 0, w, h}, 0, std::max(0, maxDepth), minArea, 0, out);
    return out;
}

const linux::ScanNode* hitTest(const std::vector<TreemapRect>& rects, double x, double y) {
    const linux::ScanNode* best = nullptr;
    int bestDepth = -1;
    for (const auto& r : rects) {
        if (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h && r.depth >= bestDepth) {
            bestDepth = r.depth;
            best = r.node;  // a null node (the merged rectangle) is a real answer: nothing to select there
        }
    }
    return best;
}

}  // namespace pmp::vm
