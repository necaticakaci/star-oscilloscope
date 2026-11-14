// SPDX-License-Identifier: GPL-3.0-or-later
#include "displaystyle.hpp"

#include <algorithm>
#include <array>
#include <cmath>

#include <QPaintDevice>
#include <QPixmap>
#include <QFontDatabase>
#include <QSettings>

namespace display_style {
namespace {

constexpr double kMaxLightnessOnWhite = 0.36;
constexpr const char* kKeys[kTraces] = {"ch1", "ch2", "math", "ref1", "ref2", "xy"};

// The X-Y figure starts in CH1's colour, as it was drawn before it had its own.
const std::array<QColor, kTraces> kDefaults{QColor(0xe6, 0xd2, 0x00), QColor(0x2c, 0xc6, 0xdc), QColor(0xe0, 0x5c, 0xe0),
                                            QColor(0xc8, 0xc8, 0xc8), QColor(0xf0, 0xa0, 0x40), QColor(0xe6, 0xd2, 0x00)};

std::array<QColor, kTraces> chosen = kDefaults;
bool print = false;

QColor forWhite(const QColor& color)
{
    float h = 0, s = 0, l = 0;
    color.getHslF(&h, &s, &l);
    return QColor::fromHslF(h, s, std::min(l, static_cast<float>(kMaxLightnessOnWhite)));
}

}  // namespace

QColor defaultColor(int trace) { return kDefaults[static_cast<std::size_t>(trace)]; }

QColor chosenColor(int trace) { return chosen[static_cast<std::size_t>(trace)]; }

void setChosenColor(int trace, const QColor& color)
{
    if (color.isValid())
        chosen[static_cast<std::size_t>(trace)] = color;
}

bool printTheme() { return print; }

void setPrintTheme(bool on) { print = on; }

QColor traceColor(int trace)
{
    const QColor color = chosenColor(trace);
    return print ? forWhite(color) : color;
}

QColor background() { return print ? QColor(Qt::white) : QColor(Qt::black); }
QColor grid() { return print ? QColor(0xc8, 0xc8, 0xc8) : QColor(0x3c, 0x3c, 0x3c); }
QColor axis() { return print ? QColor(0x80, 0x80, 0x80) : QColor(0x60, 0x60, 0x60); }
QColor text() { return print ? QColor(0x50, 0x50, 0x50) : QColor(0xa0, 0xa0, 0xa0); }
QColor strongText() { return print ? QColor(Qt::black) : QColor(0xf0, 0xf0, 0xf0); }
QColor mark() { return print ? QColor(0x40, 0x40, 0x40) : QColor(0xd0, 0xd0, 0xd0); }

QColor textOn(const QColor& fill)
{
    const double luminance = 0.299 * fill.redF() + 0.587 * fill.greenF() + 0.114 * fill.blueF();
    return luminance > 0.45 ? QColor(Qt::black) : QColor(Qt::white);
}

QPen hairline(const QColor& color, const QPaintDevice* device, Qt::PenStyle style)
{
    const qreal ratio = device ? device->devicePixelRatioF() : 1.0;
    QPen pen(color, std::max<qreal>(1.0, std::floor(ratio)), style);
    pen.setCosmetic(true);
    return pen;
}

QIcon swatch(int trace)
{
    QPixmap pixmap(10, 10);
    pixmap.fill(traceColor(trace));
    return QIcon(pixmap);
}

void load(const QSettings& store)
{
    for (int t = 0; t < kTraces; ++t) {
        const QColor color(store.value(QString("display/color/%1").arg(kKeys[t])).toString());
        chosen[static_cast<std::size_t>(t)] = color.isValid() ? color : kDefaults[static_cast<std::size_t>(t)];
    }
    print = store.value("display/printTheme", false).toBool();
}

void save(QSettings& store)
{
    for (int t = 0; t < kTraces; ++t)
        store.setValue(QString("display/color/%1").arg(kKeys[t]), chosenColor(t).name());
    store.setValue("display/printTheme", print);
}

QFont fixedFont()
{
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
#ifdef Q_OS_WIN
    if (QFontDatabase::families().contains(QStringLiteral("Consolas")))
        font.setFamily(QStringLiteral("Consolas"));
#endif
    return font;
}

}  // namespace display_style
