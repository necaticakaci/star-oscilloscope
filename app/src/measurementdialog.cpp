// SPDX-License-Identifier: GPL-3.0-or-later
#include "measurementdialog.hpp"

#include <algorithm>
#include <cmath>

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>

#include "hoverhelp.hpp"

namespace {

// The time parameters come first in kParameters.
constexpr std::size_t kTimeParameters = 7;

QString help(starosc::Parameter p)
{
    using P = starosc::Parameter;
    switch (p) {
    case P::Frequency: return QObject::tr("From the rising edges at the middle threshold; needs two of them.");
    case P::Period: return QObject::tr("Mean time between rising edges at the middle threshold.");
    case P::RiseTime: return QObject::tr("From the low to the high threshold, mean over the rising edges.");
    case P::FallTime: return QObject::tr("From the high to the low threshold, mean over the falling edges.");
    case P::PositiveWidth: return QObject::tr("From a rising to the next falling edge at the middle threshold.");
    case P::NegativeWidth: return QObject::tr("From a falling to the next rising edge at the middle threshold.");
    case P::Duty: return QObject::tr("Share of the period above the middle threshold.");
    case P::Amplitude: return QObject::tr("Top minus base.");
    case P::Top: return QObject::tr("The flat upper level of a pulse; the maximum if there is none, as on a sine.");
    case P::Base: return QObject::tr("The flat lower level of a pulse; the minimum if there is none, as on a sine.");
    case P::AcRms: return QObject::tr("RMS without the DC part.");
    case P::Overshoot: return QObject::tr("How far the maximum goes above the top, in percent of the amplitude.");
    case P::Undershoot: return QObject::tr("How far the minimum goes below the base, in percent of the amplitude.");
    case P::Delay: return QObject::tr("Positive when CH2 is later. Pulses: between rising edges. Other periodic signals: from the phase of the fundamental.");
    case P::Phase: return QObject::tr("The delay as an angle of the period, positive when CH2 is later; needs the same frequency on both channels.");
    default: return {};
    }
}

QString name(starosc::Parameter p) { return parameterName(p); }

template <typename Boxes>
void setHelp(QWidget* label, const Boxes& boxes, const QString& text)
{
    label->setStatusTip(text);
    for (QCheckBox* box : boxes)
        box->setStatusTip(text);
}

}  // namespace

MeasurementDialog::MeasurementDialog(const MeasurementSelection& selection, const starosc::MeasureThresholds& thresholds,
                                     QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Select Measurements"));

    auto* left = new QVBoxLayout;
    left->addWidget(createTraceGroup(tr("Time"), 0, kTimeParameters));
    left->addWidget(createPairGroup());
    left->addWidget(createThresholdGroup());
    left->addStretch(1);
    auto* right = new QVBoxLayout;
    right->addWidget(createTraceGroup(tr("Voltage"), kTimeParameters, starosc::kParameters.size()));
    right->addStretch(1);
    auto* columns = new QHBoxLayout;
    columns->addLayout(left);
    columns->addLayout(right);

    auto* buttons =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::RestoreDefaults, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons->button(QDialogButtonBox::RestoreDefaults), &QPushButton::clicked, this,
            [this] { showChoice(MeasurementSelection::defaults(), {}); });

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(columns);
    layout->addWidget(hover_help::line(this, 2));  // the longest help takes two lines
    layout->addWidget(buttons);
    showChoice(selection, thresholds);
}

QGroupBox* MeasurementDialog::createTraceGroup(const QString& title, std::size_t first, std::size_t last)
{
    auto* box = new QGroupBox(title, this);
    auto* grid = new QGridLayout(box);
    grid->setColumnStretch(0, 1);
    grid->setVerticalSpacing(2);
    const std::array headers{tr("CH1"), tr("CH2"), tr("Math")};
    for (int t = 0; t < MeasurementSelection::kTraces; ++t)
        grid->addWidget(new QLabel(headers[static_cast<std::size_t>(t)], box), 0, t + 1, Qt::AlignCenter);
    for (std::size_t i = first; i < last; ++i) {
        const starosc::Parameter p = starosc::kParameters[i];
        const int row = static_cast<int>(i - first) + 1;
        auto* label = new QLabel(name(p), box);
        grid->addWidget(label, row, 0);
        TraceBoxes& boxes = traceBoxes_[p];
        for (int t = 0; t < MeasurementSelection::kTraces; ++t) {
            boxes[static_cast<std::size_t>(t)] = new QCheckBox(box);
            grid->addWidget(boxes[static_cast<std::size_t>(t)], row, t + 1, Qt::AlignCenter);
        }
        setHelp(label, boxes, help(p));
    }
    return box;
}

