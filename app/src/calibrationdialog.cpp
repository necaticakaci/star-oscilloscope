// SPDX-License-Identifier: GPL-3.0-or-later
#include "calibrationdialog.hpp"

#include <cmath>

#include <QDateTime>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include "calibrationstore.hpp"

namespace {

QString now() { return QDateTime::currentDateTime().toString(Qt::ISODate); }

QString shownTime(const std::string& iso)
{
    if (iso.empty())
        return QObject::tr("never (default values)");
    const QDateTime t = QDateTime::fromString(QString::fromStdString(iso), Qt::ISODate);
    return t.isValid() ? t.toString("yyyy-MM-dd HH:mm") : QString::fromStdString(iso);
}

QString zeroProblemText(const starosc::ZeroProblem& p)
{
    using Kind = starosc::ZeroProblem::Kind;
    const auto mv = [](double v, int decimals) { return QString::number(v * 1000, 'f', decimals); };
    const QString min = mv(p.span.min_v, 0), max = mv(p.span.max_v, 0);
    switch (p.kind) {
    case Kind::None: return {};
    case Kind::Incomplete: return CalibrationDialog::tr("the measurement does not cover every stage");
    case Kind::Clipped:
        return CalibrationDialog::tr("CH%1 clips on the %2 … %3 mV stage").arg(p.channel + 1).arg(min, max);
    case Kind::Offset:
        return CalibrationDialog::tr("CH%1 reads %4 mV on the %2 … %3 mV stage")
            .arg(p.channel + 1)
            .arg(min, max, mv(p.value_v, 1));
    case Kind::Noise:
        return CalibrationDialog::tr("CH%1 shows %4 mV of noise or signal on the %2 … %3 mV stage")
            .arg(p.channel + 1)
            .arg(min, max, mv(p.value_v, 1));
    }
    return {};
}

bool isZero(int column) { return column == CalibrationDialog::Ch1Zero || column == CalibrationDialog::Ch2Zero; }
int channelOf(int column) { return column <= CalibrationDialog::Ch1Gain ? 0 : 1; }

}  // namespace

