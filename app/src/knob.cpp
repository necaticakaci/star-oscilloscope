// SPDX-License-Identifier: GPL-3.0-or-later
#include "knob.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

#include <QKeyEvent>
#include <QLineEdit>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QStyle>
#include <QStyleOptionFocusRect>
#include <QWheelEvent>
#include <qdrawutil.h>

#include "displaystyle.hpp"
#include "sispinbox.hpp"

namespace {

constexpr int kLargeDial = 56;
constexpr int kSmallDial = 38;
constexpr int kMaxTicks = 24;
constexpr int kDefaultTicks = 11;
constexpr double kStartAngle = 225;  // degrees, counter-clockwise from three o'clock: the left end
constexpr double kSweep = 270;
constexpr double kMinDragRadius = 5;  // pixels from the centre, where a small move would turn the knob a long way
constexpr int kPageSteps = 10;
constexpr int kValueMargin = 8;

QPointF onCircle(QPointF center, double radius, double degrees)
{
    const double a = degrees * std::numbers::pi / 180;
    return {center.x() + radius * std::cos(a), center.y() - radius * std::sin(a)};
}

QRectF around(QPointF center, double radius)
{
    return {center.x() - radius, center.y() - radius, 2 * radius, 2 * radius};
}

}  // namespace

Knob::Knob(const QString& caption, Size size, int valueChars, const QString& unit, QWidget* parent)
    : QWidget(parent), caption_(caption), size_(size), valueChars_(valueChars), unit_(unit)
{
    setFocusPolicy(Qt::StrongFocus);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setAccessibleName(caption);
    format_ = [](double v) { return QString::number(v, 'f', 2); };
}

void Knob::setRange(double min, double max, double step, double resolution)
{
    values_.clear();
    labels_.clear();
    min_ = min;
    max_ = std::max(min, max);
    step_ = step;
    resolution_ = resolution;
    value_ = kept(value_);
    update();
}

void Knob::setFormat(std::function<QString(double)> format)
{
    format_ = std::move(format);
    update();
}

void Knob::setSteps(const std::vector<double>& values, const QStringList& labels)
{
    if (values == values_ && labels == labels_)
        return;
    const double current = value();
    values_ = values;
    labels_ = labels;
    index_ = closestIndex(current);
    update();
}

int Knob::closestIndex(double value) const
{
    int best = 0;
    double bestDistance = INFINITY;
    for (int i = 0; i < static_cast<int>(values_.size()); ++i) {
        const double v = values_[static_cast<std::size_t>(i)];
        if (v == value)
            return i;
        // Scales are compared by ratio, so 300 mV is closer to 200 mV than to 500 mV. A zero (off) is never close.
        const double distance = v > 0 && value > 0 ? std::abs(std::log(v / value)) : std::abs(v - value) + 1e3;
        if (distance < bestDistance) {
            bestDistance = distance;
            best = i;
        }
    }
    return best;
}

double Knob::kept(double value) const
{
    const double rounded = resolution_ > 0 ? std::round(value / resolution_) * resolution_ : value;
    return std::clamp(rounded, min_, max_);
}

void Knob::setValue(double value)
{
    if (stepped())
        index_ = closestIndex(value);
    else
        value_ = kept(value);
    setAccessibleDescription(text());
    update();
}

double Knob::value() const { return stepped() ? values_[static_cast<std::size_t>(index_)] : value_; }

QString Knob::text() const { return stepped() ? labels_.value(index_) : format_(value_); }

double Knob::fraction() const
{
    if (stepped())
        return values_.size() > 1 ? static_cast<double>(index_) / static_cast<double>(values_.size() - 1) : 0;
    return max_ > min_ ? (value_ - min_) / (max_ - min_) : 0.5;
}

double Knob::valueAt(double fraction) const
{
    if (stepped()) {
        const auto last = static_cast<double>(values_.size() - 1);
        return values_[static_cast<std::size_t>(std::lround(fraction * last))];
    }
    return min_ + fraction * (max_ - min_);
}

// Degrees counter-clockwise from three o'clock, seen from the centre of the dial.
std::optional<double> Knob::pointerAngle(QPointF pos) const
{
    const QPointF from = pos - QRectF(dialRect()).center();
    if (std::hypot(from.x(), from.y()) < kMinDragRadius)
        return std::nullopt;
    return std::atan2(-from.y(), from.x()) * 180 / std::numbers::pi;
}

int Knob::dialSize() const { return size_ == Size::Large ? kLargeDial : kSmallDial; }

