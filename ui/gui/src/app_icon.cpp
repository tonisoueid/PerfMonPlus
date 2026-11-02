// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PerfMonPlus contributors

#include <perfmonplus/gui/app_icon.hpp>

#include <QtCore/QtGlobal>

// Resources of a static library must be registered explicitly, from the global namespace.
static void pmpInitIconResource() { Q_INIT_RESOURCE(perfmonplus); }

namespace pmp::gui {

QIcon applicationIcon() {
    pmpInitIconResource();
    return QIcon(QStringLiteral(":/icons/perfmonplus.svg"));
}

}  // namespace pmp::gui