CalibrationDialog::CalibrationDialog(const starosc::UserCalibration& factory, const starosc::UserCalibration& current,
                                     QWidget* parent)
    : QDialog(parent), factory_(factory), cal_(current)
{
    setWindowTitle(tr("Calibration"));

    auto* device = new QGroupBox(tr("Device"), this);
    auto* form = new QFormLayout(device);
    const auto value = [&](const QString& name, const QString& text) {
        auto* label = new QLabel(text, device);
        label->setObjectName(name);
        label->setTextInteractionFlags(Qt::TextSelectableByMouse);
        return label;
    };
    form->addRow(tr("Model:"), value("model", QString::fromStdString(factory_.model)));
    form->addRow(tr("Serial number:"), value("serialNumber", QString::fromStdString(factory_.device_id)));
    zeroTime_ = value("zeroTime", {});
    gainTime_ = value("gainTime", {});
    form->addRow(tr("Zero calibrated:"), zeroTime_);
    form->addRow(tr("Gain calibrated:"), gainTime_);

    auto* corrections = new QGroupBox(tr("Corrections"), this);
    auto* note = new QLabel(tr("V = (V<sub>default</sub> − zero) / gain, per stage and channel. Kept for this device only."),
                            corrections);
    note->setWordWrap(true);

    table_ = new QTableWidget(static_cast<int>(cal_.stages.size()), ColumnCount, this);
    table_->setHorizontalHeaderLabels(
        {tr("Stage (CH1 span)"), tr("CH1 zero, mV"), tr("CH1 gain"), tr("CH2 zero, mV"), tr("CH2 gain")});
    table_->verticalHeader()->hide();
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    connect(table_, &QTableWidget::cellChanged, this, &CalibrationDialog::onCellChanged);

    status_ = new QLabel(corrections);
    status_->setObjectName("calibrationStatus");
    status_->setWordWrap(true);
    status_->hide();

    auto* inside = new QVBoxLayout(corrections);
    inside->addWidget(note);
    inside->addWidget(table_, 1);
    inside->addWidget(status_);

    auto* tools = new QHBoxLayout;
    const auto addButton = [&](const QString& text, void (CalibrationDialog::*slot)()) {
        auto* b = new QPushButton(text, this);
        connect(b, &QPushButton::clicked, this, slot);
        tools->addWidget(b);
        buttons_.append(b);
    };
    gainWizard_ = new QPushButton(tr("Gain &Wizard…"), this);
    gainWizard_->setObjectName("gainWizard");
    connect(gainWizard_, &QPushButton::clicked, this, &CalibrationDialog::gainWizardRequested);
    tools->addWidget(gainWizard_);
    buttons_.append(gainWizard_);
    addButton(tr("Measure &Zero…"), &CalibrationDialog::measureZero);
    tools->addSpacing(12);
    addButton(tr("Reset Z&ero"), &CalibrationDialog::resetZero);
    addButton(tr("Reset &Gain"), &CalibrationDialog::resetGain);
    tools->addStretch();
    addButton(tr("&Import…"), &CalibrationDialog::importFile);
    addButton(tr("E&xport…"), &CalibrationDialog::exportFile);

    auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(box, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    buttons_.append(box->button(QDialogButtonBox::Ok));

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(device);
    layout->addWidget(corrections, 1);
    layout->addLayout(tools);
    layout->addWidget(box);

    fillTable();
    resize(720, 520);
}

void CalibrationDialog::fillTable()
{
    filling_ = true;
    for (int row = 0; row < static_cast<int>(cal_.stages.size()); ++row) {
        const starosc::StageCalibration& s = cal_.stages[row];
        auto* stage = new QTableWidgetItem(tr("%1 … %2 mV")
                                               .arg(s.factory_span[0].min_v * 1000, 0, 'f', 0)
                                               .arg(s.factory_span[0].max_v * 1000, 0, 'f', 0));
        stage->setFlags(stage->flags() & ~Qt::ItemIsEditable);
        table_->setItem(row, StageColumn, stage);
        for (int ch = 0; ch < starosc::kChannels; ++ch) {
            const starosc::ChannelCorrection& c = s.channel[ch];
            const int zeroColumn = ch == 0 ? Ch1Zero : Ch2Zero;
            auto* zero = new QTableWidgetItem(QString::number(c.zero_v * 1000, 'f', 2));
            auto* gain = new QTableWidgetItem(QString::number(c.gain, 'f', 4));
            zero->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            gain->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            table_->setItem(row, zeroColumn, zero);
            table_->setItem(row, zeroColumn + 1, gain);
        }
    }
    filling_ = false;
    updateStatus();
}

void CalibrationDialog::updateStatus()
{
    zeroTime_->setText(shownTime(cal_.zero_time));
    gainTime_->setText(shownTime(cal_.gain_time));
    showStatus({});
}

void CalibrationDialog::showStatus(const QString& text)
{
    status_->setText(text);
    status_->setVisible(!text.isEmpty());
}

void CalibrationDialog::onCellChanged(int row, int column)
{
    if (filling_ || column == StageColumn)
        return;
    starosc::ChannelCorrection& c = cal_.stages[row].channel[channelOf(column)];
    bool ok = false;
    const double value = table_->item(row, column)->text().replace(',', '.').toDouble(&ok);
    if (ok && isZero(column) && std::abs(value) < 10'000) {
        c.zero_v = value / 1000;
        cal_.zero_time = now().toStdString();
    } else if (ok && !isZero(column) && value > 0.5 && value < 2) {
        c.gain = value;
        cal_.gain_time = now().toStdString();
    }
    fillTable();  // shows the accepted value, or restores the old one
}

void CalibrationDialog::measureZero()
{
    const auto answer = QMessageBox::information(
        this, tr("Measure Zero"),
        tr("Short each probe tip to its own ground clip and remove every signal from both inputs.\n\n"
           "Every gain stage is measured on both channels; this takes a few seconds."),
        QMessageBox::Ok | QMessageBox::Cancel);
    if (answer != QMessageBox::Ok)
        return;
    setBusy(true);
    showStatus(tr("Measuring the zero of every stage…"));
    emit measureZeroRequested();
}

void CalibrationDialog::zeroMeasured(const StageZeros& zero)
{
    setBusy(false);
    const starosc::ZeroProblem problem = starosc::check_zero(factory_, zero);
    if (!problem.ok()) {
        updateStatus();
        QMessageBox::warning(this, tr("Measure Zero"),
                             tr("The zero was not accepted: %1.\n\nAre both inputs shorted and free of signal?")
                                 .arg(zeroProblemText(problem)));
        return;
    }
    for (std::size_t i = 0; i < zero.size(); ++i)
        for (int ch = 0; ch < starosc::kChannels; ++ch)
            cal_.stages[i].channel[ch].zero_v = zero[i].mean_v[ch];
    cal_.zero_time = now().toStdString();
    fillTable();
}

void CalibrationDialog::zeroFailed(const QString& message)
{
    setBusy(false);
    updateStatus();
    QMessageBox::warning(this, tr("Measure Zero"), tr("The measurement failed: %1").arg(message));
}

QString CalibrationDialog::adoptFile(const starosc::UserCalibration& file)
{
    if (file.device_id != factory_.device_id)
        return tr("This file belongs to device %1, not %2. A device's calibration is never applied to another device.")
            .arg(QString::fromStdString(file.device_id), QString::fromStdString(factory_.device_id));
    starosc::UserCalibration cal = factory_;
    const int matched = cal.adopt(file);
    if (matched == 0)
        return tr("No stage of this file matches the device.");
    cal.zero_time = file.zero_time;
    cal.gain_time = file.gain_time;
    cal_ = cal;
    fillTable();
    if (matched < static_cast<int>(cal_.stages.size()))
        showStatus(tr("%1 of %2 stages taken from the file; the others keep the default values.")
                       .arg(matched)
                       .arg(cal_.stages.size()));
    return {};
}

void CalibrationDialog::importFile()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Import Calibration"), {}, tr("Calibration (*.json)"));
    if (path.isEmpty())
        return;
    QString error;
    const auto file = calibration_store::load(path, &error);
    if (file)
        error = adoptFile(*file);
    if (!error.isEmpty())
        QMessageBox::warning(this, tr("Import Calibration"), error);
}

