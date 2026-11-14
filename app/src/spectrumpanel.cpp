// SPDX-License-Identifier: GPL-3.0-or-later
#include "spectrumpanel.hpp"

#include <cmath>
#include <utility>

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLocale>
#include <QPixmap>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <QCoreApplication>

#include "sispinbox.hpp"
#include "spectrumview.hpp"
#include "starosc/math.hpp"
#include "starosc/scale.hpp"

namespace {

QString si(double value, const char* unit, int digits = 4, bool trim = false)
{
    return QString::fromStdString(starosc::format_si(value, unit, digits, trim));
}

// Index of the item whose data is closest to `value`.
int closestIndex(const QComboBox* combo, double value)
{
    int best = 0;
    double bestDistance = INFINITY;
    for (int i = 0; i < combo->count(); ++i) {
        const double distance = std::abs(std::log(combo->itemData(i).toDouble() / value));
        if (distance < bestDistance) {
            bestDistance = distance;
            best = i;
        }
    }
    return best;
}

QIcon swatch(const QColor& color)
{
    QPixmap pixmap(10, 10);
    pixmap.fill(color);
    return QIcon(pixmap);
}

const std::vector<std::uint32_t> kDefaultRates{1'000'000, 4'000'000, 8'000'000, 16'000'000, 48'000'000};
constexpr double kDbPerDiv[] = {1, 2, 5, 10, 20};

}  // namespace

QString windowName(starosc::Window window)
{
    using W = starosc::Window;
    // Most windows carry their inventors' names, which stay as they are.
    switch (window) {
    case W::Rectangular: return QCoreApplication::translate("Window", "Rectangular");
    case W::Triangular: return QCoreApplication::translate("Window", "Triangular (Bartlett)");
    case W::Welch: return QCoreApplication::translate("Window", "Welch");
    case W::Hann: return QCoreApplication::translate("Window", "Hann");
    case W::Hamming: return QCoreApplication::translate("Window", "Hamming");
    case W::BartlettHann: return QCoreApplication::translate("Window", "Bartlett-Hann");
    case W::Cosine: return QCoreApplication::translate("Window", "Cosine");
    case W::Tukey: return QCoreApplication::translate("Window", "Tukey (0.5)");
    case W::Blackman: return QCoreApplication::translate("Window", "Blackman");
    case W::BlackmanHarris: return QCoreApplication::translate("Window", "Blackman-Harris");
    case W::BlackmanNuttall: return QCoreApplication::translate("Window", "Blackman-Nuttall");
    case W::Nuttall: return QCoreApplication::translate("Window", "Nuttall");
    case W::FlatTop: return QCoreApplication::translate("Window", "Flat top");
    case W::Kaiser: return QCoreApplication::translate("Window", "Kaiser (beta 9)");
    case W::DolphChebyshev: return QCoreApplication::translate("Window", "Dolph-Chebyshev (100 dB)");
    case W::Gaussian: return QCoreApplication::translate("Window", "Gaussian (0.4)");
    case W::Parzen: return QCoreApplication::translate("Window", "Parzen");
    case W::Bohman: return QCoreApplication::translate("Window", "Bohman");
    }
    return {};
}

SpectrumPanel::SpectrumPanel(QWidget* parent) : QWidget(parent), rates_(kDefaultRates)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(6);
    layout->addWidget(createTraces());
    layout->addWidget(createFrequency());
    layout->addWidget(createLevel());
    layout->addWidget(createAveraging());
    layout->addWidget(createAnalysis());
    layout->addStretch(1);
    setSampleRates(rates_);
    showSettings();
}

QWidget* SpectrumPanel::createTraces()
{
    auto* box = new QGroupBox(tr("Traces"), this);
    auto* grid = new QGridLayout(box);
    grid->setColumnStretch(1, 1);
    for (int src = 0; src < starosc::kSpectrumSources; ++src) {
        TraceWidgets& w = traces_[static_cast<std::size_t>(src)];
        const bool channel = src < starosc::kChannels;
        w.shown = new QCheckBox(channel ? tr("CH%1").arg(src + 1) : tr("Math"), box);
        w.shown->setIcon(swatch(SpectrumView::traceColor(src)));
        w.range = new QLabel(box);
        w.range->setEnabled(false);
        grid->addWidget(w.shown, 2 * src, 0);
        if (channel) {
            w.scale = new QComboBox(box);
            w.scale->setStatusTip(tr("Input scale, the same as on the Oscilloscope tab: it sets the input range"));
            w.coupling = new QComboBox(box);
            w.coupling->addItems({tr("DC"), tr("AC")});
            grid->addWidget(w.scale, 2 * src, 1);
            grid->addWidget(w.coupling, 2 * src, 2);
            connect(w.scale, &QComboBox::currentIndexChanged, this, &SpectrumPanel::readScope);
            connect(w.coupling, &QComboBox::currentIndexChanged, this, &SpectrumPanel::readScope);
        } else {
            w.shown->setStatusTip(tr("Spectrum of the math function set on the Oscilloscope tab"));
        }
        grid->addWidget(w.range, 2 * src + 1, 0, 1, 3);
        connect(w.shown, &QCheckBox::toggled, this, &SpectrumPanel::readWidgets);
    }
    return box;
}

