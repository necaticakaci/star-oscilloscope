// SPDX-License-Identifier: GPL-3.0-or-later
#include "spectrumview.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>

#include "displaystyle.hpp"
#include "starosc/scale.hpp"

namespace {

constexpr double kMarginLeft = 52;  // level labels
constexpr double kMarginRight = 14;
constexpr double kMarginTop = 18;   // reference level and averaging readouts
constexpr double kMarginBottom = 18;  // frequency labels
constexpr double kZoomPerNotch = 0.8;
constexpr double kMinBinsOnScreen = 8;



QString si(double value, const char* unit, int digits = 4, bool trim = false)
{
    return QString::fromStdString(starosc::format_si(value, unit, digits, trim));
}

QFont readoutFont()
{
    QFont f = display_style::fixedFont();
    f.setPointSizeF(std::max(7.0, f.pointSizeF() - 1));
    return f;
}

}  // namespace

SpectrumView::SpectrumView(QWidget* parent) : QWidget(parent)
{
    setAttribute(Qt::WA_OpaquePaintEvent);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMouseTracking(true);
}

QColor SpectrumView::traceColor(int source)
{
    return display_style::traceColor(source);
}

void SpectrumView::setFrame(SpectrumFramePtr frame)
{
    frame_ = std::move(frame);
    update();
}

void SpectrumView::setSettings(const starosc::SpectrumSettings& settings)
{
    settings_ = settings;
    update();
}

QRectF SpectrumView::plotRect() const
{
    return QRectF(kMarginLeft, kMarginTop, std::max(10.0, width() - kMarginLeft - kMarginRight),
                  std::max(10.0, height() - kMarginTop - kMarginBottom));
}

double SpectrumView::sampleRate() const
{
    return frame_ ? frame_->sample_rate_hz : static_cast<double>(settings_.sample_rate_hz);
}

double SpectrumView::binHz() const
{
    return frame_ ? frame_->bin_hz : sampleRate() / static_cast<double>(std::max<std::size_t>(settings_.points, 2));
}

std::pair<double, double> SpectrumView::visibleSpan() const
{
    const double nyquist = sampleRate() / 2;
    double lo = settings_.start_hz, hi = settings_.stop_hz;
    if (!(hi > lo)) {
        lo = 0;
        hi = nyquist;
    }
    hi = std::min(hi, nyquist);
    lo = std::clamp(lo, 0.0, hi);
    if (settings_.log_frequency)
        lo = std::max(lo, binHz());  // DC has no place on a logarithmic axis
    if (!(hi > lo))
        hi = lo * 10;
    return {lo, hi};
}

double SpectrumView::xOf(const QRectF& plot, double hz) const
{
    const auto [lo, hi] = visibleSpan();
    double t;
    if (settings_.log_frequency)
        t = hz > 0 ? std::log(hz / lo) / std::log(hi / lo) : -1;
    else
        t = (hz - lo) / (hi - lo);
    return plot.left() + std::clamp(t, -1.0, 2.0) * plot.width();
}

double SpectrumView::frequencyAt(const QRectF& plot, double x) const
{
    const auto [lo, hi] = visibleSpan();
    const double t = (x - plot.left()) / plot.width();
    return settings_.log_frequency ? lo * std::pow(hi / lo, t) : lo + t * (hi - lo);
}

double SpectrumView::topLevel() const { return starosc::level_from_dbv(settings_.top_dbv, settings_.unit); }

starosc::TraceUnit SpectrumView::traceUnit(int source) const
{
    return starosc::trace_unit(settings_.unit, source,
                               frame_ ? frame_->scope.math.operation : starosc::MathOperation::Subtract);
}

double SpectrumView::yOf(const QRectF& plot, double power, int source) const
{
    double y;
    if (starosc::unit_is_db(settings_.unit)) {
        const double level = starosc::level_in_unit(power, traceUnit(source).unit);
        y = plot.top() + (topLevel() - level) / settings_.db_per_div * plot.height() / starosc::kSpectrumDivisionsY;
    } else {
        y = plot.bottom() - std::sqrt(std::max(power, 0.0)) / topLevel() * plot.height();
    }
    return std::clamp(y, plot.top() - 1e4, plot.bottom() + 1e4);
}

