// SPDX-License-Identifier: GPL-3.0-or-later
#include "generatorpanel.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPushButton>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

#include "displaystyle.hpp"
#include "icons.hpp"
#include "sispinbox.hpp"
#include "starosc/scale.hpp"
#include "starosc/settings.hpp"

namespace {

using starosc::Waveform;

const QColor kGrid(0x3c, 0x3c, 0x3c);
const QColor kAxis(0x60, 0x60, 0x60);
const QColor kLimit(0xa0, 0x40, 0x40);
const QColor kTrace(0xe0, 0x8a, 0x2e);  // generator output; neither channel's colour
const QColor kTraceOff(0x70, 0x70, 0x70);
constexpr double kPreviewVoltsPerDiv = 2.0;  // fixed, so the output limits are always in view
constexpr int kPreviewPeriods = 2;
constexpr double kAmplitudeDropHz = 20e3;  // from here a reading through the x1 input is noticeably low (−6 %)

QString si(double value, const char* unit, int digits = 4) { return QString::fromStdString(starosc::format_si(value, unit, digits)); }

QLabel* sunkenLabel(QWidget* parent)
{
    auto* label = new QLabel(parent);
    label->setFrameStyle(QFrame::Panel | QFrame::Sunken);
    return label;
}

QPixmap led(bool on)
{
    QPixmap pm(10, 10);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setPen(QColor(0x1a, 0x1a, 0x1a));
    p.setBrush(on ? QColor(0x38, 0xd0, 0x38) : QColor(0x50, 0x50, 0x50));
    p.drawRect(0, 0, 9, 9);
    return pm;
}

}  // namespace

namespace generator_ui {

QString waveformName(Waveform w)
{
    switch (w) {
    case Waveform::Sine: return QObject::tr("Sine");
    case Waveform::Square: return QObject::tr("Square");
    case Waveform::Triangle: return QObject::tr("Triangle");
    case Waveform::RampUp: return QObject::tr("Ramp up");
    case Waveform::RampDown: return QObject::tr("Ramp down");
    }
    return {};
}

QIcon waveformIcon(Waveform w)
{
    // Two periods drawn with the output model itself, in the palette's text colour like the toolbar icons.
    return icons::drawn(QSize(28, 14), [w](QPainter& p, const QColor& ink) {
        starosc::GeneratorSettings g;
        g.waveform = w;
        g.amplitude_vpp = 2;
        g.duty_percent = 50;
        p.setPen(QPen(ink, 1.3));
        QPolygonF line;
        for (int x = 0; x <= 26; ++x) {
            const double t = (x + 0.001) / 26.0 * 2 / g.frequency_hz;
            line << QPointF(1 + x, 7 - 5 * starosc::generator_output(g, t));
        }
        p.drawPolyline(line);
    });
}

QString summary(const starosc::GeneratorSettings& g)
{
    const QString frequency =
        g.sweep.enabled ? QObject::tr("sweep %1 – %2").arg(si(g.sweep.start_hz, "Hz"), si(g.sweep.stop_hz, "Hz"))
                        : si(g.frequency_hz, "Hz");
    return QObject::tr("%1, %2, %3, offset %4")
        .arg(waveformName(g.waveform).toLower(), frequency, si(g.amplitude_vpp, "Vpp"), si(g.offset_v, "V"));
}

QPushButton* outputButton(QWidget* parent)
{
    auto* b = new QPushButton(parent);
    b->setCheckable(true);
    QFont f = b->font();
    f.setBold(true);
    b->setFont(f);
    b->setStatusTip(QObject::tr("DDS OUT on or off. Off parks the output near 0 V."));
    showOutput(b, false);
    return b;
}

void showOutput(QPushButton* b, bool on)
{
    b->setChecked(on);
    b->setText(on ? QObject::tr("Output ON") : QObject::tr("Output OFF"));
    b->setIcon(QIcon(led(on)));
}

SiSpinBox* frequencyBox(QWidget* parent)
{
    auto* b = new SiSpinBox("Hz", 1, parent);
    b->setRange(1, 20e6);
    b->setStepsOf125(true);
    b->setStatusTip(QObject::tr("Sine 1 Hz – 20 MHz, the other waveforms up to 2 MHz"));
    return b;
}

SiSpinBox* amplitudeBox(QWidget* parent)
{
    auto* b = new SiSpinBox("Vpp", 0.001, parent);
    b->setObjectName("amplitude");
    b->setStatusTip(QObject::tr("0.08 – 9.7 Vpp, clean up to about 6 Vpp"));
    const auto l = starosc::generator_limits(Waveform::Sine);
    b->setRange(l.min_vpp, l.max_vpp);
    b->setSingleStep(0.1);
    return b;
}

SiSpinBox* offsetBox(QWidget* parent)
{
    auto* b = new SiSpinBox("V", 0.001, parent);
    b->setObjectName("offset");
    const auto l = starosc::generator_limits(Waveform::Sine);
    b->setRange(l.low_v, l.high_v);
    b->setSingleStep(0.1);
    return b;
}

QSpinBox* dutyBox(QWidget* parent)
{
    auto* b = new QSpinBox(parent);
    b->setRange(1, 99);
    b->setSuffix(" %");
    b->setKeyboardTracking(false);
    b->setStatusTip(QObject::tr("Square wave only"));
    return b;
}

}  // namespace generator_ui

