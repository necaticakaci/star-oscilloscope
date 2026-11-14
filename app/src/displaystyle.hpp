// SPDX-License-Identifier: GPL-3.0-or-later
// Colours of the instrument displays: the trace colours the user chose, and the background they are drawn on.
#pragma once

#include <QColor>
#include <QFont>
#include <QIcon>
#include <QPen>

class QPaintDevice;

class QSettings;

namespace display_style {

enum Trace { Ch1, Ch2, Math, Ref1, Ref2, XY };  // XY: the figure of the X-Y display
inline constexpr int kTraces = 6;

QColor defaultColor(int trace);
QColor chosenColor(int trace);
void setChosenColor(int trace, const QColor& color);

// White background with dark traces, for pictures and documents. Light colours go darker on it.
bool printTheme();
void setPrintTheme(bool on);

// As drawn on the current background.
QColor traceColor(int trace);
inline QColor channel(int ch) { return traceColor(ch == 0 ? Ch1 : Ch2); }
inline QColor math() { return traceColor(Math); }
inline QColor reference(int ref) { return traceColor(ref == 0 ? Ref1 : Ref2); }

QColor background();
QColor grid();
QColor axis();
QColor text();        // labels
QColor strongText();  // values
QColor mark();        // cursors, the trigger position, frames

// Black or white, whichever reads on `fill`.
QColor textOn(const QColor& fill);

// A small square of the trace's colour, for lists and tables.
QIcon swatch(int trace);

// A line of whole device pixels: one on a usual screen, whatever the scaling. A pen of one logical pixel is 1.25
// device pixels at 125 % and leaves Qt's fast line drawing; the display then took a whole core (docs/building.md
// §1.1, Performance). A cosmetic pen keeps its width in device pixels; two of them from 200 %.
QPen hairline(const QColor& color, const QPaintDevice* device, Qt::PenStyle style = Qt::SolidLine);

// The fixed-width font of readouts and values. On Windows Consolas: Qt's choice there is Courier New, thin and hard to
// read on a dark screen.
QFont fixedFont();

void load(const QSettings& store);
void save(QSettings& store);

}  // namespace display_style
