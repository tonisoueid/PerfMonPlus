// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/linux/kernel_modules.hpp>

#include <charconv>
#include <sstream>

namespace pmp::linux {

std::vector<KernelModule> parseModules(std::string_view text) {
    std::vector<KernelModule> out;
    std::istringstream in{std::string(text)};
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream f(line);
        std::string name, size, refs, used, state;
        if (!(f >> name >> size >> refs >> used >> state)) continue;
        KernelModule m;
        m.name = sanitizeText(name, 128);
        std::uint64_t bytes = 0;
        if (std::from_chars(size.data(), size.data() + size.size(), bytes).ec != std::errc{}) continue;
        m.sizeBytes = bytes;
        std::from_chars(refs.data(), refs.data() + refs.size(), m.refCount);
        if (used != "-") {
            std::istringstream u(used);
            std::string part;
            while (std::getline(u, part, ',')) {
                if (!part.empty()) m.usedBy.push_back(sanitizeText(part, 128));
            }
        }
        m.state = sanitizeText(state, 16);
        out.push_back(std::move(m));
    }
    return out;
}

std::vector<KernelModule> readKernelModules(const ProcRoot& root) {
    const auto t = readText(root.proc / "modules");
    return t.ok() ? parseModules(t.text) : std::vector<KernelModule>{};
}

}  // namespace pmp::linux
