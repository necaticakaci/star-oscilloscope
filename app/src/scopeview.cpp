// SPDX-License-Identifier: GPL-3.0-or-later
#include "scopeview.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>

#include "displaystyle.hpp"
#include "hoverhelp.hpp"
#include "starosc/interpolate.hpp"
#include "starosc/math.hpp"
#include "starosc/scale.hpp"

namespace {

constexpr double kEnvelopeOpacity = 0.45;
constexpr double kBarTop = 30;     // run state, timebase, memory strip, trigger
constexpr double kBarBottom = 32;  // channel badges, measurements
constexpr double kMarginLeft = 16;
constexpr double kMarginRight = 16;
constexpr double kMarginTop = 10;  // the trigger position mark, under the bar
constexpr double kMarginBottom = 4;
constexpr double kBarPad = 4;
constexpr int kReadoutsPerChannel = 2;
constexpr double kGrab = 7;  // half size of a marker's grab area, pixels
// With more than this many pixels a sample on screen, sin(x)/x is drawn instead of straight lines.
constexpr double kSincPixelsPerSample = 3;
constexpr double kMinSelection = 8;  // pixels; less is a click

const QColor kRunning(0x2a, 0x9a, 0x2a);
const QColor kWaiting(0xc8, 0x90, 0x20);
const QColor kStopped(0xc0, 0x20, 0x20);

using Marker = ScopeView::Marker;

// Digits on the display do not jump as values change.
QFont barFont()
{
    QFont font = display_style::fixedFont();
    font.setPixelSize(12);
    return font;
}

QString si(double value, std::string_view unit, int digits = 4, bool trim = false)
{
    return QString::fromStdString(starosc::format_si(value, unit, digits, trim));
}
constexpr Marker kLanes[] = {Marker::Channel1, Marker::Channel2, Marker::Math, Marker::Ref1, Marker::Ref2};

// Pentagon pointing right (or left) with a short label, centred on y.
void drawTag(QPainter& p, double x, double y, bool pointRight, const QColor& color, const QString& label)
{
    const double w = label.size() > 1 ? 18 : 14, h = 12, tip = 5;
    QPainterPath path;
    const double dir = pointRight ? 1 : -1;
    path.moveTo(x, y - h / 2);
    path.lineTo(x + dir * (w - tip), y - h / 2);
    path.lineTo(x + dir * w, y);
    path.lineTo(x + dir * (w - tip), y + h / 2);
    path.lineTo(x, y + h / 2);
    path.closeSubpath();
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    p.drawPath(path);
    p.setPen(display_style::textOn(color));
    const QRectF box = pointRight ? QRectF(x, y - h / 2, w - tip, h) : QRectF(x - w + tip, y - h / 2, w - tip, h);
    p.drawText(box, Qt::AlignCenter, label);
}

double roundTo(double value, double step) { return step > 0 ? std::round(value / step) * step : value; }

int refIndex(Marker m) { return m == Marker::Ref1 ? 0 : 1; }

}  // namespace

ScopeView::ScopeView(QWidget* parent) : QWidget(parent)
{
    setAttribute(Qt::WA_OpaquePaintEvent);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMouseTracking(true);
}

void ScopeView::setFrame(FramePtr frame)
{
    frame_ = std::move(frame);
    if (frame_) {
        readoutRefresh_.request();
    } else {
        readoutRefresh_.restart();
        readoutFrame_ = nullptr;
    }
    placeOverview();
    update();
}

void ScopeView::setSettings(const starosc::ScopeSettings& settings)
{
    settings_ = settings;
    placeOverview();
    update();
}

void ScopeView::setRunState(RunState state)
{
    runState_ = state;
    update();
}

void ScopeView::setDots(bool on)
{
    dots_ = on;
    update();
}

void ScopeView::setOverview(QWidget* strip)
{
    overview_ = strip;
    placeOverview();
}

void ScopeView::setMeasurementSelection(const MeasurementSelection& selection)
{
    selection_ = selection;
    update();
}

QString ScopeView::horizontalText() const
{
    if (xy())
        return tr("X-Y");
    QString text = QStringLiteral("H %1").arg(si(settings_.timebase.seconds_per_div, "s", 3, true));
    if (settings_.zoom.on())
        text += QStringLiteral("  Z %1").arg(si(settings_.zoom.seconds_per_div, "s", 3, true));
    return text;
}

QString ScopeView::triggerText() const
{
    const auto& t = settings_.trigger;
    const QChar edge = t.edge == starosc::TriggerEdge::Rising    ? QChar(0x2191)
                       : t.edge == starosc::TriggerEdge::Falling ? QChar(0x2193)
                                                                 : QChar(0x2195);
    static const char* const modes[] = {"Auto", "Normal", "Single"};
    return QStringLiteral("T%1 %2 %3 %4")
        .arg(t.source + 1)
        .arg(edge)
        .arg(si(t.level_v, "V"))
        .arg(tr(modes[static_cast<int>(t.mode)]));
}

QString ScopeView::rateText() const
{
    if (!frame_)
        return {};
    return si(frame_->plan.sample_rate_hz, "Sa/s", 3, true) + "  " + si(static_cast<double>(frame_->plan.window_samples), "pts", 3, true);
}

double ScopeView::stateWidth(const QFontMetricsF& fm)
{
    double widest = 0;
    for (const QString& text : {tr("RUN"), tr("WAIT"), tr("STOP")})
        widest = std::max(widest, fm.horizontalAdvance(text));
    return std::max(44.0, widest + 12);
}

double ScopeView::triggeredWidth(const QFontMetricsF& fm)
{
    return std::max(fm.horizontalAdvance(tr("Trig'd")), fm.horizontalAdvance(tr("Auto"))) + 12;
}