void CalibrationDialog::exportFile()
{
    const QString path = QFileDialog::getSaveFileName(this, tr("Export Calibration"),
                                                      QString::fromStdString(cal_.device_id) + ".json",
                                                      tr("Calibration (*.json)"));
    QString error;
    if (!path.isEmpty() && !calibration_store::save(path, cal_, &error))
        QMessageBox::warning(this, tr("Export Calibration"), error);
}

void CalibrationDialog::resetZero()
{
    for (auto& s : cal_.stages)
        for (auto& c : s.channel)
            c.zero_v = 0;
    cal_.zero_time.clear();
    fillTable();
}

void CalibrationDialog::resetGain()
{
    for (auto& s : cal_.stages)
        for (auto& c : s.channel)
            c.gain = 1;
    cal_.gain_time.clear();
    fillTable();
}

void CalibrationDialog::adoptGains(const starosc::UserCalibration& calibration)
{
    for (std::size_t i = 0; i < cal_.stages.size() && i < calibration.stages.size(); ++i)
        for (std::size_t ch = 0; ch < starosc::kChannels; ++ch)
            cal_.stages[i].channel[ch].gain = calibration.stages[i].channel[ch].gain;
    cal_.gain_time = calibration.gain_time;
    fillTable();
}

void CalibrationDialog::setGainWizardAvailable(bool available) { gainWizard_->setVisible(available); }

void CalibrationDialog::setBusy(bool busy)
{
    table_->setEnabled(!busy);
    for (QPushButton* b : buttons_)
        b->setEnabled(!busy);
}