GeneratorPreview::GeneratorPreview(QWidget* parent) : QWidget(parent)
{
    setAttribute(Qt::WA_OpaquePaintEvent);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void GeneratorPreview::setSettings(const starosc::GeneratorSettings& settings)
{
    settings_ = settings;
    update();
}

void GeneratorPreview::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.fillRect(rect(), Qt::black);
    const QRectF plot = QRectF(rect()).adjusted(4, 4, -4, -4);
    const double dx = plot.width() / starosc::kDivisionsX;
    const double dy = plot.height() / starosc::kDivisionsY;
    const auto toY = [&](double v) { return plot.center().y() - v / kPreviewVoltsPerDiv * dy; };

    QPen dotted = display_style::hairline(kGrid, p.device(), Qt::CustomDashLine);
    dotted.setDashPattern({1, 3});
    p.setPen(dotted);
    for (int i = 1; i < starosc::kDivisionsX; ++i)
        p.drawLine(QPointF(std::round(plot.left() + i * dx) + 0.5, plot.top()),
                   QPointF(std::round(plot.left() + i * dx) + 0.5, plot.bottom()));
    for (int i = 1; i < starosc::kDivisionsY; ++i)
        p.drawLine(QPointF(plot.left(), std::round(plot.top() + i * dy) + 0.5),
                   QPointF(plot.right(), std::round(plot.top() + i * dy) + 0.5));
    p.setPen(kAxis);
    p.drawLine(QPointF(plot.left(), std::round(toY(0)) + 0.5), QPointF(plot.right(), std::round(toY(0)) + 0.5));
    p.drawRect(plot.adjusted(0.5, 0.5, -0.5, -0.5));

    const auto limits = starosc::generator_limits(settings_.waveform);
    QFont font = p.font();
    font.setPixelSize(10);
    p.setFont(font);
    p.setPen(display_style::hairline(kLimit, p.device(), Qt::DashLine));
    for (double v : {limits.high_v, limits.low_v}) {
        const double y = std::round(toY(v)) + 0.5;
        p.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
    }

    QPolygonF line;
    const int columns = static_cast<int>(plot.width());
    for (int x = 0; x <= columns; ++x) {
        const double t = (x + 0.001) / columns * kPreviewPeriods / settings_.frequency_hz;
        line << QPointF(plot.left() + x, toY(starosc::generator_output(settings_, t)));
    }
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(settings_.output ? kTrace : kTraceOff, 1.5));
    p.drawPolyline(line);
    // The texts are in the status strip under the picture, where they cover nothing.
}

GeneratorPanel::GeneratorPanel(QWidget* parent) : QWidget(parent)
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(8);
    layout->addWidget(createPreview(), 1);
    layout->addWidget(createControls());
    showSettings();
}

