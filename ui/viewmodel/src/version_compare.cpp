// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/vm/version_compare.hpp>

#include <algorithm>
#include <cstddef>
#include <vector>

namespace pmp::vm {
namespace {

std::vector<unsigned long> parts(std::string_view v) {
    std::vector<unsigned long> out;
    std::size_t i = 0;
    while (true) {
        unsigned long n = 0;
        while (i < v.size() && v[i] >= '0' && v[i] <= '9') {
            n = n * 10 + static_cast<unsigned long>(v[i] - '0');
            ++i;
        }
        out.push_back(n);
        while (i < v.size() && v[i] != '.') ++i;  // skip a suffix such as "-beta"
        if (i >= v.size()) break;
        ++i;  // the dot
    }
    return out;
}

}  // namespace

int compareVersions(std::string_view a, std::string_view b) {
    const auto pa = parts(a);
    const auto pb = parts(b);
    for (std::size_t i = 0; i < std::max(pa.size(), pb.size()); ++i) {
        const unsigned long x = i < pa.size() ? pa[i] : 0;
        const unsigned long y = i < pb.size() ? pb[i] : 0;
        if (x != y) return x < y ? -1 : 1;
    }
    return 0;
}

}  // namespace pmp::vm