ScopeView::TopBar ScopeView::topBar() const
{
    const QFontMetricsF fm(barFont());
    TopBar bar;
    const double state = kBarPad + stateWidth(fm) + 8;
    const double triggered = triggeredWidth(fm);
    bar.left_end = state + triggered + fm.horizontalAdvance(horizontalText());
    bar.trigger_left = width() - kBarPad - fm.horizontalAdvance(triggerText()) - 12;
    const QString rate = rateText();
    bar.rate_left = bar.trigger_left - 10 - fm.horizontalAdvance(rate);
    bar.strip_left = bar.left_end + 10;
    bar.strip_right = bar.rate_left - 10;
    if (bar.strip_right - bar.strip_left < 80) {  // a narrow window: the sample rate is in the status bar too
        bar.show_rate = false;
        bar.strip_right = bar.trigger_left - 10;
    }
    return bar;
}

void ScopeView::placeOverview()
{
    if (!overview_)
        return;
    const TopBar bar = topBar();
    const double room = bar.strip_right - bar.strip_left;
    overview_->setVisible(!xy() && room >= 60);
    const QRect wanted(static_cast<int>(bar.strip_left), 4, static_cast<int>(room), static_cast<int>(kBarTop) - 8);
    if (overview_->geometry() != wanted)
        overview_->setGeometry(wanted);
}

void ScopeView::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    placeOverview();
}

QString ScopeView::warningAt(const QPointF& pos) const
{
    for (const Badge& badge : badges_)
        if (badge.rect.contains(pos))
            return badge.warning;
    return {};
}

std::vector<ScopeView::Readout> ScopeView::readouts() const
{
    std::vector<Readout> out;
    const starosc::Frame* frame = readoutFrame_.get();
    if (!frame || xy())
        return out;
    using starosc::Parameter;
    const auto add = [&](int trace, const starosc::Measurements& m, Parameter p, std::string_view unit) {
        QString value = m.unresolved(p) ? "< " + si(m.resolution_s, "s", 3) : formatMeasurement(p, m.value(p), unit);
        out.push_back({trace, shortParameterName(p), std::move(value)});
    };
    const auto contains = [](const std::vector<Parameter>& list, Parameter p) {
        return std::find(list.begin(), list.end(), p) != list.end();
    };
    for (int ch = 0; ch < starosc::kChannels; ++ch) {
        if (frame->volts[static_cast<std::size_t>(ch)].empty())
            continue;
        // Frequency and peak-to-peak when they are among the chosen ones, then the others in table order.
        const auto& list = selection_.trace[static_cast<std::size_t>(ch)];
        std::vector<Parameter> picked;
        for (Parameter p : {Parameter::Frequency, Parameter::PeakToPeak})
            if (contains(list, p))
                picked.push_back(p);
        for (Parameter p : list)
            if (static_cast<int>(picked.size()) < kReadoutsPerChannel && !contains(picked, p))
                picked.push_back(p);
        for (Parameter p : picked)
            add(ch, frame->measurements[static_cast<std::size_t>(ch)], p, "V");
    }
    if (!frame->math.empty() && !selection_.trace[starosc::kChannels].empty())
        add(display_style::Math, frame->math_measurements, selection_.trace[starosc::kChannels].front(),
            starosc::math_unit(frame->settings.math.operation));
    if (!frame->volts[0].empty() && !frame->volts[1].empty())
        for (Parameter p : selection_.pair)
            out.push_back({-1, shortParameterName(p), formatMeasurement(p, frame->pair.value(p))});
    return out;
}

QStringList ScopeView::measurementReadouts() const
{
    QStringList out;
    for (const Readout& r : readouts())
        out << QString(r.label + " " + r.value).remove(QChar(QChar::Nbsp));
    return out;
}

void ScopeView::setInterpolation(bool on)
{
    interpolate_ = on;
    update();
}

void ScopeView::setCursors(const starosc::CursorSettings& cursors)
{
    cursors_ = cursors;
    update();
}

void ScopeView::setReferences(const std::array<starosc::Reference, starosc::kReferences>& references)
{
    references_ = references;
    update();
}

QRectF ScopeView::plotRect() const
{
    return QRectF(rect()).adjusted(kMarginLeft, kBarTop + kMarginTop, -kMarginRight, -(kBarBottom + kMarginBottom));
}

bool ScopeView::laneShown(Marker m) const
{
    switch (m) {
    case Marker::Channel1: return settings_.channel[0].enabled && !xy();  // in X-Y, CH1 is the horizontal axis
    case Marker::Channel2: return settings_.channel[1].enabled;
    case Marker::Math: return settings_.math.enabled && !xy();
    case Marker::Ref1:
    case Marker::Ref2: {
        const auto& r = references_[static_cast<std::size_t>(refIndex(m))];
        return r.shown && !r.empty() && !xy();
    }
    default: return false;
    }
}

double ScopeView::lanePerDiv(Marker m) const
{
    switch (m) {
    case Marker::Channel1: return settings_.channel[0].volts_per_div;
    case Marker::Channel2: return settings_.channel[1].volts_per_div;
    case Marker::Math: return settings_.math.units_per_div;
    case Marker::Ref1:
    case Marker::Ref2: return references_[static_cast<std::size_t>(refIndex(m))].units_per_div;
    default: return 1;
    }
}

double ScopeView::lanePosition(Marker m) const
{
    switch (m) {
    case Marker::Channel1: return settings_.channel[0].position_div;
    case Marker::Channel2: return settings_.channel[1].position_div;
    case Marker::Math: return settings_.math.position_div;
    case Marker::Ref1:
    case Marker::Ref2: return references_[static_cast<std::size_t>(refIndex(m))].position_div;
    default: return 0;
    }
}

double ScopeView::yOf(const QRectF& plot, double per_div, double position_div, double value) const
{
    const double div = plot.height() / starosc::kDivisionsY;
    return plot.center().y() - (value / per_div + position_div) * div;
}

double ScopeView::toY(const QRectF& plot, int channel, double volts) const
{
    const auto& ch = settings_.channel[channel];
    return yOf(plot, ch.volts_per_div, ch.position_div, volts);
}

double ScopeView::toDiv(const QRectF& plot, double y) const
{
    return (plot.center().y() - y) / (plot.height() / starosc::kDivisionsY);
}

