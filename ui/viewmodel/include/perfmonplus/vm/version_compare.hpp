// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#pragma once

#include <string_view>

namespace pmp::vm {

// Dotted numeric versions: "1.2.10" > "1.2.9" and "1.0" == "1.0.0". A part that is not a number counts as 0 and
// anything after the digits of a part (such as "-beta") is ignored. Returns <0, 0 or >0.
int compareVersions(std::string_view a, std::string_view b);

}  // namespace pmp::vm
