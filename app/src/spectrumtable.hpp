// SPDX-License-Identifier: GPL-3.0-or-later
// Measurements of the Spectrum tab: fundamental, THD, SNR, SINAD, ENOB, SFDR and the harmonics, one column a trace.
// They change a few times a second, so they can be read.
#pragma once

#include <QTableWidget>

#include "refreshlimiter.hpp"
#include "types.hpp"

class SpectrumTable : public QTableWidget {
    Q_OBJECT

public:
    explicit SpectrumTable(QWidget* parent = nullptr);

    void setFrame(SpectrumFramePtr frame);
    // After a trace colour changed.
    void refreshColors();

private:
    void setCell(int row, int column, const QString& text);
    void showFrame();

    SpectrumFramePtr frame_;
    RefreshLimiter refresh_{[this] { showFrame(); }};
};