QPointF ScopeView::markerPosition(Marker marker) const
{
    const QRectF plot = plotRect();
    const auto clampY = [&](double y) { return std::clamp(y, plot.top(), plot.bottom()); };
    const double div = plot.height() / starosc::kDivisionsY;
    if (marker == Marker::Channel1 && xy()) {
        // CH1 is the horizontal axis: its zero, on the bottom edge.
        const double x = plot.center().x() + settings_.channel[0].position_div * plot.width() / starosc::kDivisionsX;
        return {std::clamp(x, plot.left(), plot.right()), plot.bottom() - 5};
    }
    switch (marker) {
    case Marker::Channel1:
    case Marker::Channel2:
    case Marker::Math:
    case Marker::Ref1:
    case Marker::Ref2:
        return {kMarginLeft / 2, clampY(yOf(plot, lanePerDiv(marker), lanePosition(marker), 0.0))};
    case Marker::TriggerLevel:
        return {width() - kMarginRight / 2, clampY(toY(plot, settings_.trigger.source, settings_.trigger.level_v))};
    case Marker::TriggerPosition: {
        const starosc::VisibleRange r = visibleRange();
        const double x = std::clamp((settings_.trigger.pre_trigger - r.begin) / r.span, 0.0, 1.0);
        return {plot.left() + x * plot.width(), plot.top() - kMarginTop / 2};
    }
    case Marker::Cursor1:
    case Marker::Cursor2: {
        const int i = marker == Marker::Cursor1 ? 0 : 1;
        if (cursors_.mode == starosc::CursorMode::Voltage)
            return {plot.center().x(), plot.center().y() - cursors_.y_div[i] * div};
        return {plot.left() + cursors_.x[i] * plot.width(), plot.center().y()};
    }
    case Marker::None: break;
    }
    return {};
}

starosc::VisibleRange ScopeView::visibleRange() const
{
    if (xy())
        return {};
    return starosc::visible_range(frame_ ? frame_->settings.timebase : settings_.timebase, settings_.zoom);
}

double ScopeView::recordFraction(const QRectF& plot, double x) const
{
    const starosc::VisibleRange r = visibleRange();
    return r.begin + std::clamp((x - plot.left()) / plot.width(), 0.0, 1.0) * r.span;
}

ScopeView::OffScreen ScopeView::offScreen(int channel) const
{
    const auto& cs = settings_.channel[channel];
    if (!frame_ || !cs.enabled || xy())
        return OffScreen::No;
    const auto& m = frame_->measurements[channel];
    if (!std::isfinite(m.min) || !std::isfinite(m.max))
        return OffScreen::No;
    const double half = starosc::kDivisionsY / 2.0;
    if (m.min > (half - cs.position_div) * cs.volts_per_div)
        return OffScreen::Above;
    if (m.max < (-half - cs.position_div) * cs.volts_per_div)
        return OffScreen::Below;
    return OffScreen::No;
}

ScopeView::Marker ScopeView::markerAt(const QPointF& pos) const
{
    const QRectF plot = plotRect();
    const auto isNear = [&](Marker m, bool horizontal) {
        const QPointF c = markerPosition(m);
        return horizontal ? std::abs(pos.x() - c.x()) <= kGrab : std::abs(pos.y() - c.y()) <= kGrab;
    };

    if (pos.y() < kBarTop || pos.y() > height() - kBarBottom)
        return Marker::None;
    if (pos.x() < kMarginLeft) {
        Marker best = Marker::None;
        double bestDistance = kGrab + 1;
        for (Marker m : kLanes) {
            if (!laneShown(m))
                continue;
            const double d = std::abs(pos.y() - markerPosition(m).y());
            if (d < bestDistance) {
                bestDistance = d;
                best = m;
            }
        }
        return best;
    }
    if (xy()) {
        const QPointF c = markerPosition(Marker::Channel1);
        const bool onIt = std::abs(pos.x() - c.x()) <= kGrab && std::abs(pos.y() - c.y()) <= kGrab;
        return settings_.channel[0].enabled && onIt ? Marker::Channel1 : Marker::None;
    }
    if (pos.x() > width() - kMarginRight)
        return isNear(Marker::TriggerLevel, false) ? Marker::TriggerLevel : Marker::None;
    if (pos.y() < plot.top())  // on a part of the record, the marker only shows where the trigger is
        return !zoomed() && isNear(Marker::TriggerPosition, true) ? Marker::TriggerPosition : Marker::None;
    if (plot.contains(pos) && cursors_.mode != starosc::CursorMode::Off) {
        const bool vertical = cursors_.mode == starosc::CursorMode::Time;
        for (Marker m : {Marker::Cursor2, Marker::Cursor1})
            if (isNear(m, vertical))
                return m;
    }
    return Marker::None;
}

void ScopeView::dragTo(Marker marker, const QPointF& pos)
{
    const QRectF plot = plotRect();
    const double half = starosc::kDivisionsY / 2.0;
    const double lane = std::clamp(roundTo(toDiv(plot, pos.y()), 0.02), -half, half);
    switch (marker) {
    case Marker::Channel1:
        if (xy()) {
            setChannelPosition(0, (pos.x() - plot.center().x()) / (plot.width() / starosc::kDivisionsX));
            break;
        }
        setChannelPosition(0, lane);
        break;
    case Marker::Channel2:
        setChannelPosition(1, lane);
        break;
    case Marker::Math:
        settings_.math.position_div = lane;
        emit mathPositionChanged(lane);
        break;
    case Marker::Ref1:
    case Marker::Ref2:
        references_[static_cast<std::size_t>(refIndex(marker))].position_div = lane;
        emit referencePositionChanged(refIndex(marker), lane);
        break;
    case Marker::TriggerLevel: {
        const auto& src = settings_.channel[settings_.trigger.source];
        const double div = std::clamp(toDiv(plot, pos.y()), -half, half);
        const double volts = roundTo((div - src.position_div) * src.volts_per_div, src.volts_per_div / 50);
        settings_.trigger.level_v = volts;
        emit triggerLevelChanged(volts);
        break;
    }
    case Marker::TriggerPosition: {
        const double fraction = std::clamp(roundTo((pos.x() - plot.left()) / plot.width(), 0.01), 0.05, 0.95);
        settings_.trigger.pre_trigger = fraction;
        emit preTriggerChanged(fraction);
        break;
    }
    case Marker::Cursor1:
    case Marker::Cursor2: {
        const int i = marker == Marker::Cursor1 ? 0 : 1;
        if (cursors_.mode == starosc::CursorMode::Voltage)
            cursors_.y_div[i] = std::clamp(toDiv(plot, pos.y()), -half, half);
        else
            cursors_.x[i] = std::clamp((pos.x() - plot.left()) / plot.width(), 0.0, 1.0);
        emit cursorsChanged(cursors_);
        break;
    }
    case Marker::None: return;
    }
    update();
}