QString SpectrumView::levelText(double level) const
{
    if (!starosc::unit_is_db(settings_.unit))
        return si(level, "V", 3, true);
    const bool whole = std::abs(settings_.db_per_div - std::round(settings_.db_per_div)) < 1e-9 &&
                       std::abs(topLevel() - std::round(topLevel())) < 1e-9;
    return QString::number(level, 'f', whole ? 0 : 1).replace('-', QChar(0x2212));
}

void SpectrumView::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.fillRect(rect(), display_style::background());
    const QRectF plot = plotRect();
    drawGraticule(p, plot);
    if (frame_) {
        p.save();
        p.setClipRect(plot);
        for (int src = starosc::kSpectrumSources - 1; src >= 0; --src)
            if (settings_.shown[src] && frame_->power[src].size() > 1)
                drawTrace(p, plot, src);
        drawMarkers(p, plot);
        p.restore();
    }
    drawReadouts(p, plot);
    drawHover(p, plot);
}

void SpectrumView::drawGraticule(QPainter& p, const QRectF& plot) const
{
    const auto [lo, hi] = visibleSpan();
    const double dy = plot.height() / starosc::kSpectrumDivisionsY;
    QPen dotted = display_style::hairline(display_style::grid(), p.device(), Qt::CustomDashLine);
    dotted.setDashPattern({1, 3});
    QPen decade = display_style::hairline(display_style::axis(), p.device(), Qt::CustomDashLine);
    decade.setDashPattern({1, 2});

    p.setPen(dotted);
    for (int i = 1; i < starosc::kSpectrumDivisionsY; ++i) {
        const double y = std::round(plot.top() + i * dy) + 0.5;
        p.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
    }

    // Frequency lines and their labels.
    struct Label {
        double x;
        QString text;
    };
    std::vector<Label> labels;
    if (settings_.log_frequency) {
        const int first = static_cast<int>(std::floor(std::log10(lo)));
        const int last = static_cast<int>(std::ceil(std::log10(hi)));
        const bool fewDecades = std::log10(hi / lo) < 2;
        for (int d = first; d <= last; ++d)
            for (int m = 1; m <= 9; ++m) {
                const double f = m * std::pow(10.0, d);
                if (f <= lo * 1.0001 || f >= hi * 0.9999)
                    continue;
                const double x = std::round(xOf(plot, f)) + 0.5;
                p.setPen(m == 1 ? decade : dotted);
                p.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
                if (m == 1 || (fewDecades && (m == 2 || m == 5)))
                    labels.push_back({x, si(f, "Hz", 3, true)});
            }
    } else {
        const double dx = plot.width() / starosc::kSpectrumDivisionsX;
        p.setPen(dotted);
        for (int i = 1; i < starosc::kSpectrumDivisionsX; ++i) {
            const double x = std::round(plot.left() + i * dx) + 0.5;
            p.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
        }
        for (int i = 0; i <= starosc::kSpectrumDivisionsX; i += 2)
            labels.push_back({plot.left() + i * dx, si(lo + i * (hi - lo) / starosc::kSpectrumDivisionsX, "Hz", 4, true)});
    }
    p.setPen(display_style::hairline(display_style::axis(), p.device()));
    p.drawRect(plot.adjusted(0.5, 0.5, -0.5, -0.5));

    p.setFont(readoutFont());
    p.setPen(display_style::text());
    const QFontMetricsF fm(p.font());
    for (const Label& l : labels) {
        const double w = fm.horizontalAdvance(l.text);
        const double x = std::clamp(l.x - w / 2, plot.left(), plot.right() - w);
        p.drawText(QPointF(x, plot.bottom() + fm.ascent() + 3), l.text);
    }
    for (int i = 0; i <= starosc::kSpectrumDivisionsY; ++i) {
        const double level = starosc::unit_is_db(settings_.unit)
                                 ? topLevel() - i * settings_.db_per_div
                                 : topLevel() * (starosc::kSpectrumDivisionsY - i) / starosc::kSpectrumDivisionsY;
        const QString text = levelText(level);
        const double y = plot.top() + i * dy;
        p.drawText(QRectF(0, y - fm.height() / 2, plot.left() - 5, fm.height()), Qt::AlignRight | Qt::AlignVCenter, text);
    }
}

