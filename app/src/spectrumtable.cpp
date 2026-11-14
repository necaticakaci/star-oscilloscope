// SPDX-License-Identifier: GPL-3.0-or-later
#include "spectrumtable.hpp"

#include "displaystyle.hpp"

#include <cmath>

#include <QHeaderView>

#include "starosc/scale.hpp"

namespace {

enum Row { Fundamental, Level, Thd, Snr, Sinad, Enob, Sfdr, Noise, FirstHarmonic };
constexpr int kRows = FirstHarmonic + starosc::kMaxHarmonics - 1;

QString dash() { return QStringLiteral("-----"); }

QString decibels(double value, const char* unit = "dB")
{
    if (!std::isfinite(value))
        return dash();
    return QString::number(value, 'f', 1).replace('-', QChar(0x2212)) + " " + unit;
}

QString level(double power, const starosc::TraceUnit& unit)
{
    const double v = starosc::level_in_unit(power, unit.unit);
    if (!starosc::unit_is_db(unit.unit))
        return QString::fromStdString(starosc::format_si(v, unit.name));
    return decibels(v, unit.name.c_str());
}

}  // namespace

SpectrumTable::SpectrumTable(QWidget* parent) : QTableWidget(kRows, 1 + starosc::kSpectrumSources, parent)
{
    setHorizontalHeaderLabels({tr("Parameter"), tr("CH1"), tr("CH2"), tr("Math")});
    verticalHeader()->hide();
    QHeaderView* header = horizontalHeader();
    header->setHighlightSections(false);
    header->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    const QFontMetrics fixed(display_style::fixedFont());
    for (int c = 1; c <= starosc::kSpectrumSources; ++c) {
        header->setSectionResizeMode(c, QHeaderView::Fixed);
        header->resizeSection(c, fixed.horizontalAdvance(QStringLiteral("−100.0 dBc @ 999.99 kHz")) + 16);
    }
    header->setStretchLastSection(false);
    setEditTriggers(QAbstractItemView::NoEditTriggers);
    setSelectionBehavior(QAbstractItemView::SelectRows);
    setAlternatingRowColors(true);
    setWordWrap(false);
    setShowGrid(false);
    verticalHeader()->setDefaultSectionSize(fontMetrics().height() + 4);
    refreshColors();

    const QStringList names{tr("Fundamental"), tr("Level"), tr("THD"), tr("SNR"), tr("SINAD"), tr("ENOB"), tr("SFDR"),
                            tr("Noise")};
    for (int r = 0; r < kRows; ++r) {
        setCell(r, 0, r < FirstHarmonic ? names[r] : tr("Harmonic %1").arg(r - FirstHarmonic + 2));
        for (int c = 1; c <= starosc::kSpectrumSources; ++c)
            setCell(r, c, QString());
    }
    item(Thd, 0)->setStatusTip(tr("Total harmonic distortion up to the harmonic set under Analysis"));
    item(Snr, 0)->setStatusTip(tr("Fundamental to noise; DC and the harmonics left out"));
    item(Sinad, 0)->setStatusTip(tr("Fundamental to noise and harmonics"));
    item(Enob, 0)->setStatusTip(tr("Effective bits from SINAD: (SINAD − 1.76 dB) / 6.02 dB; meaningful near full scale"));
    item(Sfdr, 0)->setStatusTip(tr("Fundamental to the largest other peak"));
    item(Noise, 0)->setStatusTip(tr("Noise over the whole span, as an RMS level"));
}

void SpectrumTable::refreshColors()
{
    for (int c = 1; c <= starosc::kSpectrumSources; ++c)
        horizontalHeaderItem(c)->setIcon(display_style::swatch(c - 1));
}

void SpectrumTable::setCell(int row, int column, const QString& text)
{
    QTableWidgetItem* cell = item(row, column);
    if (!cell) {
        cell = new QTableWidgetItem;
        if (column > 0) {
            cell->setFont(display_style::fixedFont());
            cell->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        }
        setItem(row, column, cell);
    }
    cell->setText(text);
}

void SpectrumTable::setFrame(SpectrumFramePtr frame)
{
    frame_ = std::move(frame);
    if (frame_)
        refresh_.request();
}

void SpectrumTable::showFrame()
{
    const starosc::SpectrumFrame& frame = *frame_;
    for (int src = 0; src < starosc::kSpectrumSources; ++src) {
        const starosc::TraceUnit unit = starosc::trace_unit(frame.settings.unit, src, frame.scope.math.operation);
        const int column = src + 1;
        const bool shown = !frame.power[src].empty();
        setColumnHidden(column, !shown);
        if (!shown)
            continue;
        const starosc::SpectrumAnalysis& a = frame.analysis[src];
        if (!a.valid) {
            for (int r = 0; r < kRows; ++r)
                setCell(r, column, dash());
            continue;
        }
        setCell(Fundamental, column, QString::fromStdString(starosc::format_si(a.fundamental_hz, "Hz", 6)));
        setCell(Level, column, level(a.fundamental_power, unit));
        const QString percent = a.thd < 0.0995 ? QString::number(a.thd * 100, 'f', 3) : QString::number(a.thd * 100, 'f', 1);
        setCell(Thd, column, a.thd > 0 ? QStringLiteral("%1 (%2 %)").arg(decibels(20 * std::log10(a.thd)), percent) : dash());
        setCell(Snr, column, decibels(a.snr_db));
        setCell(Sinad, column, decibels(a.sinad_db));
        setCell(Enob, column, std::isfinite(a.enob_bits) ? QString::number(a.enob_bits, 'f', 2) + tr(" bits") : dash());
        setCell(Sfdr, column, decibels(a.sfdr_db, "dBc"));
        setCell(Noise, column, level(a.noise_power, unit));
        for (int r = FirstHarmonic; r < kRows; ++r) {
            const auto i = static_cast<std::size_t>(r - FirstHarmonic);
            if (i >= a.harmonics.size()) {
                setCell(r, column, QString());
                continue;
            }
            const starosc::Harmonic& h = a.harmonics[i];
            const QString at = QString::fromStdString(starosc::format_si(h.frequency_hz, "Hz", 5));
            setCell(r, column,
                    h.power > 0 ? QStringLiteral("%1 @ %2").arg(decibels(10 * std::log10(h.power / a.fundamental_power), "dBc"), at)
                                : tr("on another tone"));
        }
    }
    for (int r = FirstHarmonic; r < kRows; ++r)
        setRowHidden(r, r - FirstHarmonic + 2 > frame.settings.harmonics);
}
