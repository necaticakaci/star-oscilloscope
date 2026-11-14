// SPDX-License-Identifier: GPL-3.0-or-later
#include "generatorstrip.hpp"

#include <algorithm>
#include <utility>

#include <QComboBox>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>

#include "generatorpanel.hpp"
#include "sispinbox.hpp"

GeneratorStrip::GeneratorStrip(QWidget* parent) : QWidget(parent)
{
    auto* grid = new QGridLayout(this);
    grid->setContentsMargins(4, 2, 4, 4);
    grid->setHorizontalSpacing(8);
    grid->setVerticalSpacing(1);

    output_ = generator_ui::outputButton(this);
    output_->setMinimumHeight(30);
    waveform_ = new QComboBox(this);
    waveform_->setIconSize(QSize(28, 14));
    for (int i = 0; i < starosc::kWaveforms; ++i) {
        const auto w = static_cast<starosc::Waveform>(i);
        waveform_->addItem(generator_ui::waveformIcon(w), generator_ui::waveformName(w));
    }
    frequency_ = generator_ui::frequencyBox(this);
    amplitude_ = generator_ui::amplitudeBox(this);
    offset_ = generator_ui::offsetBox(this);
    duty_ = generator_ui::dutyBox(this);
    auto* more = new QPushButton(tr("Generator Tab…"), this);
    more->setStatusTip(tr("Sweep, preview and levels"));

    const std::pair<QString, QWidget*> fields[] = {{tr("Waveform"), waveform_},
                                                   {tr("Frequency"), frequency_},
                                                   {tr("Amplitude"), amplitude_},
                                                   {tr("Offset"), offset_},
                                                   {tr("Duty"), duty_}};
    grid->addWidget(output_, 0, 0, 2, 1, Qt::AlignBottom);
    int column = 1;
    for (const auto& [label, widget] : fields) {
        grid->addWidget(new QLabel(label, this), 0, column);
        grid->addWidget(widget, 1, column);
        grid->setColumnStretch(column, 1);
        ++column;
    }
    grid->addWidget(more, 1, column);
    sweep_ = new QLabel(this);
    sweep_->setEnabled(false);
    grid->addWidget(sweep_, 0, column);

    connect(output_, &QPushButton::toggled, this, &GeneratorStrip::readWidgets);
    connect(waveform_, &QComboBox::currentIndexChanged, this, &GeneratorStrip::readWidgets);
    for (SiSpinBox* b : {frequency_, amplitude_, offset_})
        connect(b, &QDoubleSpinBox::valueChanged, this, &GeneratorStrip::readWidgets);
    connect(duty_, &QSpinBox::valueChanged, this, &GeneratorStrip::readWidgets);
    connect(more, &QPushButton::clicked, this, &GeneratorStrip::moreRequested);
    showSettings();
}

void GeneratorStrip::setLevelControl(bool on)
{
    amplitude_->setEnabled(on);
    offset_->setEnabled(on);
}

void GeneratorStrip::setSettings(const starosc::GeneratorSettings& settings)
{
    settings_ = starosc::constrained(settings);
    showSettings();
}

void GeneratorStrip::showSettings()
{
    const bool was = std::exchange(updating_, true);
    const starosc::GeneratorSettings& g = settings_;
    generator_ui::showOutput(output_, g.output);
    waveform_->setCurrentIndex(static_cast<int>(g.waveform));
    frequency_->setMaximum(starosc::generator_limits(g.waveform).max_hz);
    frequency_->setValue(g.frequency_hz);
    frequency_->setEnabled(!g.sweep.enabled);
    frequency_->setStatusTip(g.sweep.enabled ? tr("A sweep is set on the Generator tab") : QString());
    amplitude_->setValue(g.amplitude_vpp);
    offset_->setValue(g.offset_v);
    duty_->setValue(g.duty_percent);
    duty_->setEnabled(g.waveform == starosc::Waveform::Square);
    sweep_->setText(g.sweep.enabled ? tr("Sweep on") : QString());
    updating_ = was;
}

void GeneratorStrip::readWidgets()
{
    if (updating_)
        return;
    starosc::GeneratorSettings& g = settings_;
    g.output = output_->isChecked();
    g.waveform = static_cast<starosc::Waveform>(std::max(0, waveform_->currentIndex()));
    g.frequency_hz = frequency_->value();
    g.amplitude_vpp = amplitude_->value();
    g.offset_v = offset_->value();
    g.duty_percent = duty_->value();
    settings_ = starosc::constrained(g);
    showSettings();
    emit settingsChanged(settings_);
}
