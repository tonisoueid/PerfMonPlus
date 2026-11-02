// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#pragma once

#include <perfmonplus/linux/disk_scan.hpp>

#include <string>
#include <vector>

namespace pmp::vm {

// One rectangle of the folder-size map (SPC-02). `node` is null for the merged "small items" rectangle. Rectangles
// of depth 0 tile the whole area exactly; those of depth 1 and 2 tile the inside of their parent.
struct TreemapRect {
    double x = 0, y = 0, w = 0, h = 0;
    const linux::ScanNode* node = nullptr;
    int depth = 0;
    int group = 0;  // the index of the top-level item this rectangle belongs to (for colouring)
    std::string label;  // sanitised name, or "12 small items"
};

// Squarified layout (Bruls, Huizing, van Wijk): children largest first, each row laid along the shorter side so the
// rectangles stay close to square. Items whose share of the area is below `minArea` pixels² are merged into one
// rectangle, and items with no bytes get none. Deterministic: the same tree always gives the same picture.
std::vector<TreemapRect> layoutTreemap(const linux::ScanNode& root, double w, double h, int maxDepth = 2, double minArea = 36.0);

// The deepest rectangle containing the point whose node is real; nullptr over the merged rectangle or outside.
const linux::ScanNode* hitTest(const std::vector<TreemapRect>& rects, double x, double y);

}  // namespace pmp::vm