QSize Knob::sizeHint() const
{
    QFontMetrics value(valueFont());
    QFontMetrics caption(font());
    const int width = std::max({dialSize(), valueChars_ * value.horizontalAdvance(QLatin1Char('0')) + kValueMargin,
                                caption.horizontalAdvance(caption_)});
    return {width, dialSize() + caption.height() + value.height() + 5};
}

QRect Knob::dialRect() const { return {(width() - dialSize()) / 2, 0, dialSize(), dialSize()}; }

QRect Knob::captionRect() const { return {0, dialSize(), width(), QFontMetrics(font()).height()}; }

QRect Knob::valueRect() const
{
    const int height = QFontMetrics(valueFont()).height() + 4;
    return {0, this->height() - height, width(), height};
}

QFont Knob::valueFont() const
{
    QFont f = display_style::fixedFont();
    if (font().pointSizeF() > 0)
        f.setPointSizeF(font().pointSizeF());
    return f;
}

void Knob::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    const QPalette& pal = palette();
    const bool on = isEnabled();

    p.setRenderHint(QPainter::Antialiasing);
    const QRectF dial = dialRect();
    const QPointF center = dial.center();
    const double unit = dial.width() / 64;  // the drawing is made on a 64 px dial
    const QColor ink = pal.color(on ? QPalette::Active : QPalette::Disabled, QPalette::WindowText);

    if (size_ == Size::Large) {
        const int count = stepped() ? static_cast<int>(values_.size()) : 0;
        const int ticks = count >= 2 && count <= kMaxTicks ? count : kDefaultTicks;
        p.setPen(QPen(ink, 1.2));
        for (int i = 0; i < ticks; ++i) {
            const double angle = kStartAngle - kSweep * i / (ticks - 1);
            p.drawLine(onCircle(center, 26.5 * unit, angle), onCircle(center, 30.5 * unit, angle));
        }
    }

    QLinearGradient raised(around(center, 23 * unit).topLeft(), around(center, 23 * unit).bottomRight());
    raised.setColorAt(0, pal.color(QPalette::Light));
    raised.setColorAt(1, pal.color(QPalette::Dark).darker(120));
    p.setPen(QPen(pal.color(QPalette::Shadow), 1));
    p.setBrush(raised);
    p.drawEllipse(around(center, 23 * unit));

    QLinearGradient hollow(around(center, 17.5 * unit).topLeft(), around(center, 17.5 * unit).bottomRight());
    hollow.setColorAt(0, pal.color(QPalette::Mid));
    hollow.setColorAt(1, pal.color(QPalette::Light).darker(104));
    p.setPen(QPen(pal.color(QPalette::Dark), 0.6));
    p.setBrush(hollow);
    p.drawEllipse(around(center, 17.5 * unit));

    const double angle = kStartAngle - kSweep * fraction();
    p.setPen(QPen(ink, std::max(1.5, 3 * unit), Qt::SolidLine, Qt::FlatCap));
    p.drawLine(onCircle(center, 7 * unit, angle), onCircle(center, 19 * unit, angle));

    p.setRenderHint(QPainter::Antialiasing, false);
    p.setPen(ink);
    p.drawText(captionRect(), Qt::AlignCenter, caption_);

    const QRect box = valueRect();
    p.fillRect(box, pal.color(on ? QPalette::Active : QPalette::Disabled, QPalette::Base));
    qDrawShadePanel(&p, box, pal, true, 1);
    p.setFont(valueFont());
    p.setPen(pal.color(on ? QPalette::Active : QPalette::Disabled, QPalette::Text));
    p.drawText(box, Qt::AlignCenter, text());

    if (hasFocus() && !(editor_ && editor_->isVisible())) {
        QStyleOptionFocusRect option;
        option.initFrom(this);
        option.rect = dialRect();
        option.backgroundColor = pal.color(QPalette::Window);
        style()->drawPrimitive(QStyle::PE_FrameFocusRect, &option, &p, this);
    }
}

void Knob::turn(double steps)
{
    if (stepped()) {
        const int last = static_cast<int>(values_.size()) - 1;
        userSet(values_[static_cast<std::size_t>(std::clamp(index_ + static_cast<int>(steps), 0, last))]);
    } else {
        userSet(value_ + steps * step_);
    }
}

void Knob::userSet(double value)
{
    const double before = this->value();
    setValue(value);
    if (this->value() != before)
        emit edited(this->value());
}

