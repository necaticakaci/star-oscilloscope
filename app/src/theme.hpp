// SPDX-License-Identifier: GPL-3.0-or-later
// Widget style and palette selection. Fusion with the system palette by default; the classic look (Windows style,
// classic palette) is a choice.
#pragma once

#include <QStringList>

namespace theme {

QStringList styles();
QString style();
bool classicPalette();

// Applies and remembers the choice.
void apply(const QString& style, bool classicPalette);
void applySaved();

}  // namespace theme
