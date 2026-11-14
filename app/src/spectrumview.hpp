// SPDX-License-Identifier: GPL-3.0-or-later
// The spectrum display: level against frequency on a 10 x 10 graticule, linear or logarithmic in frequency,
// with markers on the fundamental and harmonics of one trace and a readout under the mouse. The wheel zooms
// the frequency span around the mouse (over the level labels it steps the reference level), dragging pans
// it and the level, a double click shows the whole span.
#pragma once

#include <optional>
#include <utility>

#include <QWidget>

#include "starosc/spectrum.hpp"
#include "types.hpp"

class SpectrumView : public QWidget {
    Q_OBJECT

public:
    explicit SpectrumView(QWidget* parent = nullptr);

    void setFrame(SpectrumFramePtr frame);
    void setSettings(const starosc::SpectrumSettings& settings);

    QRectF plotRect() const;
    // Frequencies at the left and right edges of the screen.
    std::pair<double, double> visibleSpan() const;
    // Shown level of `power` (Vrms squared) of a trace in its unit, as a y coordinate.
    double yOf(const QRectF& plot, double power, int source = 0) const;
    double xOf(const QRectF& plot, double hz) const;

    QSize sizeHint() const override { return {640, 512}; }
    QSize minimumSizeHint() const override { return {320, 256}; }

    static QColor traceColor(int source);

signals:
    // (0, 0): the whole span.
    void spanChanged(double start_hz, double stop_hz);
    void referenceLevelChanged(double top_dbv);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    double sampleRate() const;
    double binHz() const;
    double frequencyAt(const QRectF& plot, double x) const;
    double topLevel() const;  // in the unit
    QString levelText(double level) const;
    // A trace's unit: the chosen one, or the math product's and ratio's own (starosc::trace_unit).
    starosc::TraceUnit traceUnit(int source) const;
    void setSpan(double start_hz, double stop_hz);

    void drawGraticule(QPainter& p, const QRectF& plot) const;
    void drawTrace(QPainter& p, const QRectF& plot, int source) const;
    void drawMarkers(QPainter& p, const QRectF& plot) const;
    void drawReadouts(QPainter& p, const QRectF& plot) const;
    void drawHover(QPainter& p, const QRectF& plot) const;

    SpectrumFramePtr frame_;
    starosc::SpectrumSettings settings_;
    std::optional<QPointF> hover_;
    std::optional<QPointF> dragFrom_;
    std::pair<double, double> dragSpan_;
    double dragTop_ = 0;
    double wheelRemainder_ = 0;
};