void ScopeView::setChannelPosition(int channel, double div)
{
    const double half = starosc::kDivisionsY / 2.0;
    div = std::clamp(roundTo(div, 0.02), -half, half);
    if (div == settings_.channel[channel].position_div)
        return;
    settings_.channel[channel].position_div = div;
    emit channelPositionChanged(channel, div);
}

void ScopeView::centreFigure()
{
    for (int ch = 0; ch < starosc::kChannels; ++ch) {
        double div = 0;
        if (frame_) {
            const auto& m = frame_->measurements[static_cast<std::size_t>(ch)];
            if (std::isfinite(m.min) && std::isfinite(m.max))
                div = -(m.max + m.min) / 2 / settings_.channel[ch].volts_per_div;
        }
        setChannelPosition(ch, div);
    }
}

void ScopeView::updateHoverCursor(const QPointF& pos)
{
    if (gesture_ != Gesture::None) {
        setCursor(gesture_ == Gesture::Select ? Qt::SizeHorCursor : Qt::ClosedHandCursor);
        return;
    }
    switch (drag_ != Marker::None ? drag_ : markerAt(pos)) {
    case Marker::Channel1:
        setCursor(xy() ? Qt::SizeHorCursor : Qt::SizeVerCursor);
        break;
    case Marker::Channel2:
    case Marker::Math:
    case Marker::Ref1:
    case Marker::Ref2:
    case Marker::TriggerLevel: setCursor(Qt::SizeVerCursor); break;
    case Marker::TriggerPosition: setCursor(Qt::SizeHorCursor); break;
    case Marker::Cursor1:
    case Marker::Cursor2:
        setCursor(cursors_.mode == starosc::CursorMode::Voltage ? Qt::SizeVerCursor : Qt::SizeHorCursor);
        break;
    case Marker::None:
        if ((zoomed() || xy()) && plotRect().contains(pos))
            setCursor(Qt::OpenHandCursor);
        else
            unsetCursor();
        break;
    }
}

void ScopeView::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton)
        return QWidget::mousePressEvent(event);
    drag_ = markerAt(event->position());
    if (drag_ == Marker::None && xy() && plotRect().contains(event->position())) {
        // Away from the marker, a drag in X-Y moves the figure.
        gesture_ = Gesture::Move;
        moveStart_ = event->position();
        moveFrom_ = {settings_.channel[0].position_div, settings_.channel[1].position_div};
    } else if (drag_ == Marker::None && !xy() && plotRect().contains(event->position())) {
        // Away from the markers, a drag zooms into the part it covers or moves the zoomed part.
        const starosc::VisibleRange r = visibleRange();
        gesture_ = zoomed() ? Gesture::Pan : Gesture::Select;
        gestureStart_ = gestureNow_ = event->position().x();
        panCenter_ = r.begin + r.span / 2;
    }
    updateHoverCursor(event->position());
}

void ScopeView::mouseMoveEvent(QMouseEvent* event)
{
    if (drag_ != Marker::None) {
        dragTo(drag_, event->position());
    } else if (gesture_ == Gesture::Move) {
        const QRectF plot = plotRect();
        const QPointF moved = event->position() - moveStart_;
        setChannelPosition(0, moveFrom_[0] + moved.x() / (plot.width() / starosc::kDivisionsX));
        setChannelPosition(1, moveFrom_[1] - moved.y() / (plot.height() / starosc::kDivisionsY));
    } else if (gesture_ != Gesture::None) {
        gestureNow_ = event->position().x();
        if (gesture_ == Gesture::Pan) {
            const double moved = (gestureNow_ - gestureStart_) / plotRect().width() * visibleRange().span;
            emit zoomCenterChanged(panCenter_ - moved);
        }
        update();
    }
    updateHoverCursor(event->position());
    // Why a badge warns, in the status bar while the pointer is on it.
    hover_help::say(this, warningAt(event->position()));
}

void ScopeView::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        drag_ = Marker::None;
        if (gesture_ == Gesture::Select && std::abs(gestureNow_ - gestureStart_) >= kMinSelection) {
            const QRectF plot = plotRect();
            emit zoomRangeSelected(recordFraction(plot, gestureStart_), recordFraction(plot, gestureNow_));
        }
        gesture_ = Gesture::None;
        update();
    }
    updateHoverCursor(event->position());
}

void ScopeView::mouseDoubleClickEvent(QMouseEvent* event)
{
    const Marker m = markerAt(event->position());
    switch (m) {
    case Marker::Channel1:
    case Marker::Channel2: {
        const int ch = m == Marker::Channel1 ? 0 : 1;
        settings_.channel[ch].position_div = 0;
        emit channelPositionChanged(ch, 0);
        break;
    }
    case Marker::Math:
        settings_.math.position_div = 0;
        emit mathPositionChanged(0);
        break;
    case Marker::Ref1:
    case Marker::Ref2:
        references_[static_cast<std::size_t>(refIndex(m))].position_div = 0;
        emit referencePositionChanged(refIndex(m), 0);
        break;
    case Marker::TriggerLevel: {
        // Middle of the source signal, like the "set to 50%" key of bench scopes.
        double level = 0;
        if (frame_) {
            const auto& meas = frame_->measurements[settings_.trigger.source];
            if (std::isfinite(meas.max) && std::isfinite(meas.min))
                level = (meas.max + meas.min) / 2;
        }
        const auto& src = settings_.channel[settings_.trigger.source];
        settings_.trigger.level_v = roundTo(level, src.volts_per_div / 50);
        emit triggerLevelChanged(settings_.trigger.level_v);
        break;
    }
    case Marker::TriggerPosition:
        settings_.trigger.pre_trigger = 0.5;
        emit preTriggerChanged(0.5);
        break;
    default:
        if (xy() && plotRect().contains(event->position()))
            centreFigure();
        else if (zoomed() && plotRect().contains(event->position()))
            emit zoomOffRequested();
        else
            QWidget::mouseDoubleClickEvent(event);
        return;
    }
    update();
}

