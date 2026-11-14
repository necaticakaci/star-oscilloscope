// SPDX-License-Identifier: GPL-3.0-or-later
#include "controlpanel.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QIcon>
#include <QMenu>
#include <QPixmap>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

#include "displaystyle.hpp"
#include "probebox.hpp"
#include "starosc/math.hpp"
#include "starosc/scale.hpp"
#include "starosc/zoom.hpp"

namespace {

QString stepLabel(double value, const char* unit)
{
    return QString::fromStdString(starosc::format_si(value, unit, 3, true));
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

}  // namespace

ControlPanel::ControlPanel(QWidget* parent) : ScopePanel(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(6);
    for (int ch = 0; ch < starosc::kChannels; ++ch)
        layout->addWidget(createChannel(ch));
    layout->addWidget(createTimebase());
    layout->addWidget(createTrigger());
    layout->addWidget(createMath());
    layout->addWidget(createReferences());
    layout->addStretch(1);
    showSettings();
}

QGroupBox* ControlPanel::createMath()
{
    math_ = new QGroupBox(tr("Math"), this);
    math_->setCheckable(true);
    mathA_ = new QComboBox(math_);
    mathB_ = new QComboBox(math_);
    for (QComboBox* c : {mathA_, mathB_})
        c->addItems({tr("CH1"), tr("CH2")});
    mathOperation_ = new QComboBox(math_);
    for (int i = 0; i < starosc::kMathOperations; ++i)
        mathOperation_->addItem(QString::fromUtf8(starosc::math_symbol(static_cast<starosc::MathOperation>(i)).data()));
    auto* function = new QHBoxLayout;
    function->addWidget(mathA_, 1);
    function->addWidget(mathOperation_);
    function->addWidget(mathB_, 1);
    mathScale_ = new QComboBox(math_);
    mathPosition_ = new QDoubleSpinBox(math_);
    mathPosition_->setRange(-starosc::kDivisionsY / 2.0, starosc::kDivisionsY / 2.0);
    mathPosition_->setSingleStep(0.1);
    mathPosition_->setDecimals(2);
    mathPosition_->setSuffix(tr(" div"));
    mathPosition_->setKeyboardTracking(false);

    auto* form = new QFormLayout(math_);
    form->addRow(tr("Function"), function);
    form->addRow(tr("Scale"), mathScale_);
    form->addRow(tr("Position"), mathPosition_);

    connect(math_, &QGroupBox::toggled, this, &ControlPanel::readWidgets);
    for (QComboBox* c : {mathA_, mathOperation_, mathB_, mathScale_})
        connect(c, &QComboBox::currentIndexChanged, this, &ControlPanel::readWidgets);
    connect(mathPosition_, &QDoubleSpinBox::valueChanged, this, &ControlPanel::readWidgets);
    return math_;
}

QGroupBox* ControlPanel::createReferences()
{
    auto* box = new QGroupBox(tr("Reference"), this);
    auto* grid = new QGridLayout(box);
    grid->setColumnStretch(1, 1);
    for (int r = 0; r < static_cast<int>(references_.size()); ++r) {
        ReferenceWidgets& w = references_[r];
        w.shown = new QCheckBox(tr("R%1").arg(r + 1), box);
        w.shown->setStatusTip(tr("Show the stored waveform"));
        w.shown->setIcon(display_style::swatch(display_style::Ref1 + r));
        w.source = new QComboBox(box);
        w.source->addItems({tr("CH1"), tr("CH2"), tr("Math")});
        w.source->setStatusTip(tr("Trace to store"));
        auto* store = new QPushButton(tr("Store"), box);
        store->setStatusTip(tr("Store the trace as it is on the screen now"));
        auto* more = new QToolButton(box);
        more->setText(tr("…"));
        more->setPopupMode(QToolButton::InstantPopup);
        auto* menu = new QMenu(more);
        menu->addAction(tr("Save As…"), this, [this, r] { emit referenceSaveRequested(r); });
        menu->addAction(tr("Load…"), this, [this, r] { emit referenceLoadRequested(r); });
        menu->addAction(tr("Clear"), this, [this, r] { emit referenceClearRequested(r); });
        more->setMenu(menu);
        w.description = new QLabel(box);
        w.description->setEnabled(false);

        grid->addWidget(w.shown, 2 * r, 0);
        grid->addWidget(w.source, 2 * r, 1);
        grid->addWidget(store, 2 * r, 2);
        grid->addWidget(more, 2 * r, 3);
        grid->addWidget(w.description, 2 * r + 1, 0, 1, 4);

        connect(w.shown, &QCheckBox::toggled, this, [this, r](bool on) {
            if (!updating_)
                emit referenceShown(r, on);
        });
        connect(store, &QPushButton::clicked, this, [this, r] {
            emit referenceStoreRequested(r, references_[r].source->currentIndex());
        });
        setReferenceState(r, {}, false);
    }
    return box;
}

void ControlPanel::refreshColors()
{
    for (int r = 0; r < static_cast<int>(references_.size()); ++r)
        references_[static_cast<std::size_t>(r)].shown->setIcon(display_style::swatch(display_style::Ref1 + r));
}

void ControlPanel::setReferenceState(int ref, const QString& description, bool shown)
{
    ReferenceWidgets& w = references_[static_cast<std::size_t>(ref)];
    const bool was = std::exchange(updating_, true);
    w.shown->setEnabled(!description.isEmpty());
    w.shown->setChecked(shown && !description.isEmpty());
    w.description->setText(description.isEmpty() ? tr("empty") : description);
    updating_ = was;
}

QGroupBox* ControlPanel::createChannel(int channel)
{
    ChannelWidgets& w = channels_[channel];
    w.box = new QGroupBox(tr("Channel %1").arg(channel + 1), this);
    w.box->setCheckable(true);

    w.scale = new QComboBox(w.box);
    w.position = new QDoubleSpinBox(w.box);
    w.position->setObjectName(QString("position%1").arg(channel + 1));
    w.position->setRange(-starosc::kDivisionsY / 2.0, starosc::kDivisionsY / 2.0);
    w.position->setSingleStep(0.1);
    w.position->setDecimals(2);
    w.position->setSuffix(tr(" div"));
    w.position->setKeyboardTracking(false);

    w.dc = new QRadioButton(tr("DC"), w.box);
    w.ac = new QRadioButton(tr("AC"), w.box);
    auto* coupling = new QButtonGroup(w.box);
    coupling->addButton(w.dc);
    coupling->addButton(w.ac);
    auto* couplingRow = new QHBoxLayout;
    couplingRow->addWidget(w.dc);
    couplingRow->addWidget(w.ac);
    couplingRow->addStretch(1);

    w.probe = new ProbeBox(w.box);
    w.probe->setObjectName(QString("probe%1").arg(channel + 1));

    auto* form = new QFormLayout(w.box);
    form->addRow(tr("Scale"), w.scale);
    form->addRow(tr("Position"), w.position);
    form->addRow(tr("Coupling"), couplingRow);
    form->addRow(tr("Probe"), w.probe);

    connect(w.box, &QGroupBox::toggled, this, &ControlPanel::readWidgets);
    connect(w.scale, &QComboBox::currentIndexChanged, this, &ControlPanel::readWidgets);
    connect(w.position, &QDoubleSpinBox::valueChanged, this, &ControlPanel::readWidgets);
    connect(w.dc, &QRadioButton::toggled, this, &ControlPanel::readWidgets);
    connect(w.probe, &ProbeBox::probeChosen, this, [this, channel](double probe) { changeProbe(channel, probe); });
    return w.box;
}

QGroupBox* ControlPanel::createTimebase()
{
    auto* box = new QGroupBox(tr("Timebase"), this);
    timeScale_ = new QComboBox(box);
    for (double s : starosc::seconds_per_div_steps())
        timeScale_->addItem(stepLabel(s, "s/div"), s);
    memory_ = new QComboBox(box);
    memory_->addItem(tr("Small (fast update)"), static_cast<int>(starosc::MemoryDepth::Small));
    memory_->addItem(tr("Medium"), static_cast<int>(starosc::MemoryDepth::Medium));
    memory_->addItem(tr("Large (high rate)"), static_cast<int>(starosc::MemoryDepth::Large));
    acquisitionInfo_ = new QLabel(box);
    acquisitionInfo_->setObjectName("acquisitionInfo");
    zoom_ = new QComboBox(box);
    zoom_->setObjectName("zoomScale");
    zoom_->setStatusTip(tr("Show a part of the record with a faster timebase. The capture and the measurements stay on "
                         "the main timebase."));
    zoomPosition_ = new QDoubleSpinBox(box);
    zoomPosition_->setObjectName("zoomPosition");
    zoomPosition_->setRange(0, 100);
    zoomPosition_->setDecimals(3);
    zoomPosition_->setSuffix(tr(" %"));
    zoomPosition_->setKeyboardTracking(false);
    zoomPosition_->setStatusTip(tr("Centre of the zoomed part, in percent of the record"));
    display_ = new QComboBox(box);
    display_->addItem(tr("Y-T"), static_cast<int>(starosc::DisplayMode::YT));
    display_->addItem(tr("X-Y (CH1 across, CH2 up)"), static_cast<int>(starosc::DisplayMode::XY));

    auto* form = new QFormLayout(box);
    form->addRow(tr("Scale"), timeScale_);
    form->addRow(tr("Memory"), memory_);
    form->addRow(tr("Sampling"), acquisitionInfo_);
    form->addRow(tr("Zoom"), zoom_);
    form->addRow(tr("Zoom pos."), zoomPosition_);
    form->addRow(tr("Display"), display_);

    connect(timeScale_, &QComboBox::currentIndexChanged, this, &ControlPanel::readWidgets);
    connect(memory_, &QComboBox::currentIndexChanged, this, &ControlPanel::readWidgets);
    connect(zoom_, &QComboBox::currentIndexChanged, this, &ControlPanel::readWidgets);
    connect(zoomPosition_, &QDoubleSpinBox::valueChanged, this, &ControlPanel::readWidgets);
    connect(display_, &QComboBox::currentIndexChanged, this, &ControlPanel::readWidgets);
    return box;
}

QGroupBox* ControlPanel::createTrigger()
{
    auto* box = new QGroupBox(tr("Trigger"), this);
    triggerMode_ = new QComboBox(box);
    triggerMode_->addItems({tr("Auto"), tr("Normal"), tr("Single")});
    triggerSource_ = new QComboBox(box);
    triggerSource_->addItems({tr("CH1"), tr("CH2")});
    triggerEdge_ = new QComboBox(box);
    triggerEdge_->addItems({tr("Edge, rising"), tr("Edge, falling"), tr("Edge, either")});
    triggerLevel_ = new QDoubleSpinBox(box);
    triggerLevel_->setDecimals(3);
    triggerLevel_->setSuffix(tr(" V"));
    triggerLevel_->setKeyboardTracking(false);
    sensitivity_ = new QDoubleSpinBox(box);
    sensitivity_->setRange(0.1, 1.0);
    sensitivity_->setSingleStep(0.1);
    sensitivity_->setDecimals(1);
    sensitivity_->setSuffix(tr(" div"));
    sensitivity_->setKeyboardTracking(false);
    sensitivity_->setStatusTip(tr("Hysteresis: how far the signal must swing back before the trigger re-arms"));
    preTrigger_ = new QSpinBox(box);
    preTrigger_->setRange(5, 95);
    preTrigger_->setSuffix(tr(" %"));
    preTrigger_->setKeyboardTracking(false);

    auto* form = new QFormLayout(box);
    form->addRow(tr("Mode"), triggerMode_);
    form->addRow(tr("Source"), triggerSource_);
    form->addRow(tr("Type"), triggerEdge_);
    form->addRow(tr("Level"), triggerLevel_);
    form->addRow(tr("Sensitivity"), sensitivity_);
    form->addRow(tr("Pre-trigger"), preTrigger_);

    connect(triggerMode_, &QComboBox::currentIndexChanged, this, &ControlPanel::readWidgets);
    connect(triggerSource_, &QComboBox::currentIndexChanged, this, &ControlPanel::readWidgets);
    connect(triggerEdge_, &QComboBox::currentIndexChanged, this, &ControlPanel::readWidgets);
    connect(triggerLevel_, &QDoubleSpinBox::valueChanged, this, &ControlPanel::readWidgets);
    connect(sensitivity_, &QDoubleSpinBox::valueChanged, this, &ControlPanel::readWidgets);
    connect(preTrigger_, &QSpinBox::valueChanged, this, &ControlPanel::readWidgets);
    return box;
}

void ControlPanel::setAcquisitionInfo(const QString& text) { acquisitionInfo_->setText(text); }

void ControlPanel::showSettings()
{
    const bool wasUpdating = std::exchange(updating_, true);
    for (int ch = 0; ch < starosc::kChannels; ++ch) {
        const auto& cs = settings_.channel[ch];
        ChannelWidgets& w = channels_[ch];
        w.box->setChecked(cs.enabled);
        w.probe->showProbe(cs);
        if (w.scale->property("probe").toDouble() != cs.probe) {
            w.scale->clear();
            for (double v : starosc::volts_per_div_steps(cs.probe))
                w.scale->addItem(stepLabel(v, "V/div"), v);
            w.scale->setProperty("probe", cs.probe);
        }
        w.scale->setCurrentIndex(closestIndex(w.scale, cs.volts_per_div));
        w.position->setValue(cs.position_div);
        (cs.coupling == starosc::Coupling::DC ? w.dc : w.ac)->setChecked(true);
    }
    timeScale_->setCurrentIndex(closestIndex(timeScale_, settings_.timebase.seconds_per_div));
    memory_->setCurrentIndex(static_cast<int>(settings_.timebase.memory));

    const auto zoomList = qMakePair(settings_.timebase.seconds_per_div, sampleRate_);
    if (zoom_->property("list").value<QPair<double, double>>() != zoomList || zoom_->count() == 0) {
        zoom_->clear();
        zoom_->addItem(tr("Off"), 0.0);
        for (double s : starosc::zoom_steps(settings_.timebase.seconds_per_div, sampleRate_))
            zoom_->addItem(stepLabel(s, "s/div"), s);
        zoom_->setProperty("list", QVariant::fromValue(zoomList));
    }
    const bool yt = settings_.display == starosc::DisplayMode::YT;
    zoom_->setCurrentIndex(settings_.zoom.on() ? closestIndex(zoom_, settings_.zoom.seconds_per_div) : 0);
    zoom_->setEnabled(yt);
    zoomPosition_->setEnabled(yt && settings_.zoom.on());
    const starosc::VisibleRange range = starosc::visible_range(settings_.timebase, settings_.zoom);
    zoomPosition_->setValue((range.begin + range.span / 2) * 100);
    zoomPosition_->setSingleStep(std::max(0.001, range.span * 10));  // a tenth of the screen

    const auto& t = settings_.trigger;
    triggerMode_->setCurrentIndex(static_cast<int>(t.mode));
    triggerSource_->setCurrentIndex(t.source);
    triggerEdge_->setCurrentIndex(static_cast<int>(t.edge));
    const auto& src = settings_.channel[t.source];
    const double half = starosc::kDivisionsY / 2.0;
    triggerLevel_->setRange((-half - src.position_div) * src.volts_per_div, (half - src.position_div) * src.volts_per_div);
    triggerLevel_->setSingleStep(src.volts_per_div / 10);
    triggerLevel_->setValue(t.level_v);
    sensitivity_->setValue(t.hysteresis_div);
    preTrigger_->setValue(static_cast<int>(std::lround(t.pre_trigger * 100)));
    display_->setCurrentIndex(static_cast<int>(settings_.display));

    const auto& m = settings_.math;
    math_->setChecked(m.enabled);
    mathA_->setCurrentIndex(m.a);
    mathOperation_->setCurrentIndex(static_cast<int>(m.operation));
    mathB_->setCurrentIndex(m.b);
    const char* unit = starosc::math_unit(m.operation).data();
    const QString scaleUnit = QString::fromUtf8(unit) + "/div";
    if (mathScale_->property("unit").toString() != scaleUnit) {
        mathScale_->clear();
        for (double v : starosc::units_per_div_steps())
            mathScale_->addItem(QString::fromStdString(starosc::format_si(v, scaleUnit.toStdString(), 3, true)), v);
        mathScale_->setProperty("unit", scaleUnit);
    }
    mathScale_->setCurrentIndex(closestIndex(mathScale_, m.units_per_div));
    mathPosition_->setValue(m.position_div);
    updating_ = wasUpdating;
}

void ControlPanel::readWidgets()
{
    if (updating_)
        return;
    for (int ch = 0; ch < starosc::kChannels; ++ch) {
        auto& cs = settings_.channel[ch];
        const ChannelWidgets& w = channels_[ch];
        cs.enabled = w.box->isChecked();
        cs.volts_per_div = w.scale->currentData().toDouble();
        cs.position_div = w.position->value();
        cs.coupling = w.dc->isChecked() ? starosc::Coupling::DC : starosc::Coupling::AC;
    }
    settings_.timebase.seconds_per_div = timeScale_->currentData().toDouble();
    settings_.timebase.memory = static_cast<starosc::MemoryDepth>(memory_->currentData().toInt());
    settings_.zoom.seconds_per_div = zoom_->currentData().toDouble();
    // The box rounds; the centre changes only when the box shows something else, or a large zoom would jump.
    const starosc::VisibleRange range = starosc::visible_range(settings_.timebase, settings_.zoom);
    if (std::abs(zoomPosition_->value() - (range.begin + range.span / 2) * 100) >= 0.001)
        settings_.zoom.center = zoomPosition_->value() / 100;


    auto& t = settings_.trigger;
    t.mode = static_cast<starosc::TriggerMode>(triggerMode_->currentIndex());
    t.source = triggerSource_->currentIndex();
    t.edge = static_cast<starosc::TriggerEdge>(triggerEdge_->currentIndex());
    t.level_v = triggerLevel_->value();
    t.hysteresis_div = sensitivity_->value();
    t.pre_trigger = preTrigger_->value() / 100.0;
    settings_.display = static_cast<starosc::DisplayMode>(display_->currentData().toInt());

    auto& m = settings_.math;
    m.enabled = math_->isChecked();
    m.a = mathA_->currentIndex();
    m.b = mathB_->currentIndex();
    const auto operation = static_cast<starosc::MathOperation>(mathOperation_->currentIndex());
    // A new operation changes the unit; its scale list is rebuilt, so the old scale is kept instead.
    if (operation == m.operation)
        m.units_per_div = mathScale_->currentData().toDouble();
    m.operation = operation;
    m.position_div = mathPosition_->value();

    showSettings();  // keeps dependent ranges (trigger level) in step
    emit settingsChanged(settings_);
}