void Knob::wheelEvent(QWheelEvent* event)
{
    event->accept();
    wheelRemainder_ += event->angleDelta().y() / 120.0;
    const int notches = static_cast<int>(wheelRemainder_);
    if (notches == 0)
        return;
    wheelRemainder_ -= notches;
    const bool fine = !stepped() && (event->modifiers() & Qt::ControlModifier);
    turn(fine ? notches / 10.0 : notches);
}

void Knob::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton || !dialRect().contains(event->position().toPoint()))
        return QWidget::mousePressEvent(event);
    dragging_ = true;
    dragFraction_ = fraction();
    lastAngle_ = pointerAngle(event->position());
}

void Knob::mouseMoveEvent(QMouseEvent* event)
{
    if (!dragging_)
        return QWidget::mouseMoveEvent(event);
    // The knob turns as far as the pointer goes around it, from wherever it stood: it never jumps to the pointer, and
    // past an end it waits there. Farther from the centre, the same turn takes a longer move, for a finer setting.
    const std::optional<double> angle = pointerAngle(event->position());
    if (angle && lastAngle_) {
        const double clockwise = std::remainder(*lastAngle_ - *angle, 360.0);
        dragFraction_ = std::clamp(dragFraction_ + clockwise / kSweep, 0.0, 1.0);
        userSet(valueAt(dragFraction_));
    }
    lastAngle_ = angle;
}

void Knob::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
        dragging_ = false;
    QWidget::mouseReleaseEvent(event);
}

void Knob::mouseDoubleClickEvent(QMouseEvent* event)
{
    dragging_ = false;
    if (event->button() == Qt::LeftButton)
        startEditing();
}

void Knob::keyPressEvent(QKeyEvent* event)
{
    switch (event->key()) {
    case Qt::Key_Up:
    case Qt::Key_Right: turn(1); break;
    case Qt::Key_Down:
    case Qt::Key_Left: turn(-1); break;
    case Qt::Key_PageUp: turn(kPageSteps); break;
    case Qt::Key_PageDown: turn(-kPageSteps); break;
    case Qt::Key_Home: userSet(stepped() ? values_.front() : min_); break;
    case Qt::Key_End: userSet(stepped() ? values_.back() : max_); break;
    case Qt::Key_Return:
    case Qt::Key_Enter:
    case Qt::Key_F2: startEditing(); break;
    default: QWidget::keyPressEvent(event);
    }
}

void Knob::startEditing()
{
    if (!editor_) {
        editor_ = new QLineEdit(this);
        editor_->setObjectName("knobEditor");
        editor_->setFrame(false);
        editor_->setAlignment(Qt::AlignCenter);
        editor_->setFont(valueFont());
        editor_->installEventFilter(this);
    }
    editor_->setGeometry(valueRect().adjusted(1, 1, -1, -1));
    editor_->setText(text());
    editor_->selectAll();
    editor_->show();
    editor_->setFocus();
}

void Knob::finishEditing()
{
    if (!editor_ || editor_->isHidden())
        return;
    QString typed = editor_->text().trimmed();
    const bool hadFocus = editor_->hasFocus();
    editor_->hide();  // first: losing the focus comes back here
    if (hadFocus)
        setFocus();
    if (typed == text())
        return;  // left as it was; the box may show the value rounded

    if (stepped()) {
        // A step may have a word for a name, such as Off.
        for (int i = 0; i < labels_.size(); ++i)
            if (labels_[i].compare(typed, Qt::CaseInsensitive) == 0)
                return userSet(values_[static_cast<std::size_t>(i)]);
    }
    if (typed.endsWith(QLatin1String("/div"), Qt::CaseInsensitive))
        typed.chop(4);
    if (const auto v = SiSpinBox::parse(typed, unit_))
        userSet(*v);
}

bool Knob::eventFilter(QObject* watched, QEvent* event)
{
    if (watched != editor_)
        return QWidget::eventFilter(watched, event);
    if (event->type() == QEvent::FocusOut) {
        finishEditing();  // the line edit reports this only when the text changed
    } else if (event->type() == QEvent::KeyPress) {
        const int key = static_cast<QKeyEvent*>(event)->key();
        if (key == Qt::Key_Escape) {
            editor_->hide();
            setFocus();
            return true;
        }
        if (key == Qt::Key_Return || key == Qt::Key_Enter) {
            finishEditing();
            return true;  // a line edit passes these on, and here they would open the box again
        }
    }
    return QWidget::eventFilter(watched, event);
}
