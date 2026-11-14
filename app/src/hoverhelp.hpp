// SPDX-License-Identifier: GPL-3.0-or-later
// Help under the pointer goes to a status line, never into a tooltip (2026-11-09). A tooltip over a key or a knob
// repeats its label or covers what is next to it with a long text; a status line has room and covers nothing.
//
// The help of a widget is its status tip; of an item or a column heading, its Qt::StatusTipRole; of a tab, its tab tool
// tip. In the main window, its docks (also floating) and the panels in them it goes to the main window's status bar. A
// dialog shows it in its own help line (line()); one without a help line has no help to show.
#pragma once

#include <QString>

class QLabel;
class QWidget;

namespace hover_help {

// Puts the filter on the application, once: tooltips are not shown, and help follows the pointer.
void install();

// A help line for the foot of a dialog, `lines` lines high so that the dialog keeps its size.
QLabel* line(QWidget* dialog, int lines = 1);

// Shows `text` as the help of `source`, for a widget whose help depends on where the pointer is in it; an empty text
// clears it.
void say(QWidget* source, const QString& text);

}  // namespace hover_help