QWidget* SpectrumPanel::createFrequency()
{
    auto* box = new QGroupBox(tr("Frequency"), this);
    rate_ = new QComboBox(box);
    rate_->setStatusTip(tr("Sample rate; the span reaches half of it"));
    points_ = new QComboBox(box);
    for (std::size_t n = starosc::kMinSpectrumPoints; n <= starosc::kMaxSpectrumPoints; n *= 2)
        points_->addItem(QLocale().toString(static_cast<qulonglong>(n)), static_cast<qulonglong>(n));
    points_->setStatusTip(tr("FFT length: more points, finer frequency resolution and a longer capture"));
    window_ = new QComboBox(box);
    for (int i = 0; i < starosc::kWindows; ++i)
        window_->addItem(windowName(static_cast<starosc::Window>(i)));
    window_->setStatusTip(tr("Flat top for accurate levels, Blackman-Harris or Kaiser for weak tones next to strong "
                           "ones, Hann for general use, Rectangular for signals that fit the record exactly"));
    axis_ = new QComboBox(box);
    axis_->addItems({tr("Linear"), tr("Logarithmic")});
    start_ = new SiSpinBox(QStringLiteral("Hz"), 1, box);
    stop_ = new SiSpinBox(QStringLiteral("Hz"), 1, box);
    for (SiSpinBox* s : {start_, stop_}) {
        s->setRange(0, 1e9);
        s->setStepsOf125(true);
        s->setKeyboardTracking(false);
    }
    auto* full = new QPushButton(tr("Full Span"), box);
    full->setStatusTip(tr("Show from 0 Hz to half the sample rate (or double-click the display)"));
    auto* spanRow = new QHBoxLayout;
    spanRow->addWidget(start_, 1);
    spanRow->addWidget(new QLabel(tr("to"), box));
    spanRow->addWidget(stop_, 1);
    resolution_ = new QLabel(box);
    resolution_->setObjectName("spectrumResolution");
    resolution_->setWordWrap(true);

    auto* form = new QFormLayout(box);
    form->addRow(tr("Sample rate"), rate_);
    form->addRow(tr("Points"), points_);
    form->addRow(tr("Window"), window_);
    form->addRow(tr("Axis"), axis_);
    form->addRow(tr("Span"), spanRow);
    form->addRow(QString(), full);
    form->addRow(tr("Resolution"), resolution_);

    for (QComboBox* c : {rate_, points_, window_, axis_})
        connect(c, &QComboBox::currentIndexChanged, this, &SpectrumPanel::readWidgets);
    for (SiSpinBox* s : {start_, stop_})
        connect(s, &QDoubleSpinBox::valueChanged, this, &SpectrumPanel::readWidgets);
    connect(full, &QPushButton::clicked, this, [this] {
        settings_.start_hz = settings_.stop_hz = 0;
        showSettings();
        emit settingsChanged(settings_);
    });
    return box;
}

QWidget* SpectrumPanel::createLevel()
{
    auto* box = new QGroupBox(tr("Level"), this);
    unit_ = new QComboBox(box);
    for (int i = 0; i < starosc::kSpectrumUnits; ++i) {
        const auto unit = static_cast<starosc::SpectrumUnit>(i);
        QString name = QString::fromUtf8(starosc::unit_name(unit).data());
        if (unit == starosc::SpectrumUnit::Vrms)
            name = tr("V (RMS)");
        else if (unit == starosc::SpectrumUnit::dBm)
            name = tr("dBm (50 Ω)");
        unit_->addItem(name);
    }
    unit_->setStatusTip(tr("dBm assumes a 50 Ω load; the input itself is 1 MΩ"));
    reference_ = new QDoubleSpinBox(box);
    reference_->setKeyboardTracking(false);
    reference_->setStatusTip(tr("Level at the top of the screen (or turn the wheel over the level labels)"));
    dbPerDiv_ = new QComboBox(box);
    for (double d : kDbPerDiv)
        dbPerDiv_->addItem(tr("%1 dB/div").arg(d), d);

    auto* form = new QFormLayout(box);
    form->addRow(tr("Unit"), unit_);
    form->addRow(tr("Reference"), reference_);
    form->addRow(tr("Scale"), dbPerDiv_);
    connect(unit_, &QComboBox::currentIndexChanged, this, &SpectrumPanel::readWidgets);
    connect(dbPerDiv_, &QComboBox::currentIndexChanged, this, &SpectrumPanel::readWidgets);
    connect(reference_, &QDoubleSpinBox::valueChanged, this, &SpectrumPanel::readWidgets);
    return box;
}