QWidget* GeneratorPanel::createPreview()
{
    auto* box = new QWidget(this);
    auto* layout = new QVBoxLayout(box);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);
    layout->addWidget(new QLabel(tr("Preview at DDS OUT, without a load"), box));

    preview_ = new GeneratorPreview(box);
    auto* frame = new QFrame(box);
    frame->setFrameStyle(QFrame::Panel | QFrame::Sunken);
    auto* frameLayout = new QVBoxLayout(frame);
    frameLayout->setContentsMargins(2, 2, 2, 2);
    frameLayout->addWidget(preview_);
    layout->addWidget(frame, 1);

    auto* status = new QHBoxLayout;
    status->setSpacing(3);
    outputState_ = sunkenLabel(box);
    outputState_->setObjectName("previewOutput");
    previewScale_ = sunkenLabel(box);
    previewScale_->setObjectName("previewScale");
    previewScale_->setText(tr("%1/div · %2 periods").arg(si(kPreviewVoltsPerDiv, "V", 2)).arg(kPreviewPeriods));
    outputLimits_ = sunkenLabel(box);
    outputLimits_->setObjectName("previewLimits");
    outputLimits_->setStatusTip(tr("The dashed red lines: the highest and lowest voltage DDS OUT gives with this waveform"));
    // Each takes the width of its text while there is room; in a narrow panel they are cut rather than widening it.
    for (QLabel* label : {outputState_, previewScale_, outputLimits_})
        label->setMinimumWidth(1);
    status->addWidget(outputState_);
    status->addWidget(previewScale_);
    status->addWidget(outputLimits_);
    layout->addLayout(status);

    auto* readouts = new QHBoxLayout;
    readouts->setSpacing(3);
    for (QLabel*& r : readouts_) {
        r = sunkenLabel(box);
        readouts->addWidget(r, 1);
    }
    layout->addLayout(readouts);

    warnings_ = new QLabel(box);
    warnings_->setObjectName("generatorWarnings");
    warnings_->setFrameStyle(QFrame::Panel | QFrame::Plain);
    warnings_->setWordWrap(true);
    warnings_->setMargin(4);
    warnings_->setAutoFillBackground(true);
    QPalette pal = warnings_->palette();
    pal.setColor(QPalette::Window, QColor(0xff, 0xff, 0xe1));
    pal.setColor(QPalette::WindowText, Qt::black);
    warnings_->setPalette(pal);
    layout->addWidget(warnings_);

    auto* note = new QLabel(tr("Levels are typical values measured on one ISDS205X with a 1 kHz sine. "
                               "After power-up DDS OUT sits at about +4.9 V until the generator is set."),
                            box);
    note->setWordWrap(true);
    note->setEnabled(false);
    layout->addWidget(note);
    return box;
}