void SpectrumView::drawTrace(QPainter& p, const QRectF& plot, int source) const
{
    const std::vector<float>& power = frame_->power[source];
    const double bin = binHz();
    const auto [lo, hi] = visibleSpan();
    const auto last = static_cast<double>(power.size() - 1);
    const auto k0 = static_cast<std::size_t>(std::clamp(std::floor(lo / bin) - 1, settings_.log_frequency ? 1.0 : 0.0, last));
    const auto k1 = static_cast<std::size_t>(std::clamp(std::ceil(hi / bin) + 1, 0.0, last));
    // One point per bin, or per pixel column with the highest bin in it: peaks stay visible however many
    // bins share a pixel.
    QPolygonF line;
    line.reserve(static_cast<qsizetype>(std::min<std::size_t>(k1 - k0 + 1, static_cast<std::size_t>(plot.width()) * 2 + 4)));
    long column = 0;
    for (std::size_t k = k0; k <= k1; ++k) {
        const double x = xOf(plot, static_cast<double>(k) * bin);
        const double y = yOf(plot, power[k], source);
        const auto c = static_cast<long>(std::floor(x));
        if (!line.isEmpty() && c == column) {
            line.back().setY(std::min(line.back().y(), y));
            continue;
        }
        line << QPointF(x, y);
        column = c;
    }
    p.setPen(display_style::hairline(traceColor(source), p.device()));
    p.drawPolyline(line);
}

void SpectrumView::drawMarkers(QPainter& p, const QRectF& plot) const
{
    const int src = settings_.marker_source;
    if (src < 0 || src >= starosc::kSpectrumSources || !settings_.shown[src])
        return;
    const std::vector<float>& power = frame_->power[src];
    const starosc::SpectrumAnalysis& a = frame_->analysis[src];
    if (!a.valid || power.size() < 2)
        return;
    const double bin = binHz();
    p.setFont(readoutFont());
    // A marker too close to one already drawn is left out: at a wide span the harmonics crowd together.
    std::vector<double> drawn;
    const auto mark = [&](double hz, int order) {
        const auto centre = static_cast<long>(std::lround(hz / bin));
        long peak = std::clamp(centre, 0L, static_cast<long>(power.size()) - 1);
        for (long k = std::max(0L, centre - 3); k <= std::min(static_cast<long>(power.size()) - 1, centre + 3); ++k)
            if (power[static_cast<std::size_t>(k)] > power[static_cast<std::size_t>(peak)])
                peak = k;
        const double x = xOf(plot, static_cast<double>(peak) * bin);
        if (x < plot.left() || x > plot.right() ||
            std::any_of(drawn.begin(), drawn.end(), [x](double d) { return std::abs(d - x) < 12; }))
            return;
        drawn.push_back(x);
        const double y = std::clamp(yOf(plot, power[static_cast<std::size_t>(peak)], src), plot.top() + 22, plot.bottom());
        QPainterPath tip;
        tip.moveTo(x - 4, y - 10);
        tip.lineTo(x + 4, y - 10);
        tip.lineTo(x, y - 3);
        tip.closeSubpath();
        p.setPen(Qt::NoPen);
        p.setBrush(traceColor(src));
        p.drawPath(tip);
        p.setPen(display_style::mark());
        p.drawText(QRectF(x - 10, y - 23, 20, 12), Qt::AlignCenter, QString::number(order));
    };
    mark(a.fundamental_hz, 1);
    for (const starosc::Harmonic& h : a.harmonics)
        if (h.power > 0)
            mark(h.frequency_hz, h.order);
}

