// SPDX-License-Identifier: GPL-3.0-or-later
// Thin strip above the display. Not zoomed: the envelope of the whole capture with the record on the screen framed.
// The frame stays in the middle and the capture is drawn around it as it lay around the record, so that the frame does
// not jump with where the trigger fell in each capture (a software trigger in a free-running capture); a part of the
// strip the capture did not reach stays empty. Zoomed: the envelope of the record with the part on the screen framed.
// The zoom is set from here too: drag to zoom into a part of the record or to move the zoomed part, wheel to step it,
// double-click to leave it.
#pragma once

#include <QWidget>

#include "starosc/settings.hpp"
#include "starosc/zoom.hpp"
#include "types.hpp"

class OverviewStrip : public QWidget {
    Q_OBJECT

public:
    explicit OverviewStrip(QWidget* parent = nullptr);

    void setFrame(FramePtr frame);
    void setSettings(const starosc::ScopeSettings& settings);

    // Where the frame is drawn, in the widget.
    QRectF frameRect() const;

    QSize sizeHint() const override { return {640, 24}; }
    QSize minimumSizeHint() const override { return {100, 24}; }

signals:
    // Fractions of the record.
    void zoomRangeSelected(double begin, double end);
    void zoomCenterChanged(double center);
    void zoomStepped(int steps);  // steps < 0: closer
    void zoomOffRequested();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    enum class Gesture { None, Select, Move };

    bool usable() const;
    bool zoomed() const;
    starosc::VisibleRange visibleRange() const;
    QRectF area() const;
    // What a horizontal position in the strip shows, as a fraction of the record.
    double recordFraction(double x) const;
    // Not zoomed: the record's share of the capture, and where the capture's start falls in the strip (fractions).
    double recordShare() const;
    double captureStart() const;

    FramePtr frame_;
    starosc::ScopeSettings settings_;
    Gesture gesture_ = Gesture::None;
    double gestureStart_ = 0, gestureNow_ = 0;  // Select: widget x
    double grabOffset_ = 0;                     // Move: pointer from the centre of the zoomed part
    double wheelRemainder_ = 0;
};