QWidget* GeneratorPanel::createControls()
{
    auto* box = new QWidget(this);
    auto* layout = new QVBoxLayout(box);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    auto* outputBox = new QGroupBox(tr("Output"), box);
    auto* outputLayout = new QVBoxLayout(outputBox);
    auto* outputRow = new QHBoxLayout;
    output_ = generator_ui::outputButton(outputBox);
    output_->setMinimumHeight(30);
    outputRow->addWidget(output_);
    outputRow->addWidget(new QLabel(tr("DDS OUT"), outputBox));
    outputRow->addStretch(1);
    outputLayout->addLayout(outputRow);
    park_ = new QCheckBox(tr("Park at 0 V when connecting and closing"), outputBox);
    park_->setChecked(true);
    park_->setStatusTip(tr("Otherwise the output keeps its last settings, or +4.9 V after power-up"));
    outputLayout->addWidget(park_);
    noLevels_ = new QLabel(tr("This model sets no amplitude or offset: the output has the level the device gives it, "
                              "and cannot be parked. The preview and the levels are the ISDS205X's."),
                           outputBox);
    noLevels_->setObjectName("noLevels");
    noLevels_->setWordWrap(true);
    noLevels_->setEnabled(false);
    noLevels_->hide();
    outputLayout->addWidget(noLevels_);
    layout->addWidget(outputBox);

    auto* waveBox = new QGroupBox(tr("Waveform"), box);
    auto* waveLayout = new QHBoxLayout(waveBox);
    waveLayout->setSpacing(3);
    waveforms_ = new QButtonGroup(waveBox);
    for (int i = 0; i < starosc::kWaveforms; ++i) {
        const auto w = static_cast<Waveform>(i);
        auto* b = new QToolButton(waveBox);
        b->setCheckable(true);
        b->setIcon(generator_ui::waveformIcon(w));
        b->setIconSize(QSize(28, 14));
        b->setText(generator_ui::waveformName(w));
        b->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        waveforms_->addButton(b, i);
        waveLayout->addWidget(b);
    }
    layout->addWidget(waveBox);

    auto* signalBox = new QGroupBox(tr("Signal"), box);
    auto* form = new QFormLayout(signalBox);
    frequency_ = generator_ui::frequencyBox(signalBox);
    amplitude_ = generator_ui::amplitudeBox(signalBox);
    offset_ = generator_ui::offsetBox(signalBox);
    duty_ = generator_ui::dutyBox(signalBox);
    form->addRow(tr("Frequency"), frequency_);
    form->addRow(tr("Amplitude"), amplitude_);
    form->addRow(tr("Offset"), offset_);
    form->addRow(tr("Duty"), duty_);
    layout->addWidget(signalBox);

    sweep_ = new QGroupBox(tr("Sweep"), box);
    sweep_->setCheckable(true);
    auto* sweepForm = new QFormLayout(sweep_);
    sweepStart_ = generator_ui::frequencyBox(sweep_);
    sweepStop_ = generator_ui::frequencyBox(sweep_);
    sweepTime_ = new QDoubleSpinBox(sweep_);
    sweepTime_->setRange(0.1, 100);
    sweepTime_->setDecimals(1);
    sweepTime_->setSingleStep(0.1);
    sweepTime_->setSuffix(tr(" s"));
    sweepTime_->setKeyboardTracking(false);
    sweepSpacing_ = new QComboBox(sweep_);
    sweepSpacing_->addItems({tr("Logarithmic"), tr("Linear")});
    sweepForm->addRow(tr("Start"), sweepStart_);
    sweepForm->addRow(tr("Stop"), sweepStop_);
    sweepForm->addRow(tr("Time"), sweepTime_);
    sweepForm->addRow(tr("Spacing"), sweepSpacing_);
    auto* sweepHint = new QLabel(tr("The app steps the frequency about\n50 times a second, then starts over."), sweep_);
    sweepHint->setEnabled(false);
    sweepForm->addRow(sweepHint);
    layout->addWidget(sweep_);
    layout->addStretch(1);
    box->setFixedWidth(std::max(300, box->sizeHint().width()));

    connect(output_, &QPushButton::toggled, this, &GeneratorPanel::readWidgets);
    connect(park_, &QCheckBox::toggled, this, &GeneratorPanel::parkOutputChanged);
    connect(waveforms_, &QButtonGroup::idToggled, this, [this](int, bool on) {
        if (on)
            readWidgets();
    });
    for (QDoubleSpinBox* b : {static_cast<QDoubleSpinBox*>(frequency_), static_cast<QDoubleSpinBox*>(amplitude_),
                              static_cast<QDoubleSpinBox*>(offset_), static_cast<QDoubleSpinBox*>(sweepStart_),
                              static_cast<QDoubleSpinBox*>(sweepStop_), sweepTime_})
        connect(b, &QDoubleSpinBox::valueChanged, this, &GeneratorPanel::readWidgets);
    connect(duty_, &QSpinBox::valueChanged, this, &GeneratorPanel::readWidgets);
    connect(sweep_, &QGroupBox::toggled, this, &GeneratorPanel::readWidgets);
    connect(sweepSpacing_, &QComboBox::currentIndexChanged, this, &GeneratorPanel::readWidgets);
    return box;
}

void GeneratorPanel::setSettings(const starosc::GeneratorSettings& settings)
{
    settings_ = starosc::constrained(settings);
    showSettings();
}

void GeneratorPanel::setParkOutput(bool on)
{
    const QSignalBlocker block(park_);
    park_->setChecked(on);
}

void GeneratorPanel::setSensitiveInputs(const std::array<bool, starosc::kChannels>& sensitive)
{
    if (sensitive == sensitive_)
        return;
    sensitive_ = sensitive;
    showSettings();
}

void GeneratorPanel::setLevelControl(bool on)
{
    amplitude_->setEnabled(on);
    offset_->setEnabled(on);
    park_->setEnabled(on);
    noLevels_->setVisible(!on);
}