void ScopeView::wheelEvent(QWheelEvent* event)
{
    wheelRemainder_ += event->angleDelta().y() / 120.0;
    const int notches = static_cast<int>(wheelRemainder_);
    if (notches == 0)
        return;
    wheelRemainder_ -= notches;

    // Wheel up zooms in: a smaller scale or a faster timebase.
    const QPointF pos = event->position();
    switch (const Marker m = markerAt(pos)) {
    case Marker::Channel1:
    case Marker::Channel2: emit voltsPerDivStepped(m == Marker::Channel1 ? 0 : 1, -notches); break;
    case Marker::Math: emit mathScaleStepped(-notches); break;
    case Marker::Ref1:
    case Marker::Ref2: emit referenceScaleStepped(refIndex(m), -notches); break;
    default: {
        const QRectF plot = plotRect();
        if (!plot.contains(pos) || xy())
            break;
        // Zoomed or stopped, the wheel moves through the record; Ctrl gets there from a running, full screen.
        if (zoomed() || runState_ == RunState::Stopped || (event->modifiers() & Qt::ControlModifier))
            emit zoomStepped(-notches, (pos.x() - plot.left()) / plot.width());
        else
            emit timebaseStepped(-notches);
    }
    }
    event->accept();
}

void ScopeView::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.fillRect(rect(), display_style::background());
    const QRectF plot = plotRect();
    drawGraticule(p, plot);

    p.save();
    p.setClipRect(plot);
    if (xy()) {
        drawXY(p, plot);
    } else {
        // References behind the live traces; screen time runs from the left edge as the timebase has it now.
        const starosc::VisibleRange range = visibleRange();
        const double record = starosc::kDivisionsX * settings_.timebase.seconds_per_div;
        const double span = record * range.span;
        const double left = starosc::screen_start_s(settings_) + record * range.begin;
        for (int r = starosc::kReferences - 1; r >= 0; --r) {
            const starosc::Reference& ref = references_[static_cast<std::size_t>(r)];
            if (!laneShown(r == 0 ? Marker::Ref1 : Marker::Ref2) || !(ref.sample_interval_s > 0))
                continue;
            drawSeries(p, plot,
                       {ref.samples, {}, {}, span / ref.sample_interval_s, (left - ref.start_s) / ref.sample_interval_s,
                        ref.units_per_div, ref.position_div, display_style::reference(r)});
        }
        if (frame_) {
            const auto record = static_cast<double>(frame_->plan.window_samples);
            const double window = record * range.span;
            const double first = frame_->start_fraction + record * range.begin;
            // Samples a period, from the measured frequency: sin(x)/x only where it helps.
            const auto per_period = [&](const starosc::Measurements& m) {
                return m.frequency > 0 && frame_->sample_interval_s > 0 ? 1 / (m.frequency * frame_->sample_interval_s) : 0.0;
            };
            for (int ch = starosc::kChannels - 1; ch >= 0; --ch) {
                const auto& cs = settings_.channel[ch];
                if (cs.enabled)
                    drawSeries(p, plot,
                               {frame_->volts[ch], frame_->before[ch], frame_->after[ch], window, first,
                                cs.volts_per_div, cs.position_div, display_style::channel(ch),
                                per_period(frame_->measurements[ch])});
            }
            const auto& ms = settings_.math;
            if (ms.enabled)
                drawSeries(p, plot,
                           {frame_->math, frame_->math_before, frame_->math_after, window, first, ms.units_per_div,
                            ms.position_div, display_style::math(), per_period(frame_->math_measurements)});
        }
        if (gesture_ == Gesture::Select) {
            const double a = std::clamp(std::min(gestureStart_, gestureNow_), plot.left(), plot.right());
            const double b = std::clamp(std::max(gestureStart_, gestureNow_), plot.left(), plot.right());
            QColor fill = display_style::mark();
            fill.setAlpha(40);
            p.fillRect(QRectF(a, plot.top(), b - a, plot.height()), fill);
        }
    }
    p.restore();
    if (frame_ && !xy())
        drawOffScreen(p, plot);
    if (!xy())
        drawCursors(p, plot);
    drawMarkers(p, plot);
    drawTopBar(p);
    drawBottomBar(p);
}

void ScopeView::drawTopBar(QPainter& p) const
{
    p.setRenderHint(QPainter::Antialiasing, false);
    p.setFont(barFont());
    const QFontMetricsF fm(p.font());
    const double baseline = (kBarTop + fm.ascent() - fm.descent()) / 2;
    const TopBar bar = topBar();

    const auto state = runState_ == RunState::Running   ? std::pair{tr("RUN"), kRunning}
                       : runState_ == RunState::Waiting ? std::pair{tr("WAIT"), kWaiting}
                                                        : std::pair{tr("STOP"), kStopped};
    const QRectF box(kBarPad, 5, stateWidth(fm), kBarTop - 10);
    p.fillRect(box, state.second);
    p.setPen(display_style::textOn(state.second));
    p.drawText(box, Qt::AlignCenter, state.first);
    double x = box.right() + 8;

    if (frame_) {
        p.setPen(frame_->triggered ? kRunning : kWaiting);
        p.drawText(QPointF(x, baseline), frame_->triggered ? tr("Trig'd") : tr("Auto"));
    }
    x += triggeredWidth(fm);
    p.setPen(display_style::strongText());
    p.drawText(QPointF(x, baseline), horizontalText());

    if (bar.show_rate) {
        p.setPen(display_style::text());
        p.drawText(QPointF(bar.rate_left, baseline), rateText());
    }

    const QColor source = display_style::channel(settings_.trigger.source);
    const QRectF trigger(bar.trigger_left, 5, width() - kBarPad - bar.trigger_left, kBarTop - 10);
    p.setPen(display_style::hairline(display_style::axis(), p.device()));
    p.setBrush(Qt::NoBrush);
    p.drawRect(trigger.adjusted(0.5, 0.5, -0.5, -0.5));
    p.setPen(source);
    p.drawText(trigger, Qt::AlignCenter, triggerText());
}

