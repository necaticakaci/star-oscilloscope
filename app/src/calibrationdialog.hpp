// SPDX-License-Identifier: GPL-3.0-or-later
// User calibration of the connected device: zero and gain per stage and channel. The device's model and serial number,
// and when its zero and gain were last calibrated, stand above the table.
#pragma once

#include <QDialog>
#include <QList>

#include "starosc/calibration.hpp"
#include "types.hpp"

class QLabel;
class QPushButton;
class QTableWidget;

class CalibrationDialog : public QDialog {
    Q_OBJECT

public:
    enum Column { StageColumn, Ch1Zero, Ch1Gain, Ch2Zero, Ch2Gain, ColumnCount };

    CalibrationDialog(const starosc::UserCalibration& factory, const starosc::UserCalibration& current,
                      QWidget* parent = nullptr);

    const starosc::UserCalibration& calibration() const { return cal_; }
    QTableWidget* table() const { return table_; }

    // Takes the gains of `calibration`, as the gain wizard found them.
    void adoptGains(const starosc::UserCalibration& calibration);
    // Without a generator there is no gain wizard.
    void setGainWizardAvailable(bool available);

    // Takes over the corrections of `file` when it belongs to this device; returns an error otherwise.
    QString adoptFile(const starosc::UserCalibration& file);

public slots:
    void zeroMeasured(const StageZeros& zero);
    void zeroFailed(const QString& message);

signals:
    void measureZeroRequested();
    void gainWizardRequested();

private:
    void fillTable();
    void updateStatus();
    void showStatus(const QString& text);
    void onCellChanged(int row, int column);
    void measureZero();
    void importFile();
    void exportFile();
    void resetZero();
    void resetGain();
    void setBusy(bool busy);

    starosc::UserCalibration factory_;
    starosc::UserCalibration cal_;
    QTableWidget* table_ = nullptr;
    QLabel* zeroTime_ = nullptr;
    QLabel* gainTime_ = nullptr;
    QLabel* status_ = nullptr;  // what is going on, or what an import left out; hidden when there is nothing to say
    QList<QPushButton*> buttons_;
    QPushButton* gainWizard_ = nullptr;
    bool filling_ = false;
};
