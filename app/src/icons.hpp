// SPDX-License-Identifier: GPL-3.0-or-later
// Toolbar icons, drawn in code at whatever size is asked for: open line drawings in the palette's text colour, so they
// stay visible on a dark palette and show whatever is behind them, and colour only for the run state.
#pragma once

#include <functional>

#include <QColor>
#include <QIcon>
#include <QSize>

class QPainter;

namespace icons {

// An icon drawn whenever it is shown, so it follows a change of palette. The drawing is made on a canvas of the given
// size, scaled to the icon. The pen starts in `ink`: the palette's button text, or its highlighted text where the icon
// is selected, and the brush empty.
using Draw = std::function<void(QPainter&, const QColor& ink)>;
QIcon drawn(QSize canvas, Draw draw);

QIcon run();
QIcon stop();
QIcon single();
QIcon forceTrigger();
QIcon autoset();
QIcon cursors();
QIcon xy();
QIcon measurements();
QIcon resetStatistics();
QIcon generator();
QIcon exportData();
QIcon screenshot();
QIcon resetLayout();

}  // namespace icons