void ScopeView::drawBottomBar(QPainter& p) const
{
    p.setRenderHint(QPainter::Antialiasing, false);
    p.setFont(barFont());
    const QFontMetricsF fm(p.font());
    const double top = height() - kBarBottom + 5;
    const double h = kBarBottom - 9;
    const double baseline = top + (h + fm.ascent() - fm.descent()) / 2;
    double x = kBarPad;
    badges_.clear();

    const auto badge = [&](const QColor& color, const QString& tag, const QString& text, const QString& warning,
                           const QColor& warningColor, const QString& why) {
        const double tagWidth = fm.horizontalAdvance(tag) + 10;
        double width = tagWidth + fm.horizontalAdvance(text) + 10;
        if (!warning.isEmpty())
            width += fm.horizontalAdvance(warning) + 8;
        const QRectF rect(x, top, width, h);
        const QRectF tagRect(x, top, tagWidth, h);
        p.fillRect(tagRect, color);
        p.setPen(display_style::textOn(color));
        p.drawText(tagRect, Qt::AlignCenter, tag);
        p.setPen(display_style::hairline(color, p.device()));
        p.setBrush(Qt::NoBrush);
        p.drawRect(rect.adjusted(0.5, 0.5, -0.5, -0.5));
        p.setPen(display_style::strongText());
        p.drawText(QPointF(x + tagWidth + 5, baseline), text);
        if (!warning.isEmpty()) {
            p.setPen(warningColor);
            p.drawText(QPointF(x + width - fm.horizontalAdvance(warning) - 5, baseline), warning);
        }
        badges_.push_back({rect, why});
        x += width + 4;
    };

    for (int ch = 0; ch < starosc::kChannels; ++ch) {
        const auto& cs = settings_.channel[static_cast<std::size_t>(ch)];
        if (!cs.enabled) {
            badge(display_style::axis(), QString::number(ch + 1), tr("off"), {}, {}, {});
            continue;
        }
        QString text = si(cs.volts_per_div, "V/", 3, true) + " " + (cs.coupling == starosc::Coupling::DC ? "DC" : "AC");
        if (cs.probe != 1)
            text += QStringLiteral(" ×") + QString::number(cs.probe, 'g', 6);
        QString warning, why;
        QColor warningColor;
        if (frame_ && frame_->clipped[static_cast<std::size_t>(ch)]) {
            warning = tr("CLIP");
            warningColor = kStopped;
            why = tr("The signal exceeds the input range of the selected gain; increase V/div.");
        } else if (frame_ && frame_->aliased[static_cast<std::size_t>(ch)]) {
            warning = tr("ALIAS");
            warningColor = kWaiting;
            why = tr("The signal is faster than half the sample rate (%1), so the trace shows a false, slower wave; "
                     "choose a faster timebase.")
                      .arg(si(frame_->plan.sample_rate_hz / 2.0, "Hz", 3, true));
        }
        badge(display_style::channel(ch), QString::number(ch + 1), text, warning, warningColor, why);
    }
    const auto& ms = settings_.math;
    if (ms.enabled && !xy())
        badge(display_style::math(), QStringLiteral("M"),
              QStringLiteral("%1%2%3 %4")
                  .arg(ms.a + 1)
                  .arg(QString::fromUtf8(starosc::math_symbol(ms.operation).data()))
                  .arg(ms.b + 1)
                  .arg(si(ms.units_per_div, std::string(starosc::math_unit(ms.operation)) + "/", 3, true)),
              {}, {}, {});
    for (int r = 0; r < starosc::kReferences; ++r)
        if (laneShown(r == 0 ? Marker::Ref1 : Marker::Ref2)) {
            const auto& ref = references_[static_cast<std::size_t>(r)];
            badge(display_style::reference(r), QString("R%1").arg(r + 1), si(ref.units_per_div, ref.unit + "/", 3, true), {}, {}, {});
        }

    x += 4;
    for (const Readout& r : readouts()) {
        const QString label = r.label + " ";
        const double wide = fm.horizontalAdvance(label) + fm.horizontalAdvance(r.value);
        if (x + wide > width() - kBarPad)
            break;
        p.setPen(r.trace < 0 ? display_style::text() : display_style::traceColor(r.trace));
        p.drawText(QPointF(x, baseline), label);
        p.setPen(display_style::strongText());
        p.drawText(QPointF(x + fm.horizontalAdvance(label), baseline), r.value);
        x += wide + 8;
    }
}

void ScopeView::drawGraticule(QPainter& p, const QRectF& plot) const
{
    const double dx = plot.width() / starosc::kDivisionsX;
    const double dy = plot.height() / starosc::kDivisionsY;

    QPen dotted = display_style::hairline(display_style::grid(), p.device(), Qt::CustomDashLine);
    dotted.setDashPattern({1, 3});
    p.setPen(dotted);
    for (int i = 1; i < starosc::kDivisionsX; ++i)
        if (i != starosc::kDivisionsX / 2)
            p.drawLine(QPointF(std::round(plot.left() + i * dx) + 0.5, plot.top()),
                       QPointF(std::round(plot.left() + i * dx) + 0.5, plot.bottom()));
    for (int i = 1; i < starosc::kDivisionsY; ++i)
        if (i != starosc::kDivisionsY / 2)
            p.drawLine(QPointF(plot.left(), std::round(plot.top() + i * dy) + 0.5),
                       QPointF(plot.right(), std::round(plot.top() + i * dy) + 0.5));

    p.setPen(display_style::hairline(display_style::axis(), p.device()));
    const double cx = std::round(plot.center().x()) + 0.5;
    const double cy = std::round(plot.center().y()) + 0.5;
    p.drawLine(QPointF(cx, plot.top()), QPointF(cx, plot.bottom()));
    p.drawLine(QPointF(plot.left(), cy), QPointF(plot.right(), cy));
    for (int i = 1; i < starosc::kDivisionsX * 5; ++i) {
        const double x = std::round(plot.left() + i * dx / 5) + 0.5;
        p.drawLine(QPointF(x, cy - 3), QPointF(x, cy + 3));
    }
    for (int i = 1; i < starosc::kDivisionsY * 5; ++i) {
        const double y = std::round(plot.top() + i * dy / 5) + 0.5;
        p.drawLine(QPointF(cx - 3, y), QPointF(cx + 3, y));
    }
    p.drawRect(plot.adjusted(0.5, 0.5, -0.5, -0.5));
}