QWidget* SpectrumPanel::createAveraging()
{
    auto* box = new QGroupBox(tr("Averaging"), this);
    averaging_ = new QComboBox(box);
    averaging_->addItems({tr("Off"), tr("Average"), tr("Peak hold")});
    averages_ = new QSpinBox(box);
    averages_->setRange(2, starosc::kMaxSpectrumAverages);
    averages_->setKeyboardTracking(false);
    averages_->setStatusTip(tr("Spectra in the running average"));
    auto* restart = new QPushButton(tr("Restart"), box);
    restart->setStatusTip(tr("Start the average or the peak hold over"));
    auto* row = new QHBoxLayout;
    row->addWidget(averaging_, 1);
    row->addWidget(averages_);
    auto* form = new QFormLayout(box);
    form->addRow(tr("Mode"), row);
    form->addRow(QString(), restart);
    connect(averaging_, &QComboBox::currentIndexChanged, this, &SpectrumPanel::readWidgets);
    connect(averages_, &QSpinBox::valueChanged, this, &SpectrumPanel::readWidgets);
    connect(restart, &QPushButton::clicked, this, &SpectrumPanel::restartRequested);
    return box;
}

QWidget* SpectrumPanel::createAnalysis()
{
    auto* box = new QGroupBox(tr("Analysis"), this);
    harmonics_ = new QSpinBox(box);
    harmonics_->setRange(starosc::kMinHarmonics, starosc::kMaxHarmonics);
    harmonics_->setKeyboardTracking(false);
    harmonics_->setStatusTip(tr("Highest harmonic counted in THD and SINAD"));
    markers_ = new QComboBox(box);
    markers_->addItems({tr("None"), tr("CH1"), tr("CH2"), tr("Math")});
    markers_->setStatusTip(tr("Trace whose fundamental and harmonics are marked on the display"));
    auto* form = new QFormLayout(box);
    form->addRow(tr("Harmonics"), harmonics_);
    form->addRow(tr("Markers"), markers_);
    connect(harmonics_, &QSpinBox::valueChanged, this, &SpectrumPanel::readWidgets);
    connect(markers_, &QComboBox::currentIndexChanged, this, &SpectrumPanel::readWidgets);
    return box;
}

void SpectrumPanel::refreshColors()
{
    for (int src = 0; src < starosc::kSpectrumSources; ++src)
        traces_[static_cast<std::size_t>(src)].shown->setIcon(swatch(SpectrumView::traceColor(src)));
}

void SpectrumPanel::setSettings(const starosc::SpectrumSettings& settings)
{
    settings_ = settings;
    showSettings();
}

void SpectrumPanel::setScopeSettings(const starosc::ScopeSettings& settings)
{
    scope_ = settings;
    showSettings();
}

void SpectrumPanel::setSampleRates(const std::vector<std::uint32_t>& rates)
{
    rates_ = rates.empty() ? kDefaultRates : rates;
    const bool was = std::exchange(updating_, true);
    rate_->clear();
    for (std::uint32_t r : rates_)
        rate_->addItem(tr("%1 (span %2)").arg(si(r, "Sa/s", 3, true), si(r / 2.0, "Hz", 3, true)), r);
    updating_ = was;
    showSettings();
}

void SpectrumPanel::setResolutionInfo(const QString& text) { resolution_->setText(text); }

