// SPDX-License-Identifier: GPL-3.0-or-later
#include "overviewstrip.hpp"

#include <algorithm>
#include <cmath>
#include <span>
#include <vector>

#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>

#include "displaystyle.hpp"

namespace {

constexpr double kMinSelection = 4;  // pixels; less is a click

void envelope(std::span<const float> v, std::vector<float>& lo, std::vector<float>& hi)
{
    const std::size_t n = v.size();
    const std::size_t bins = std::min(starosc::Overview::kBins, n);
    lo.resize(bins);
    hi.resize(bins);
    for (std::size_t b = 0; b < bins; ++b) {
        const auto [mn, mx] = std::minmax_element(v.begin() + static_cast<std::ptrdiff_t>(b * n / bins),
                                                  v.begin() + static_cast<std::ptrdiff_t>((b + 1) * n / bins));
        lo[b] = *mn;
        hi[b] = *mx;
    }
}

}  // namespace

OverviewStrip::OverviewStrip(QWidget* parent) : QWidget(parent)
{
    setAttribute(Qt::WA_OpaquePaintEvent);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setStatusTip(tr("Whole capture; the frame marks the record on the screen. Drag inside it to zoom in. "
                    "When zoomed: the record; drag the frame to move, wheel to zoom, double-click to leave the zoom."));
}

void OverviewStrip::setFrame(FramePtr frame)
{
    frame_ = std::move(frame);
    update();
}

void OverviewStrip::setSettings(const starosc::ScopeSettings& settings)
{
    settings_ = settings;
    update();
}

bool OverviewStrip::usable() const { return frame_ && settings_.display == starosc::DisplayMode::YT; }

bool OverviewStrip::zoomed() const { return usable() && visibleRange().span < 1; }

starosc::VisibleRange OverviewStrip::visibleRange() const
{
    return frame_ ? starosc::visible_range(frame_->settings.timebase, settings_.zoom) : starosc::VisibleRange{};
}

QRectF OverviewStrip::area() const { return QRectF(rect()).adjusted(1, 2, -1, -2); }

double OverviewStrip::recordShare() const
{
    return frame_ ? std::clamp(frame_->overview.window_end - frame_->overview.window_begin, 0.0, 1.0) : 1.0;
}

double OverviewStrip::captureStart() const
{
    return frame_ ? (1 - recordShare()) / 2 - frame_->overview.window_begin : 0.0;
}

QRectF OverviewStrip::frameRect() const
{
    const QRectF a = area();
    double begin = (1 - recordShare()) / 2;
    double end = begin + recordShare();
    if (zoomed()) {
        const starosc::VisibleRange r = visibleRange();
        begin = r.begin;
        end = r.begin + r.span;
    }
    const double x0 = a.left() + begin * a.width();
    const double x1 = a.left() + end * a.width();
    return QRectF(std::round(x0) + 0.5, 0.5, std::max(2.0, std::round(x1 - x0)), height() - 1.0);
}

double OverviewStrip::recordFraction(double x) const
{
    const QRectF a = area();
    const double at = (x - a.left()) / a.width();
    if (zoomed() || !frame_)
        return std::clamp(at, 0.0, 1.0);
    const double share = recordShare();
    return share > 0 ? std::clamp((at - (1 - share) / 2) / share, 0.0, 1.0) : 0.0;
}

void OverviewStrip::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton || !usable())
        return QWidget::mousePressEvent(event);
    const double x = event->position().x();
    if (zoomed()) {
        const starosc::VisibleRange r = visibleRange();
        const double at = recordFraction(x);
        const double center = r.begin + r.span / 2;
        grabOffset_ = std::abs(at - center) <= r.span / 2 ? at - center : 0;
        gesture_ = Gesture::Move;
        emit zoomCenterChanged(at - grabOffset_);
    } else {
        gesture_ = Gesture::Select;
        gestureStart_ = gestureNow_ = x;
    }
}

void OverviewStrip::mouseMoveEvent(QMouseEvent* event)
{
    const double x = event->position().x();
    if (gesture_ == Gesture::Move) {
        emit zoomCenterChanged(recordFraction(x) - grabOffset_);
    } else if (gesture_ == Gesture::Select) {
        gestureNow_ = x;
        update();
    }
}

void OverviewStrip::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton)
        return;
    if (gesture_ == Gesture::Select && std::abs(gestureNow_ - gestureStart_) >= kMinSelection)
        emit zoomRangeSelected(recordFraction(gestureStart_), recordFraction(gestureNow_));
    gesture_ = Gesture::None;
    update();
}

void OverviewStrip::mouseDoubleClickEvent(QMouseEvent*)
{
    if (settings_.zoom.on())
        emit zoomOffRequested();
}

void OverviewStrip::wheelEvent(QWheelEvent* event)
{
    if (!usable())
        return;
    wheelRemainder_ += event->angleDelta().y() / 120.0;
    const int notches = static_cast<int>(wheelRemainder_);
    if (notches == 0)
        return;
    wheelRemainder_ -= notches;
    emit zoomStepped(-notches);
    event->accept();
}

void OverviewStrip::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.fillRect(rect(), display_style::background());
    if (!frame_)
        return;

    const QRectF a = area();
    const auto& o = frame_->overview;
    const bool zoom = zoomed();
    std::vector<float> recordLo, recordHi;
    for (int ch = starosc::kChannels - 1; ch >= 0; --ch) {
        const auto& cs = settings_.channel[ch];
        if (!cs.enabled)
            continue;
        if (zoom)
            envelope(frame_->volts[ch], recordLo, recordHi);
        const std::vector<float>& lo = zoom ? recordLo : o.min[ch];
        const std::vector<float>& hi = zoom ? recordHi : o.max[ch];
        if (lo.empty())
            continue;
        // Same vertical mapping as the display, squeezed into the strip.
        const auto toY = [&](double v) {
            const double div = (v / cs.volts_per_div + cs.position_div) / starosc::kDivisionsY;
            return std::clamp(a.center().y() - div * a.height(), a.top(), a.bottom());
        };
        QColor color = display_style::channel(ch);
        color.setAlpha(170);
        p.setPen(display_style::hairline(color, p.device()));
        const double binWidth = a.width() / static_cast<double>(lo.size());
        const double start = zoom ? 0.0 : captureStart() * a.width();  // the capture as it lay around the record
        for (std::size_t b = 0; b < lo.size(); ++b) {
            const double x = a.left() + start + (static_cast<double>(b) + 0.5) * binWidth;
            if (x < a.left() || x > a.right())
                continue;
            p.drawLine(QPointF(x, toY(hi[b])), QPointF(x, toY(lo[b])));
        }
    }

    const QRectF frame = frameRect();
    const double x0 = frame.left();
    const double x1 = frame.right();
    p.setPen(display_style::hairline(display_style::mark(), p.device()));
    p.setBrush(Qt::NoBrush);
    p.drawRect(frame);

    if (gesture_ == Gesture::Select) {
        const double left = std::clamp(std::min(gestureStart_, gestureNow_), x0, x1);
        const double right = std::clamp(std::max(gestureStart_, gestureNow_), x0, x1);
        QColor fill = display_style::mark();
        fill.setAlpha(70);
        p.fillRect(QRectF(left, 0, right - left, height()), fill);
    }
}