void ScopeView::drawSeries(QPainter& p, const QRectF& plot, const Series& s) const
{
    const auto& v = s.samples;
    if (v.size() < 2 || !(s.window > 0))
        return;
    const double width = plot.width();
    const auto y = [&](double value) { return yOf(plot, s.per_div, s.position_div, value); };
    const bool envelope = s.window > 2 * width;
    // NaN (a ratio with no divisor) breaks the line.
    std::vector<QPolygonF> lines(1);
    std::vector<QPolygonF> peaks(1);  // with the envelope; `lines` then has the means
    const auto gap = [&] {
        if (!lines.back().isEmpty())
            lines.emplace_back();
        if (!peaks.back().isEmpty())
            peaks.emplace_back();
    };
    const int columns = static_cast<int>(std::ceil(width));

    if (envelope) {
        // More samples than pixels: the min/max envelope of each pixel column, and its mean.
        for (int px = 0; px < columns; ++px) {
            const double a = px / width * s.window + s.first;
            const double b = (px + 1) / width * s.window + s.first;
            if (b <= 0 || a >= static_cast<double>(v.size())) {
                gap();
                continue;
            }
            const auto from = static_cast<std::size_t>(std::max(0.0, a));
            const auto to = std::min(v.size(), static_cast<std::size_t>(b) + 1);
            float lo = std::numeric_limits<float>::infinity(), hi = -lo;
            double sum = 0;
            std::size_t count = 0;
            for (std::size_t i = from; i < to; ++i)
                if (std::isfinite(v[i])) {
                    lo = std::min(lo, v[i]);
                    hi = std::max(hi, v[i]);
                    sum += v[i];
                    ++count;
                }
            if (lo > hi) {
                gap();
                continue;
            }
            const double x = plot.left() + px + 0.5;
            peaks.back() << QPointF(x, y(px % 2 ? hi : lo)) << QPointF(x, y(px % 2 ? lo : hi));
            lines.back() << QPointF(x, y(sum / static_cast<double>(count)));
        }
    } else if (interpolate_ && !dots_ && s.window * kSincPixelsPerSample < width && starosc::sinc_helps(s.samples_per_period) &&
               std::all_of(v.begin(), v.end(), [](float x) { return std::isfinite(x); })) {
        // Few samples on screen: the band-limited signal through them, one point per pixel column.
        const auto values = starosc::sinc_interpolate(v, s.before, s.after, s.first, s.window / width,
                                                      static_cast<std::size_t>(columns) + 1);
        const double lowest = -static_cast<double>(s.before.size());
        const double highest = static_cast<double>(v.size() - 1 + s.after.size());
        for (int px = 0; px <= columns; ++px) {
            const double at = s.first + px * s.window / width;
            if (at < lowest || at > highest) {
                gap();
                continue;
            }
            lines.back() << QPointF(plot.left() + px, y(values[static_cast<std::size_t>(px)]));
        }
    } else {
        // Only what can reach the screen; a zoomed record has far more.
        const auto size = static_cast<double>(v.size());
        const auto from = static_cast<std::size_t>(std::clamp(std::floor(s.first) - 1, 0.0, size));
        const auto to = static_cast<std::size_t>(std::clamp(std::ceil(s.first + s.window) + 2, 0.0, size));
        for (std::size_t i = from; i < to; ++i) {
            if (!std::isfinite(v[i])) {
                gap();
                continue;
            }
            lines.back() << QPointF(plot.left() + (static_cast<double>(i) - s.first) / s.window * width, y(v[i]));
        }
    }
    if (dots_ && !envelope) {
        // Every sample a square of two pixels: where the data is, with nothing drawn between.
        p.setRenderHint(QPainter::Antialiasing, false);
        p.setPen(QPen(s.color, 2, Qt::SolidLine, Qt::SquareCap));
        for (const QPolygonF& line : lines)
            p.drawPoints(line);
        return;
    }
    // The peaks stay in sight, faint, so noise and a spike of one sample do not go unseen. The mean through them is the
    // trace: many samples a column average out the ADC's steps, which are 2 to 3 pixels on a usual screen and would
    // show as stairs (docs/building.md §1.1, Performance).
    if (envelope) {
        QColor faint = s.color;
        faint.setAlphaF(kEnvelopeOpacity);
        p.setRenderHint(QPainter::Antialiasing, false);
        p.setPen(display_style::hairline(faint, p.device()));
        for (const QPolygonF& line : peaks)
            if (line.size() >= 2)
                p.drawPolyline(line);
    }
    // A pen wider than one device pixel leaves Qt's fast line drawing: at 1.3 px the traces took most of the CPU time,
    // and so does a pen of one logical pixel on a screen scaled to 125 % (display_style::hairline). An antialiased
    // line of one pixel is spread over two and looks dim, so it is drawn twice.
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(display_style::hairline(s.color, p.device()));
    for (const QPolygonF& line : lines)
        if (line.size() >= 2) {
            p.drawPolyline(line);
            p.drawPolyline(line);
        }
}

