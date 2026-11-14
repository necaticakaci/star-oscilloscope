// SPDX-License-Identifier: GPL-3.0-or-later
#include "appicon.hpp"

#include <algorithm>
#include <cmath>

#include <QIconEngine>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QTransform>

namespace app_icon {
namespace {

const QColor kOutline(0x2e, 0x2e, 0x2e);
const QColor kHighlight(0xff, 0xff, 0xff);
const QColor kShadow(0x5a, 0x5a, 0x5a);
const QColor kFace(0xbd, 0xbd, 0xbd);
// The screen is a grey-green LCD with a dark trace (concept "LCD", 2026-11-09), like the displays of bench instruments.
const QColor kScreenEdge(0x55, 0x55, 0x55);
const QColor kScreen(0xb9, 0xc6, 0xa4);
const QColor kGrid(0x9a, 0xa9, 0x8a);
const QColor kTrace(0x1e, 0x2a, 0x1a);
const QColor kSparkle(0x4f, 0x93, 0xea);
const QColor kSparkleEdge(0x0f, 0x2f, 0x6a);

// Below this size the graticule is left out, the trace is drawn bolder and the sparkle a little larger.
constexpr int kSmall = 40;
// From 16 pixels up to this size the trace and the sparkle are placed for the pixels of a 16 × 16 icon.
constexpr int kTiny = 20;

// A four-pointed sparkle with concave sides; `waist` is how far the sides bow in, as a fraction of the radius.
QPainterPath sparkle(QPointF c, double r, double waist)
{
    const double w = waist * r;
    QPainterPath path(c + QPointF(0, -r));
    path.quadTo(c + QPointF(w, -w), c + QPointF(r, 0));
    path.quadTo(c + QPointF(w, w), c + QPointF(0, r));
    path.quadTo(c + QPointF(-w, w), c + QPointF(-r, 0));
    path.quadTo(c + QPointF(-w, -w), c + QPointF(0, -r));
    path.closeSubpath();
    return path;
}

// Two periods of a sine across the screen, in the 64 units the design is drawn on.
QPainterPath trace()
{
    QPainterPath path(QPointF(10, 41));
    double x = 10;
    for (int half = 0; half < 4; ++half) {
        const double peak = half % 2 == 0 ? 29.3 : 52.7;
        path.cubicTo(x + 3.6, peak, x + 7.4, peak, x + 11, 41);
        x += 11;
    }
    return path;
}

void draw(QPainter& p, int size)
{
    const double s = size / 64.0;
    const auto px = [s](double units) { return static_cast<int>(std::lround(units * s)); };
    const bool small = size < kSmall;

    // The frame in whole pixels: a dark outline, the bevel (light above and left, dark below and right), the face.
    const int margin = px(1.5);
    const int line = std::max(1, px(1));
    const int bevel = std::max(1, px(1.5));
    const QRect outer(margin, margin, size - 2 * margin, size - 2 * margin);
    p.fillRect(outer, kOutline);
    const QRect face = outer.adjusted(line, line, -line, -line);
    p.fillRect(face, kShadow);
    p.fillRect(face.adjusted(0, 0, -bevel, -bevel), kHighlight);
    p.fillRect(face.adjusted(bevel, bevel, -bevel, -bevel), kFace);

    const int inset = std::max(face.left() + bevel, px(8));
    const QRect frame(inset, inset, size - 2 * inset, size - 2 * inset);
    // The LCD is light: a dark edge sets it off from the face. A 16-pixel icon has no pixel to spare for it.
    const int edge = size < kTiny ? 0 : std::max(1, px(1));
    const QRect screen = frame.adjusted(edge, edge, -edge, -edge);
    p.fillRect(frame, kScreenEdge);
    p.fillRect(screen, kScreen);
    if (!small) {
        const int width = std::max(1, px(0.8));
        for (double at : {20.0, 32.0, 44.0}) {
            const int pos = static_cast<int>(std::lround(at * s - width / 2.0));
            p.fillRect(QRect(pos, screen.top(), width, screen.height()), kGrid);
            p.fillRect(QRect(screen.left(), pos, screen.width(), width), kGrid);
        }
    }

    if (size >= 16 && size < kTiny) {
        // Drawn for its pixels: one period of the trace placed pixel by pixel, and the sparkle inside the screen,
        // clear of the bevel, without the outline that would only blur it.
        constexpr int kWave[] = {10, 9, 8, 8, 9, 10, 11, 12, 12, 11, 10, 9};  // row of each column from x = 2
        for (int i = 0; i < static_cast<int>(std::size(kWave)); ++i)
            p.fillRect(QRect(2 + i, kWave[i], 1, 1), kTrace);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(kSparkle);
        p.drawPath(sparkle(QPointF(10.5, 5.5), 3.5, 0.3));
        return;
    }

    p.setRenderHint(QPainter::Antialiasing);
    p.save();
    p.setClipRect(screen);
    p.setPen(QPen(kTrace, std::max(1.4, 2.6 * s), Qt::SolidLine, Qt::FlatCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    p.drawPath(QTransform::fromScale(s, s).map(trace()));
    p.restore();

    // Centred on a pixel, so the thin arms fall on whole pixels.
    const QPointF centre(std::floor(44 * s) + 0.5, std::floor(18 * s) + 0.5);
    p.setPen(QPen(kSparkleEdge, small ? 0.8 : 1.2 * s, Qt::SolidLine, Qt::FlatCap, Qt::RoundJoin));
    p.setBrush(kSparkle);
    p.drawPath(sparkle(centre, (small ? 13 : 12) * s, small ? 0.3 : 0.22));
}

class Engine : public QIconEngine {
public:
    void paint(QPainter* painter, const QRect& rect, QIcon::Mode, QIcon::State) override
    {
        const int side = std::min(rect.width(), rect.height());
        const qreal scale = painter->device() ? painter->device()->devicePixelRatio() : 1;
        painter->drawImage(QRect(rect.topLeft(), QSize(side, side)), image(static_cast<int>(std::lround(side * scale))));
    }

    QPixmap pixmap(const QSize& size, QIcon::Mode mode, QIcon::State state) override { return scaledPixmap(size, mode, state, 1); }

    QPixmap scaledPixmap(const QSize& size, QIcon::Mode, QIcon::State, qreal scale) override
    {
        const int side = std::min(size.width(), size.height());
        QPixmap pixmap = QPixmap::fromImage(image(static_cast<int>(std::lround(side * scale))));
        pixmap.setDevicePixelRatio(scale);
        return pixmap;
    }

    QSize actualSize(const QSize& size, QIcon::Mode, QIcon::State) override
    {
        const int side = std::min(size.width(), size.height());
        return {side, side};
    }

    QList<QSize> availableSizes(QIcon::Mode, QIcon::State) override
    {
        QList<QSize> sizes;
        for (int size : kPngSizes)
            sizes.push_back({size, size});
        return sizes;
    }

    QIconEngine* clone() const override { return new Engine; }
};

}  // namespace

QIcon icon() { return QIcon(new Engine); }

QImage image(int size)
{
    QImage image(size, size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter p(&image);
    draw(p, size);
    return image;
}

}  // namespace app_icon
