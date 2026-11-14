// SPDX-License-Identifier: GPL-3.0-or-later
// The oscilloscope display: graticule, traces, math and reference traces, edge markers and cursors, or the
// X-Y plot, with a bar above (run state, timebase, memory strip, trigger) and a bar below (channel badges,
// measurements). Markers and cursors can be dragged; the wheel steps the scale (over a marker on the left) or
// the timebase. The measurements in the bar below change a few times a second, so they can be read. In X-Y the figure is
// dragged by hand and a double click centres it; CH1's marker on the bottom edge moves it sideways.
#pragma once

#include <array>
#include <span>
#include <vector>

#include <QStringList>
#include <QWidget>

#include "measurementselection.hpp"
#include "refreshlimiter.hpp"

#include "starosc/reference.hpp"
#include "starosc/settings.hpp"
#include "starosc/zoom.hpp"
#include "types.hpp"

class ScopeView : public QWidget {
    Q_OBJECT

public:
    enum class Marker { None, Channel1, Channel2, Math, Ref1, Ref2, TriggerLevel, TriggerPosition, Cursor1, Cursor2 };
    enum class OffScreen { No, Above, Below };
    enum class RunState { Stopped, Running, Waiting };

    explicit ScopeView(QWidget* parent = nullptr);

    void setFrame(FramePtr frame);
    void setSettings(const starosc::ScopeSettings& settings);
    void setCursors(const starosc::CursorSettings& cursors);
    void setReferences(const std::array<starosc::Reference, starosc::kReferences>& references);
    // With few samples on screen, draw the sin(x)/x reconstruction through them instead of straight lines.
    void setInterpolation(bool on);
    bool interpolation() const { return interpolate_; }
    // Stopped: the wheel over the plot zooms the record on the screen, as no new one would come.
    void setRunState(RunState state);
    // Draw the samples alone instead of lines through them.
    void setDots(bool on);
    bool dots() const { return dots_; }
    // The memory strip lives in the bar above the plot, which places it as the bar's text changes.
    void setOverview(QWidget* strip);
    void setMeasurementSelection(const MeasurementSelection& selection);
    // The measurements the bar below the plot would show if there is room: "Freq 1.000 kHz".
    QStringList measurementReadouts() const;
    // Why the channel badge at that position warns; empty anywhere else. Needs a painted display.
    QString warningAt(const QPointF& pos) const;

    QRectF plotRect() const;
    // Centre of a marker's grab area in widget coordinates.
    QPointF markerPosition(Marker marker) const;
    // Whether the whole trace of the current frame lies above or below the screen; an arrow at that edge says so.
    OffScreen offScreen(int channel) const;

    QSize sizeHint() const override { return {640, 576}; }
    QSize minimumSizeHint() const override { return {400, 320}; }


signals:
    void channelPositionChanged(int channel, double div);
    void mathPositionChanged(double div);
    void referencePositionChanged(int ref, double div);
    void triggerLevelChanged(double volts);
    void preTriggerChanged(double fraction);
    void voltsPerDivStepped(int channel, int steps);  // steps > 0: larger V/div
    void mathScaleStepped(int steps);
    void referenceScaleStepped(int ref, int steps);
    void timebaseStepped(int steps);                  // steps > 0: slower timebase
    void cursorsChanged(const starosc::CursorSettings& cursors);
    void zoomStepped(int steps, double xFraction);    // steps < 0: closer, around that part of the screen
    void zoomRangeSelected(double begin, double end); // fractions of the record
    void zoomCenterChanged(double center);            // fraction of the record
    void zoomOffRequested();

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    // Samples placed on the screen: `window` of them span its width and sample index `first` sits at its left edge.
    struct Series {
        std::span<const float> samples, before, after;
        double window = 0;
        double first = 0;
        double per_div = 1;
        double position_div = 0;
        QColor color;
        double samples_per_period = 0;  // of the signal, when known; a densely sampled one is drawn with straight lines
    };

    enum class Gesture { None, Select, Pan, Move };  // Move: the X-Y figure
    struct Readout {
        int trace;  // display_style::Trace, or -1 between the channels
        QString label, value;
    };
    struct Badge {
        QRectF rect;
        QString warning;
    };
    // Where the texts of the bar above the plot go, given their widths.
    struct TopBar {
        double left_end = 0;    // of the state and the timebase
        double rate_left = 0;   // of the sample rate; hidden when show_rate is false
        double trigger_left = 0;
        bool show_rate = true;
        double strip_left = 0, strip_right = 0;
    };


    bool xy() const { return settings_.display == starosc::DisplayMode::XY; }
    // The part of the record on the screen. The frame's own timebase says how long its record is.
    starosc::VisibleRange visibleRange() const;
    bool zoomed() const { return visibleRange().span < 1; }
    double recordFraction(const QRectF& plot, double x) const;
    // Left-edge markers: channels, math, references. Their scale and position, and whether they are shown.
    bool laneShown(Marker marker) const;
    double lanePerDiv(Marker marker) const;
    double lanePosition(Marker marker) const;
    double yOf(const QRectF& plot, double per_div, double position_div, double value) const;
    double toY(const QRectF& plot, int channel, double volts) const;
    double toDiv(const QRectF& plot, double y) const;  // divisions from the centre, + is up
    Marker markerAt(const QPointF& pos) const;
    void dragTo(Marker marker, const QPointF& pos);
    void setChannelPosition(int channel, double div);
    // X-Y: the middle of each signal on the centre of the screen.
    void centreFigure();
    void updateHoverCursor(const QPointF& pos);

    void drawGraticule(QPainter& p, const QRectF& plot) const;
    void drawSeries(QPainter& p, const QRectF& plot, const Series& series) const;
    void drawXY(QPainter& p, const QRectF& plot) const;
    void drawOffScreen(QPainter& p, const QRectF& plot) const;
    void drawCursors(QPainter& p, const QRectF& plot) const;
    void drawMarkers(QPainter& p, const QRectF& plot) const;
    void drawTriggerPosition(QPainter& p, const QRectF& plot) const;
    void drawTopBar(QPainter& p) const;
    void drawBottomBar(QPainter& p) const;
    QString horizontalText() const;
    QString triggerText() const;
    QString rateText() const;
    TopBar topBar() const;
    // Room for the run state ("RUN") and the trigger state ("Trig'd"), as wide as their longest translation.
    static double stateWidth(const QFontMetricsF& fm);
    static double triggeredWidth(const QFontMetricsF& fm);
    void placeOverview();
    std::vector<Readout> readouts() const;



    FramePtr frame_;
    FramePtr readoutFrame_;  // the one the measurements in the bar below come from
    RefreshLimiter readoutRefresh_{[this] {
        readoutFrame_ = frame_;
        update();
    }};
    starosc::ScopeSettings settings_;
    starosc::CursorSettings cursors_;
    std::array<starosc::Reference, starosc::kReferences> references_;
    Marker drag_ = Marker::None;
    double wheelRemainder_ = 0;
    bool interpolate_ = true;
    RunState runState_ = RunState::Stopped;
    bool dots_ = false;
    QWidget* overview_ = nullptr;
    MeasurementSelection selection_ = MeasurementSelection::defaults();
    mutable std::vector<Badge> badges_;
    Gesture gesture_ = Gesture::None;
    double gestureStart_ = 0, gestureNow_ = 0;  // widget x
    double panCenter_ = 0;                      // zoom centre when the pan began
    QPointF moveStart_;                         // X-Y: where the move began, and the channel positions then
    std::array<double, starosc::kChannels> moveFrom_{};
};