void ScopeView::drawXY(QPainter& p, const QRectF& plot) const
{
    const bool both = settings_.channel[0].enabled && settings_.channel[1].enabled;
    const std::size_t n = frame_ && both ? std::min(frame_->volts[0].size(), frame_->volts[1].size()) : 0;
    if (n < 2) {
        QFont font = p.font();
        font.setPixelSize(12);
        p.setFont(font);
        p.setPen(display_style::mark());
        p.drawText(plot, Qt::AlignCenter, both ? tr("Waiting for samples") : tr("X-Y needs CH1 and CH2"));
        return;
    }
    const auto& cx = settings_.channel[0];
    const double dx = plot.width() / starosc::kDivisionsX;
    QPolygonF line;
    line.reserve(static_cast<qsizetype>(n));
    for (std::size_t i = 0; i < n; ++i)
        line << QPointF(plot.center().x() + (frame_->volts[0][i] / cx.volts_per_div + cx.position_div) * dx,
                        toY(plot, 1, frame_->volts[1][i]));
    p.setRenderHint(QPainter::Antialiasing, static_cast<double>(n) < 4 * plot.width());
    p.setPen(display_style::hairline(display_style::traceColor(display_style::XY), p.device()));
    p.drawPolyline(line);
}

void ScopeView::drawOffScreen(QPainter& p, const QRectF& plot) const
{
    QFont font = p.font();
    font.setPixelSize(10);
    font.setBold(true);
    p.setFont(font);
    for (int ch = 0; ch < starosc::kChannels; ++ch) {
        const OffScreen where = offScreen(ch);
        if (where == OffScreen::No)
            continue;
        // Triangle with its tip on the edge the trace went past, and the channel name beside it.
        const bool above = where == OffScreen::Above;
        const double x = plot.left() + 6 + ch * 46;
        const double tip = above ? plot.top() + 3 : plot.bottom() - 3;
        const double base = above ? tip + 8 : tip - 8;
        QPainterPath arrow;
        arrow.moveTo(x + 5, tip);
        arrow.lineTo(x + 10, base);
        arrow.lineTo(x, base);
        arrow.closeSubpath();
        p.setPen(Qt::NoPen);
        p.setBrush(display_style::channel(ch));
        p.drawPath(arrow);
        p.setPen(display_style::channel(ch));
        p.drawText(QRectF(x + 13, std::min(tip, base), 30, 8), Qt::AlignLeft | Qt::AlignVCenter,
                   tr("CH%1").arg(ch + 1));
    }
}

void ScopeView::drawCursors(QPainter& p, const QRectF& plot) const
{
    if (cursors_.mode == starosc::CursorMode::Off)
        return;
    QPen pen = display_style::hairline(display_style::mark(), p.device(), Qt::DashLine);
    QFont font = p.font();
    font.setPixelSize(10);
    p.setFont(font);
    for (int i = 0; i < 2; ++i) {
        const QPointF c = markerPosition(i == 0 ? Marker::Cursor1 : Marker::Cursor2);
        p.setPen(pen);
        if (cursors_.mode == starosc::CursorMode::Time) {
            const double x = std::round(c.x()) + 0.5;
            p.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
            p.setPen(display_style::mark());
            p.drawText(QPointF(x + 3, plot.top() + 11), QString::number(i + 1));
        } else {
            const double y = std::round(c.y()) + 0.5;
            p.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
            p.setPen(display_style::mark());
            p.drawText(QPointF(plot.left() + 3, y - 3), QString::number(i + 1));
        }
    }
}

void ScopeView::drawMarkers(QPainter& p, const QRectF& plot) const
{
    QFont font = p.font();
    font.setPixelSize(10);
    font.setBold(true);
    p.setFont(font);
    p.setRenderHint(QPainter::Antialiasing, false);

    // Back to front, so the channels sit on top.
    for (Marker m : {Marker::Ref2, Marker::Ref1, Marker::Math, Marker::Channel2, Marker::Channel1}) {
        if (!laneShown(m))
            continue;
        const QPointF c = markerPosition(m);
        switch (m) {
        case Marker::Channel1: drawTag(p, 1, c.y(), true, display_style::channel(0), "1"); break;
        case Marker::Channel2: drawTag(p, 1, c.y(), true, display_style::channel(1), "2"); break;
        case Marker::Math: drawTag(p, 1, c.y(), true, display_style::math(), "M"); break;
        default: drawTag(p, 1, c.y(), true, display_style::reference(refIndex(m)), QString("R%1").arg(refIndex(m) + 1)); break;
        }
    }

    if (xy()) {
        if (!settings_.channel[0].enabled)
            return;
        const double x = markerPosition(Marker::Channel1).x();
        QPainterPath arrow;
        arrow.moveTo(x, plot.bottom() - 10);
        arrow.lineTo(x + 6, plot.bottom());
        arrow.lineTo(x - 6, plot.bottom());
        arrow.closeSubpath();
        p.setPen(Qt::NoPen);
        p.setBrush(display_style::channel(0));
        p.drawPath(arrow);
        return;
    }

    const QPointF level = markerPosition(Marker::TriggerLevel);
    drawTag(p, width() - 1, level.y(), false, display_style::channel(settings_.trigger.source), "T");
    drawTriggerPosition(p, plot);
}

void ScopeView::drawTriggerPosition(QPainter& p, const QRectF& plot) const
{
    const starosc::VisibleRange r = visibleRange();
    const double at = (settings_.trigger.pre_trigger - r.begin) / r.span;
    const double tx = markerPosition(Marker::TriggerPosition).x();
    const double top = plot.top() - kMarginTop;
    QPainterPath arrow;
    if (at < 0 || at > 1) {
        // The trigger is outside the zoomed part: an arrow at the edge points towards it.
        const double dir = at < 0 ? -1 : 1;
        const double edge = at < 0 ? plot.left() : plot.right();
        arrow.moveTo(edge + dir * 2, top + kMarginTop / 2);
        arrow.lineTo(edge - dir * 6, top);
        arrow.lineTo(edge - dir * 6, top + kMarginTop);
    } else {
        arrow.moveTo(tx - 6, top);
        arrow.lineTo(tx + 6, top);
        arrow.lineTo(tx + 6, top + 4);
        arrow.lineTo(tx, top + kMarginTop);
        arrow.lineTo(tx - 6, top + 4);
    }
    arrow.closeSubpath();
    p.setPen(Qt::NoPen);
    p.setBrush(display_style::mark());
    p.drawPath(arrow);
}
