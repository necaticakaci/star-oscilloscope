// SPDX-License-Identifier: GPL-3.0-or-later
// Finds the gain of every stage with the generator on both inputs and, for the absolute value, a multimeter
// (starosc/gain_transfer.hpp). The measurements themselves run elsewhere: the wizard asks for them with its signals and
// gets the results through its slots.
#pragma once

#include <array>

#include <QWizard>

#include "starosc/calibration.hpp"
#include "starosc/gain_transfer.hpp"

class GainWizard : public QWizard {
    Q_OBJECT

public:
    enum PageId { IntroPageId, ConnectPageId, ReferencePlusId, ReferenceMinusId, TransferPageId, CheckPlusId, CheckMinusId, ResultPageId };

    // A level as it was read: by the two channels and by the multimeter.
    struct LevelReading {
        std::array<double, starosc::kChannels> read{};
        double meter = 0;
        bool have = false;
    };

    explicit GainWizard(const starosc::UserCalibration& current, QWidget* parent = nullptr);

    bool withMultimeter() const;
    const starosc::UserCalibration& current() const { return current_; }
    // The calibration with the new gains, once the last page is reached.
    const starosc::UserCalibration& result() const { return result_; }
    const starosc::GainTransfer& transfer() const { return transfer_; }
    LevelReading& reading(int pageId);

    // What the check against the multimeter says, for each channel, as a fraction; false when it was skipped.
    bool checkError(std::array<double, starosc::kChannels>& error) const;
    void computeResult();

public slots:
    void benchChecked();
    void levelMeasured(double ch1, double ch2);
    void transferProgress(int done, int total);
    void transferred(const starosc::GainTransfer& result);
    void stepFailed(const QString& message);

signals:
    void checkBenchRequested();
    void levelRequested(int stage, double volts, double sine_vpp);
    void transferRequested();

private:
    starosc::UserCalibration current_;
    starosc::UserCalibration result_;
    starosc::GainTransfer transfer_;
    std::array<LevelReading, 4> readings_{};
};
