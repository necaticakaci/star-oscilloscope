// SPDX-License-Identifier: GPL-3.0-or-later
// Chooses the colour of each trace.
#pragma once

#include <array>

#include <QColor>
#include <QDialog>

#include "displaystyle.hpp"

class QToolButton;

class TraceColorsDialog : public QDialog {
    Q_OBJECT

public:
    explicit TraceColorsDialog(QWidget* parent = nullptr);

    QColor color(int trace) const { return colors_[static_cast<std::size_t>(trace)]; }
    void setColor(int trace, const QColor& color);
    // Writes the colours to display_style.
    void apply() const;

private:
    void choose(int trace);

    std::array<QColor, display_style::kTraces> colors_;
    std::array<QToolButton*, display_style::kTraces> buttons_{};
};
