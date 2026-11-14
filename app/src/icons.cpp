// SPDX-License-Identifier: GPL-3.0-or-later
#include "icons.hpp"

#include <utility>

#include <QGuiApplication>
#include <QIconEngine>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPixmap>

namespace icons {
namespace {

const QColor kGreen(0x2a, 0x9a, 0x2a);
const QColor kRed(0xc0, 0x20, 0x20);

class DrawnIcon : public QIconEngine {
public:
    DrawnIcon(QSize canvas, Draw draw) : canvas_(canvas), draw_(std::move(draw)) {}

    void paint(QPainter* painter, const QRect& rect, QIcon::Mode mode, QIcon::State) override
    {
        // The colours of the current palette, taken whenever the icon is drawn.
        const QPalette palette = QGuiApplication::palette();
        const QColor ink = mode == QIcon::Disabled ? palette.color(QPalette::Disabled, QPalette::ButtonText)
                           : mode == QIcon::Selected ? palette.color(QPalette::Active, QPalette::HighlightedText)
                                                     : palette.color(QPalette::Active, QPalette::ButtonText);
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->translate(rect.topLeft());
        painter->scale(rect.width() / double(canvas_.width()), rect.height() / double(canvas_.height()));
        painter->setPen(QPen(ink, 1));
        painter->setBrush(Qt::NoBrush);
        draw_(*painter, ink);
        painter->restore();
    }

    QPixmap pixmap(const QSize& size, QIcon::Mode mode, QIcon::State state) override { return scaledPixmap(size, mode, state, 1); }

    QPixmap scaledPixmap(const QSize& size, QIcon::Mode mode, QIcon::State state, qreal scale) override
    {
        const QSize fit = actualSize(size, mode, state);
        QPixmap pixmap(fit * scale);
        pixmap.setDevicePixelRatio(scale);
        pixmap.fill(Qt::transparent);
        QPainter p(&pixmap);
        paint(&p, QRect(QPoint(0, 0), fit), mode, state);
        return pixmap;
    }