void SpectrumView::drawReadouts(QPainter& p, const QRectF& plot) const
{
    p.setFont(readoutFont());
    const QFontMetricsF fm(p.font());
    const double baseline = plot.top() - 4 - fm.descent();
    const QString unit = QString::fromUtf8(starosc::unit_name(settings_.unit).data());
    QString scale;
    if (starosc::unit_is_db(settings_.unit))
        scale = tr("Ref %1 %2   %3 dB/div").arg(levelText(topLevel()), unit, QString::number(settings_.db_per_div, 'g', 3));
    else
        scale = tr("Ref %1   %2/div").arg(si(topLevel(), "V", 4, true), si(topLevel() / starosc::kSpectrumDivisionsY, "V", 3, true));
    p.setPen(display_style::text());
    p.drawText(QPointF(plot.left(), baseline), scale);

    QString right;
    if (frame_) {
        if (settings_.averaging == starosc::SpectrumAveraging::Average)
            right = tr("Avg %1/%2").arg(frame_->averaged).arg(settings_.averages);
        else if (settings_.averaging == starosc::SpectrumAveraging::PeakHold)
            right = tr("Peak hold %1").arg(frame_->averaged);
    }
    double x = plot.right() - fm.horizontalAdvance(right);
    p.drawText(QPointF(x, baseline), right);
    if (frame_)
        for (int ch = starosc::kChannels - 1; ch >= 0; --ch)
            if (frame_->clipped[ch]) {
                const QString clip = tr("CH%1 CLIP").arg(ch + 1);
                x -= fm.horizontalAdvance(clip) + 12;
                p.setPen(display_style::channel(ch));
                p.drawText(QPointF(x, baseline), clip);
            }
}

void SpectrumView::drawHover(QPainter& p, const QRectF& plot) const
{
    if (!hover_ || !plot.contains(*hover_) || dragFrom_)
        return;
    const double x = std::round(hover_->x()) + 0.5;
    QPen dotted = display_style::hairline(display_style::mark(), p.device(), Qt::CustomDashLine);
    dotted.setDashPattern({2, 3});
    p.setPen(dotted);
    p.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));

    const double f = frequencyAt(plot, hover_->x());
    QStringList lines{si(f, "Hz", 5)};
    std::vector<QColor> colors{display_style::mark()};
    if (frame_) {
        const double bin = binHz();
        const double fa = frequencyAt(plot, hover_->x() - 0.5), fb = frequencyAt(plot, hover_->x() + 0.5);
        for (int src = 0; src < starosc::kSpectrumSources; ++src) {
            const std::vector<float>& power = frame_->power[src];
            if (!settings_.shown[src] || power.size() < 2)
                continue;
            const auto last = static_cast<long>(power.size()) - 1;
            const long a = std::clamp(static_cast<long>(std::lround(fa / bin)), 0L, last);
            const long b = std::clamp(static_cast<long>(std::lround(fb / bin)), a, last);
            float best = power[static_cast<std::size_t>(a)];
            for (long k = a; k <= b; ++k)
                best = std::max(best, power[static_cast<std::size_t>(k)]);
            const starosc::TraceUnit unit = traceUnit(src);
            const double level = starosc::level_in_unit(best, unit.unit);
            const QString name = src < starosc::kChannels ? tr("CH%1").arg(src + 1) : tr("Math");
            lines << QStringLiteral("%1 %2").arg(name, starosc::unit_is_db(unit.unit)
                                                         ? QString::number(level, 'f', 1).replace('-', QChar(0x2212)) + " " +
                                                               QString::fromStdString(unit.name)
                                                         : si(level, unit.name.c_str(), 4));
            colors.push_back(traceColor(src));
        }
    }
    p.setFont(readoutFont());
    const QFontMetricsF fm(p.font());
    double w = 0;
    for (const QString& l : lines)
        w = std::max(w, fm.horizontalAdvance(l));
    const QRectF box(plot.right() - w - 14, plot.top() + 4, w + 10, fm.height() * lines.size() + 6);
    p.setPen(display_style::hairline(display_style::axis(), p.device()));
    p.setBrush(display_style::background());
    p.drawRect(box.adjusted(0.5, 0.5, -0.5, -0.5));
    for (int i = 0; i < lines.size(); ++i) {
        p.setPen(colors[static_cast<std::size_t>(i)]);
        p.drawText(QPointF(box.left() + 5, box.top() + 3 + fm.ascent() + i * fm.height()), lines[i]);
    }
}