void SpectrumPanel::showSettings()
{
    const bool was = std::exchange(updating_, true);
    const auto& s = settings_;
    for (int src = 0; src < starosc::kSpectrumSources; ++src) {
        TraceWidgets& w = traces_[static_cast<std::size_t>(src)];
        w.shown->setChecked(s.shown[src]);
        if (src < starosc::kChannels) {
            const starosc::ChannelSettings& cs = scope_.channel[src];
            if (w.scale->property("probe").toDouble() != cs.probe) {
                w.scale->clear();
                for (double v : starosc::volts_per_div_steps(cs.probe))
                    w.scale->addItem(si(v, "V/div", 3, true), v);
                w.scale->setProperty("probe", cs.probe);
            }
            w.scale->setCurrentIndex(closestIndex(w.scale, cs.volts_per_div));
            w.coupling->setCurrentIndex(cs.coupling == starosc::Coupling::AC ? 1 : 0);
            const double half = starosc::kDivisionsY / 2.0;
            w.range->setText(tr("Range %1 to %2%3")
                                 .arg(si((-half - cs.position_div) * cs.volts_per_div, "V", 3, true),
                                      si((half - cs.position_div) * cs.volts_per_div, "V", 3, true),
                                      cs.probe != 1 ? tr(", probe ×%1").arg(cs.probe) : QString()));
        } else {
            const starosc::MathSettings& m = scope_.math;
            w.range->setText(tr("CH%1 %2 CH%3, set on the Oscilloscope tab")
                                 .arg(m.a + 1)
                                 .arg(QString::fromUtf8(starosc::math_symbol(m.operation).data()))
                                 .arg(m.b + 1));
        }
    }

    std::size_t rateIndex = 0;
    for (std::size_t i = 0; i < rates_.size(); ++i)
        if (rates_[i] <= s.sample_rate_hz)
            rateIndex = i;
    rate_->setCurrentIndex(static_cast<int>(rateIndex));
    const double nyquist = rates_[rateIndex] / 2.0;
    points_->setCurrentIndex(std::max(0, points_->findData(static_cast<qulonglong>(s.points))));
    window_->setCurrentIndex(static_cast<int>(s.window));
    axis_->setCurrentIndex(s.log_frequency ? 1 : 0);
    const bool full = !(s.stop_hz > s.start_hz);
    start_->setValue(full ? 0 : s.start_hz);
    stop_->setValue(full ? nyquist : std::min(s.stop_hz, nyquist));

    unit_->setCurrentIndex(static_cast<int>(s.unit));
    const bool db = starosc::unit_is_db(s.unit);
    const QString unit = QString::fromUtf8(starosc::unit_name(s.unit).data());
    if (db) {
        const double offset = starosc::level_from_dbv(0, s.unit);
        reference_->setDecimals(1);
        reference_->setRange(-200 + offset, 100 + offset);
        reference_->setSingleStep(s.db_per_div);
    } else {
        reference_->setDecimals(4);
        reference_->setRange(1e-4, 100);
        reference_->setSingleStep(0.1);
    }
    reference_->setSuffix(" " + unit);
    reference_->setValue(starosc::level_from_dbv(s.top_dbv, s.unit));
    dbPerDiv_->setCurrentIndex(closestIndex(dbPerDiv_, s.db_per_div));
    dbPerDiv_->setEnabled(db);

    averaging_->setCurrentIndex(static_cast<int>(s.averaging));
    averages_->setValue(s.averages);
    averages_->setEnabled(s.averaging == starosc::SpectrumAveraging::Average);
    harmonics_->setValue(s.harmonics);
    markers_->setCurrentIndex(s.marker_source + 1);
    updating_ = was;
}

void SpectrumPanel::readWidgets()
{
    if (updating_)
        return;
    starosc::SpectrumSettings& s = settings_;
    for (int src = 0; src < starosc::kSpectrumSources; ++src)
        s.shown[src] = traces_[static_cast<std::size_t>(src)].shown->isChecked();
    const auto rate = static_cast<std::uint32_t>(rate_->currentData().toUInt());
    s.points = static_cast<std::size_t>(points_->currentData().toULongLong());
    s.window = static_cast<starosc::Window>(window_->currentIndex());
    s.log_frequency = axis_->currentIndex() == 1;

    // The span from the boxes; a new rate shows its whole span.
    const double nyquist = rate / 2.0;
    if (rate != s.sample_rate_hz) {
        s.start_hz = s.stop_hz = 0;
    } else if (stop_->value() > start_->value()) {
        const bool whole = start_->value() <= 0 && stop_->value() >= nyquist * 0.9999;
        s.start_hz = whole ? 0 : start_->value();
        s.stop_hz = whole ? 0 : std::min(stop_->value(), nyquist);
    }
    s.sample_rate_hz = rate;

    const auto unit = static_cast<starosc::SpectrumUnit>(unit_->currentIndex());
    if (unit == s.unit)  // a new unit keeps the same level at the top
        s.top_dbv = starosc::dbv_from_level(reference_->value(), unit);
    s.unit = unit;
    s.db_per_div = dbPerDiv_->currentData().toDouble();
    s.averaging = static_cast<starosc::SpectrumAveraging>(averaging_->currentIndex());
    s.averages = averages_->value();
    s.harmonics = harmonics_->value();
    s.marker_source = markers_->currentIndex() - 1;
    settings_ = starosc::constrained(s);
    showSettings();
    emit settingsChanged(settings_);
}

void SpectrumPanel::readScope()
{
    if (updating_)
        return;
    for (int ch = 0; ch < starosc::kChannels; ++ch) {
        const TraceWidgets& w = traces_[static_cast<std::size_t>(ch)];
        scope_.channel[ch].volts_per_div = w.scale->currentData().toDouble();
        scope_.channel[ch].coupling = w.coupling->currentIndex() == 1 ? starosc::Coupling::AC : starosc::Coupling::DC;
    }
    showSettings();
    emit scopeSettingsChanged(scope_);
}
