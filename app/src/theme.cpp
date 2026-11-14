// SPDX-License-Identifier: GPL-3.0-or-later
#include "theme.hpp"

#include <QApplication>
#include <QEvent>
#include <QMenu>
#include <QOperatingSystemVersion>
#include <QPalette>
#include <QSettings>
#include <QStyle>
#include <QStyleFactory>

namespace theme {
namespace {

constexpr auto kStyleKey = "appearance/style";
constexpr auto kPaletteKey = "appearance/classicPalette";

// Gray workstation palette in the spirit of Windows 2000 and KDE 2.
QPalette makeClassicPalette()
{
    const QColor face(0xd4, 0xd0, 0xc8);
    const QColor disabledText(0x80, 0x80, 0x80);
    QPalette p;
    p.setColor(QPalette::Window, face);
    p.setColor(QPalette::WindowText, Qt::black);
    p.setColor(QPalette::Base, Qt::white);
    p.setColor(QPalette::AlternateBase, QColor(0xf3, 0xf2, 0xef));
    p.setColor(QPalette::ToolTipBase, QColor(0xff, 0xff, 0xe1));
    p.setColor(QPalette::ToolTipText, Qt::black);
    p.setColor(QPalette::PlaceholderText, disabledText);
    p.setColor(QPalette::Text, Qt::black);
    p.setColor(QPalette::Button, face);
    p.setColor(QPalette::ButtonText, Qt::black);
    p.setColor(QPalette::BrightText, Qt::white);
    p.setColor(QPalette::Light, Qt::white);
    p.setColor(QPalette::Midlight, QColor(0xe9, 0xe7, 0xe2));
    p.setColor(QPalette::Mid, QColor(0xa0, 0x9d, 0x96));
    p.setColor(QPalette::Dark, QColor(0x80, 0x80, 0x80));
    p.setColor(QPalette::Shadow, QColor(0x40, 0x40, 0x40));
    p.setColor(QPalette::Highlight, QColor(0x0a, 0x24, 0x6a));
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::Link, QColor(0x00, 0x00, 0xc0));
    for (auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        p.setColor(QPalette::Disabled, role, disabledText);
    p.setColor(QPalette::Disabled, QPalette::Base, face);
    return p;
}

// The classic Windows style draws a raised frame around a menu, and Windows puts its own shadow beside it, which looks
// like a second frame. With that style the menus go without the shadow, as menus did in its day.
class MenuShadows : public QObject {
public:
    using QObject::QObject;

    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (event->type() == QEvent::Polish || event->type() == QEvent::StyleChange)
            if (auto* menu = qobject_cast<QMenu*>(watched); menu && !menu->isVisible())
                menu->setWindowFlag(Qt::NoDropShadowWindowHint, classicFrames());
        return false;
    }

private:
    static bool classicFrames() { return QApplication::style()->name().compare("windows", Qt::CaseInsensitive) == 0; }
};

// Fusion with the system's palette (2026-11-09): the same on every system, and dark where the system is. The classic
// look, Windows with the classic palette, is a choice in View > Window Appearance.
QString defaultStyle()
{
    for (const QString& key : QStyleFactory::keys())
        if (key.compare("Fusion", Qt::CaseInsensitive) == 0)
            return key;
    return QApplication::style()->name();
}

}  // namespace

QStringList styles()
{
    QStringList keys = QStyleFactory::keys();
    // On Windows 10 it draws white text on light grey, whatever the palette.
    if (QOperatingSystemVersion::current() < QOperatingSystemVersion::Windows11)
        keys.removeIf([](const QString& key) { return key.compare("windows11", Qt::CaseInsensitive) == 0; });
    return keys;
}

QString style()
{
    const QString saved = QSettings().value(kStyleKey, defaultStyle()).toString();
    for (const QString& key : styles())
        if (key.compare(saved, Qt::CaseInsensitive) == 0)
            return key;
    return defaultStyle();
}

bool classicPalette() { return QSettings().value(kPaletteKey, false).toBool(); }

void apply(const QString& styleName, bool classic)
{
    static auto* shadows = [] {
        auto* filter = new MenuShadows(qApp);
        qApp->installEventFilter(filter);
        return filter;
    }();
    Q_UNUSED(shadows);
    if (QStyle* s = QStyleFactory::create(styleName))
        QApplication::setStyle(s);
    QApplication::setPalette(classic ? makeClassicPalette() : QApplication::style()->standardPalette());

    QSettings settings;
    settings.setValue(kStyleKey, styleName);
    settings.setValue(kPaletteKey, classic);
}

void applySaved() { apply(style(), classicPalette()); }

}  // namespace theme