void SpectrumView::setSpan(double lo, double hi)
{
    const double nyquist = sampleRate() / 2;
    const double floor = settings_.log_frequency ? binHz() : 0.0;
    const double width = hi - lo;
    if (lo < floor) {
        hi += floor - lo;
        lo = floor;
    }
    if (hi > nyquist) {
        lo = std::max(floor, lo - (hi - nyquist));
        hi = nyquist;
    }
    if (settings_.log_frequency ? hi / lo < 1.5 : width < kMinBinsOnScreen * binHz())
        return;
    if (lo <= floor * 1.0001 && hi >= nyquist * 0.9999)
        emit spanChanged(0, 0);
    else
        emit spanChanged(lo, hi);
}

void SpectrumView::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton || !plotRect().contains(event->position()))
        return;
    dragFrom_ = event->position();
    dragSpan_ = visibleSpan();
    dragTop_ = settings_.top_dbv;
    setCursor(Qt::ClosedHandCursor);
    update();
}

void SpectrumView::mouseMoveEvent(QMouseEvent* event)
{
    hover_ = event->position();
    if (dragFrom_) {
        const QRectF plot = plotRect();
        const QPointF d = event->position() - *dragFrom_;
        const auto [lo, hi] = dragSpan_;
        const double t = -d.x() / plot.width();
        if (settings_.log_frequency) {
            const double factor = std::pow(hi / lo, t);
            setSpan(lo * factor, hi * factor);
        } else {
            setSpan(lo + t * (hi - lo), hi + t * (hi - lo));
        }
        if (starosc::unit_is_db(settings_.unit)) {
            const double top = dragTop_ + d.y() / (plot.height() / starosc::kSpectrumDivisionsY) * settings_.db_per_div;
            const double rounded = std::round(top * 10) / 10;
            if (rounded != settings_.top_dbv)
                emit referenceLevelChanged(rounded);
        }
    }
    update();
}

void SpectrumView::mouseReleaseEvent(QMouseEvent*)
{
    dragFrom_.reset();
    unsetCursor();
    update();
}

void SpectrumView::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (plotRect().contains(event->position()))
        emit spanChanged(0, 0);
}

void SpectrumView::wheelEvent(QWheelEvent* event)
{
    wheelRemainder_ += event->angleDelta().y() / 120.0;
    const int notches = static_cast<int>(wheelRemainder_);
    if (notches == 0)
        return;
    wheelRemainder_ -= notches;
    const QRectF plot = plotRect();
    const QPointF pos = event->position();
    if (pos.x() < plot.left()) {
        // Over the level labels: the reference level, a division a notch (6 dB for volts).
        const double step = starosc::unit_is_db(settings_.unit) ? settings_.db_per_div : 20 * std::log10(2.0);
        emit referenceLevelChanged(settings_.top_dbv - notches * step);
    } else if (plot.contains(pos)) {
        const auto [lo, hi] = visibleSpan();
        const double factor = std::pow(kZoomPerNotch, notches);
        const double f = frequencyAt(plot, pos.x());
        if (settings_.log_frequency)
            setSpan(f * std::pow(lo / f, factor), f * std::pow(hi / f, factor));
        else
            setSpan(f - (f - lo) * factor, f + (hi - f) * factor);
    }
    event->accept();
}

void SpectrumView::leaveEvent(QEvent*)
{
    hover_.reset();
    update();
}
