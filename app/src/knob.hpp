// SPDX-License-Identifier: GPL-3.0-or-later
// A round control of the front panel: turned with the wheel, by dragging around it or with the arrow keys, with its
// caption and a box showing the value under it. A double click, Enter or F2 types a value into the box.
#pragma once

#include <functional>
#include <optional>
#include <vector>

#include <QStringList>
#include <QWidget>

class QLineEdit;

class Knob : public QWidget {
    Q_OBJECT

public:
    enum class Size { Small, Large };

    // `valueChars` sets the width of the value box. `unit` is the one understood in typed values, as in "500 mV".
    Knob(const QString& caption, Size size, int valueChars, const QString& unit = {}, QWidget* parent = nullptr);

    // A value anywhere in [min, max]. A wheel notch moves it by `step`, with Ctrl by a tenth of that; it keeps to
    // multiples of `resolution`. Shown through the function given to setFormat.
    void setRange(double min, double max, double step, double resolution);
    void setFormat(std::function<QString(double)> format);

    // One of `values`, in the order of turning clockwise, each shown as its label. A typed value goes to the closest.
    void setSteps(const std::vector<double>& values, const QStringList& labels);

    void setUnit(const QString& unit) { unit_ = unit; }

    // Never emits edited.
    void setValue(double value);
    double value() const;
    QString text() const;

    QSize sizeHint() const override;

signals:
    // Only when the user changed the value.
    void edited(double value);

protected:
    void paintEvent(QPaintEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    bool stepped() const { return !values_.empty(); }
    int dialSize() const;
    QRect dialRect() const;
    QRect captionRect() const;
    QRect valueRect() const;
    QFont valueFont() const;
    double fraction() const;  // of the way around, 0 at the left end
    double valueAt(double fraction) const;
    std::optional<double> pointerAngle(QPointF pos) const;

    void turn(double steps);
    void userSet(double value);
    int closestIndex(double value) const;
    double kept(double value) const;
    void startEditing();
    void finishEditing();

    QString caption_;
    Size size_;
    int valueChars_;
    QString unit_;

    std::vector<double> values_;
    QStringList labels_;
    int index_ = 0;

    double min_ = 0;
    double max_ = 1;
    double step_ = 0.1;
    double resolution_ = 0.01;
    double value_ = 0;
    std::function<QString(double)> format_;

    double wheelRemainder_ = 0;
    bool dragging_ = false;
    double dragFraction_ = 0;  // where the drag has turned to; small moves add up though a step or the value rounds
    std::optional<double> lastAngle_;
    QLineEdit* editor_ = nullptr;
};