void GeneratorPanel::showSettings()
{
    const bool was = std::exchange(updating_, true);
    const starosc::GeneratorSettings& g = settings_;
    const auto limits = starosc::generator_limits(g.waveform);

    generator_ui::showOutput(output_, g.output);
    waveforms_->button(static_cast<int>(g.waveform))->setChecked(true);
    for (SiSpinBox* b : {frequency_, sweepStart_, sweepStop_})
        b->setMaximum(limits.max_hz);
    frequency_->setValue(g.frequency_hz);
    frequency_->setEnabled(!g.sweep.enabled);
    amplitude_->setValue(g.amplitude_vpp);
    offset_->setValue(g.offset_v);
    duty_->setValue(g.duty_percent);
    duty_->setEnabled(g.waveform == Waveform::Square);
    sweep_->setChecked(g.sweep.enabled);
    sweepStart_->setValue(g.sweep.start_hz);
    sweepStop_->setValue(g.sweep.stop_hz);
    sweepTime_->setValue(g.sweep.seconds);
    sweepSpacing_->setCurrentIndex(g.sweep.logarithmic ? 0 : 1);

    preview_->setSettings(g);
    outputState_->setText(g.output ? tr("Output on") : tr("Output off, about 0 V"));
    outputState_->setStatusTip(g.output ? QString() : tr("Output off: DDS OUT is parked near 0 V"));
    outputLimits_->setText(QStringLiteral("<span style=\"color:%1\">– – –</span> %2")
                               .arg(kLimit.lighter(130).name(),
                                    tr("Limits %1 / %2").arg("+" + si(limits.high_v, "V", 3), si(limits.low_v, "V", 3))));
    const starosc::GeneratorLevels levels = starosc::generator_levels(g);
    readouts_[0]->setText(tr("Max %1").arg(si(levels.max, "V")));
    readouts_[1]->setText(tr("Min %1").arg(si(levels.min, "V")));
    readouts_[2]->setText(tr("Period %1").arg(si(1 / g.frequency_hz, "s")));
    readouts_[3]->setText(tr("RMS %1").arg(si(levels.rms, "V")));

    QStringList warnings;
    if (levels.clipped)
        warnings << tr("The wave reaches an output limit (%1 / %2) and is clipped.")
                        .arg(si(limits.high_v, "V", 3), si(limits.low_v, "V", 3));
    if (g.amplitude_vpp > limits.clean_vpp)
        warnings << tr("Above about %1 the wave distorts.").arg(si(limits.clean_vpp, "Vpp", 2));
    // An input on its most sensitive stages raises the output; only then is there something to say. Measured on one
    // unit; docs/measurements.md §5.
    if (sensitive_[0] || sensitive_[1]) {
        const QString inputs = sensitive_[0] && sensitive_[1] ? tr("CH1 and CH2") : sensitive_[0] ? tr("CH1") : tr("CH2");
        if (g.amplitude_vpp > 3)
            warnings << tr("%1 on a fine scale: at this level the output grows a lot (5 Vpp became 8.3 Vpp with both "
                           "inputs fine).")
                            .arg(inputs);
        else
            warnings << tr("%1 on a fine scale: the output is up to 20 % higher than set.").arg(inputs);
    }
    const double highest = g.sweep.enabled ? std::max(g.sweep.start_hz, g.sweep.stop_hz) : g.frequency_hz;
    if (highest >= kAmplitudeDropHz)
        warnings << tr("The amplitude is set for 1 kHz. Read through this device's input on a ×1 probe it shows lower: "
                       "about −13 % at 100 kHz, −3 dB at 2 MHz. Up to 100 kHz that is the input, not the output "
                       "(measured on one unit).");
    warnings_->setText(warnings.join('\n'));
    warnings_->setVisible(!warnings.isEmpty());
    updating_ = was;
}

void GeneratorPanel::readWidgets()
{
    if (updating_)
        return;
    starosc::GeneratorSettings& g = settings_;
    g.output = output_->isChecked();
    g.waveform = static_cast<Waveform>(std::max(0, waveforms_->checkedId()));
    g.frequency_hz = frequency_->value();
    g.amplitude_vpp = amplitude_->value();
    g.offset_v = offset_->value();
    g.duty_percent = duty_->value();
    g.sweep.enabled = sweep_->isChecked();
    g.sweep.start_hz = sweepStart_->value();
    g.sweep.stop_hz = sweepStop_->value();
    g.sweep.seconds = sweepTime_->value();
    g.sweep.logarithmic = sweepSpacing_->currentIndex() == 0;
    settings_ = starosc::constrained(g);
    showSettings();
    emit settingsChanged(settings_);
}