QGroupBox* MeasurementDialog::createPairGroup()
{
    auto* box = new QGroupBox(tr("Between channels (CH1 to CH2)"), this);
    auto* row = new QHBoxLayout(box);
    for (starosc::Parameter p : starosc::kPairParameters) {
        auto* check = new QCheckBox(name(p), box);
        check->setStatusTip(help(p));
        pairBoxes_[p] = check;
        row->addWidget(check);
    }
    row->addStretch(1);
    return box;
}

QGroupBox* MeasurementDialog::createThresholdGroup()
{
    auto* box = new QGroupBox(tr("Thresholds"), this);
    box->setStatusTip(tr("Reference levels of the time measurements, in percent of the distance from base to top."));
    const auto spin = [box] {
        auto* s = new QSpinBox(box);
        s->setRange(1, 99);
        s->setSuffix(tr(" %"));
        return s;
    };
    low_ = spin();
    mid_ = spin();
    high_ = spin();
    auto* row = new QHBoxLayout(box);
    for (const auto& [label, widget] : {std::pair{tr("Low"), low_}, std::pair{tr("Middle"), mid_}, std::pair{tr("High"), high_}}) {
        row->addWidget(new QLabel(label, box));
        row->addWidget(widget);
    }
    row->addStretch(1);
    for (QSpinBox* s : {low_, mid_, high_})
        connect(s, &QSpinBox::valueChanged, this, &MeasurementDialog::keepThresholdsInOrder);
    return box;
}

void MeasurementDialog::keepThresholdsInOrder()
{
    low_->setMaximum(mid_->value() - 1);
    mid_->setRange(low_->value() + 1, high_->value() - 1);
    high_->setMinimum(mid_->value() + 1);
}

void MeasurementDialog::showChoice(const MeasurementSelection& selection, const starosc::MeasureThresholds& thresholds)
{
    for (auto& [p, boxes] : traceBoxes_)
        for (std::size_t t = 0; t < boxes.size(); ++t)
            boxes[t]->setChecked(std::ranges::find(selection.trace[t], p) != selection.trace[t].end());
    for (auto& [p, box] : pairBoxes_)
        box->setChecked(std::ranges::find(selection.pair, p) != selection.pair.end());

    const starosc::MeasureThresholds t = starosc::constrained(thresholds);
    // The ranges follow the values, so they open up first for values on the far side of the old ones.
    const std::array<std::pair<QSpinBox*, double>, 3> levels{{{low_, t.low}, {mid_, t.mid}, {high_, t.high}}};
    for (const auto& [spin, level] : levels) {
        const QSignalBlocker quiet(spin);
        spin->setRange(1, 99);
        spin->setValue(static_cast<int>(std::lround(level)));
    }
    keepThresholdsInOrder();
}

MeasurementSelection MeasurementDialog::selection() const
{
    MeasurementSelection out;
    for (starosc::Parameter p : starosc::kParameters) {
        const TraceBoxes& boxes = traceBoxes_.at(p);
        for (std::size_t t = 0; t < boxes.size(); ++t)
            if (boxes[t]->isChecked())
                out.trace[t].push_back(p);
    }
    for (starosc::Parameter p : starosc::kPairParameters)
        if (pairBoxes_.at(p)->isChecked())
            out.pair.push_back(p);
    return out;
}

starosc::MeasureThresholds MeasurementDialog::thresholds() const
{
    return starosc::constrained(starosc::MeasureThresholds{static_cast<double>(low_->value()),
                                                           static_cast<double>(mid_->value()),
                                                           static_cast<double>(high_->value())});
}

QCheckBox* MeasurementDialog::checkBox(starosc::Parameter parameter, int trace) const
{
    if (trace < 0)
        return pairBoxes_.at(parameter);
    return traceBoxes_.at(parameter)[static_cast<std::size_t>(trace)];
}