    // As large as fits, in the canvas' shape.
    QSize actualSize(const QSize& size, QIcon::Mode, QIcon::State) override { return canvas_.scaled(size, Qt::KeepAspectRatio); }
    QIconEngine* clone() const override { return new DrawnIcon(canvas_, draw_); }

private:
    QSize canvas_;
    Draw draw_;
};

QIcon make(Draw draw) { return drawn(QSize(16, 16), std::move(draw)); }

QPolygonF points(std::initializer_list<QPointF> list) { return QPolygonF(QList<QPointF>(list)); }

void wave(QPainter& p, double x0, double y0, double dx, double dy)
{
    QPainterPath path(QPointF(x0, y0));
    path.cubicTo(x0 + 0.27 * dx, y0 - dy, x0 + 0.67 * dx, y0 - dy, x0 + dx, y0);
    path.cubicTo(x0 + 1.33 * dx, y0 + dy, x0 + 1.73 * dx, y0 + dy, x0 + 2 * dx, y0);
    p.drawPath(path);
}

}  // namespace

QIcon drawn(QSize canvas, Draw draw) { return QIcon(new DrawnIcon(canvas, std::move(draw))); }

QIcon run()
{
    return make([](QPainter& p, const QColor&) {
        p.setBrush(kGreen);
        p.drawPolygon(points({{4.5, 2.5}, {4.5, 13.5}, {13, 8}}));
    });
}

QIcon stop()
{
    return make([](QPainter& p, const QColor&) {
        p.setBrush(kRed);
        p.drawRect(QRectF(3.5, 3.5, 9, 9));
    });
}

QIcon single()
{
    return make([](QPainter& p, const QColor& ink) {
        p.setBrush(ink);
        p.setPen(Qt::NoPen);
        p.drawPolygon(points({{3.5, 2.5}, {3.5, 13.5}, {10.5, 8}}));
        p.drawRect(QRectF(11.5, 2.5, 2, 11));
    });
}

QIcon forceTrigger()
{
    return make([](QPainter& p, const QColor& ink) {
        p.setPen(QPen(ink, 1.5));
        p.drawLine(QPointF(8, 2), QPointF(8, 10.5));
        p.drawPolyline(points({{4.5, 7.5}, {8, 11}, {11.5, 7.5}}));
        p.setPen(QPen(ink, 1));
        p.drawLine(QPointF(2.5, 13.5), QPointF(13.5, 13.5));
    });
}

QIcon autoset()
{
    return make([](QPainter& p, const QColor& ink) {
        p.drawPolyline(points({{1.5, 5}, {1.5, 2.5}, {4.5, 2.5}}));
        p.drawPolyline(points({{11.5, 2.5}, {14.5, 2.5}, {14.5, 5}}));
        p.drawPolyline(points({{14.5, 11}, {14.5, 13.5}, {11.5, 13.5}}));
        p.drawPolyline(points({{4.5, 13.5}, {1.5, 13.5}, {1.5, 11}}));
        p.setPen(QPen(ink, 1.3));
        wave(p, 3.5, 8, 4.5, 4);
    });
}

QIcon cursors()
{
    return make([](QPainter& p, const QColor& ink) {
        QPen dashed(ink, 1, Qt::CustomDashLine);
        dashed.setDashPattern({2, 1});
        p.setPen(dashed);
        p.drawLine(QPointF(5.5, 1.5), QPointF(5.5, 14.5));
        p.drawLine(QPointF(10.5, 1.5), QPointF(10.5, 14.5));
        p.setPen(Qt::NoPen);
        p.setBrush(ink);
        p.drawPolygon(points({{3.5, 1.5}, {7.5, 1.5}, {5.5, 4}}));
        p.drawPolygon(points({{8.5, 1.5}, {12.5, 1.5}, {10.5, 4}}));
    });
}

QIcon xy()
{
    return make([](QPainter& p, const QColor& ink) {
        // A Lissajous ellipse across dotted axes.
        QPen axis(ink, 1, Qt::CustomDashLine);
        axis.setDashPattern({1, 1});
        p.setPen(axis);
        p.drawLine(QPointF(8, 1), QPointF(8, 15));
        p.drawLine(QPointF(1, 8), QPointF(15, 8));
        p.setPen(QPen(ink, 1.3));
        p.translate(8, 8);
        p.rotate(-35);
        p.drawEllipse(QPointF(0, 0), 6.6, 3.2);
    });
}

QIcon measurements()
{
    return make([](QPainter& p, const QColor&) {
        p.drawRect(QRectF(1.5, 5.5, 13, 5));
        for (double x : {3.5, 7.5, 11.5})
            p.drawLine(QPointF(x, 5.5), QPointF(x, 7.5));
        for (double x : {5.5, 9.5})
            p.drawLine(QPointF(x, 5.5), QPointF(x, 6.5));
    });
}

QIcon resetStatistics()
{
    return make([](QPainter& p, const QColor& ink) {
        p.setPen(QPen(ink, 1.4));
        p.drawPolyline(points({{12, 3.5}, {4.5, 3.5}, {8.5, 8}, {4.5, 12.5}, {12, 12.5}}));
    });
}

QIcon generator()
{
    return make([](QPainter& p, const QColor&) {
        p.drawRect(QRectF(1.5, 3.5, 13, 9));
        wave(p, 3, 8, 3.8, 3.5);
        p.setPen(Qt::NoPen);
        p.setBrush(kGreen);
        p.drawEllipse(QPointF(12.6, 5.6), 1.2, 1.2);
    });
}

QIcon exportData()
{
    return make([](QPainter& p, const QColor&) {
        p.drawPolygon(points({{3.5, 1.5}, {9.5, 1.5}, {12.5, 4.5}, {12.5, 14.5}, {3.5, 14.5}}));
        p.drawPolyline(points({{9.5, 1.5}, {9.5, 4.5}, {12.5, 4.5}}));
        for (double y : {7.5, 9.5, 11.5})
            p.drawLine(QPointF(5.5, y), QPointF(10.5, y));
    });
}

QIcon screenshot()
{
    return make([](QPainter& p, const QColor&) {
        p.drawRect(QRectF(1.5, 4.5, 13, 9));
        p.drawPolyline(points({{5.5, 4.5}, {6.5, 2.5}, {9.5, 2.5}, {10.5, 4.5}}));
        p.drawEllipse(QPointF(8, 9), 2.5, 2.5);
    });
}

QIcon resetLayout()
{
    return make([](QPainter& p, const QColor&) {
        p.drawRect(QRectF(1.5, 2.5, 13, 11));
        p.drawLine(QPointF(10.5, 2.5), QPointF(10.5, 13.5));
        p.drawLine(QPointF(1.5, 10.5), QPointF(10.5, 10.5));
    });
}

}  // namespace icons
