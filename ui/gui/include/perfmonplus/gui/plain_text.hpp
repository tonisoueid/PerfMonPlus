// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#pragma once

#include <QString>

namespace pmp::gui {

// Qt shows a tooltip as rich text whenever it looks like markup. Text that came from another program (a process or
// file name) is escaped and wrapped so it is always shown literally.
inline QString plainTip(const QString& text) { return "<span>" + text.toHtmlEscaped() + "</span>"; }

}  // namespace pmp::gui
