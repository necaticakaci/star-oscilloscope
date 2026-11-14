// SPDX-License-Identifier: GPL-3.0-or-later
// Runs acquisitions on its own thread and hands finished frames to the GUI.
#pragma once

#include <atomic>
#include <memory>
#include <optional>

#include <QElapsedTimer>
#include <QObject>
#include <QString>

class QTimer;

#include "starosc/engine.hpp"
#include "starosc/source.hpp"
#include "types.hpp"

class AcquisitionWorker : public QObject {
    Q_OBJECT

public:
    enum State { Stopped, Running, Waiting };
    Q_ENUM(State)
    // What a step acquires: an oscilloscope frame, or a spectrum (the Spectrum tab).
    enum class Mode { Scope, Spectrum };

    explicit AcquisitionWorker(QObject* parent = nullptr);
    ~AcquisitionWorker() override;

    // Frames go out at most this often; a screen shows no more, and faster only costs CPU time.
    static constexpr int kMinFrameIntervalMs = 16;
    // A software sweep sets a new frequency this often; long captures can delay a step.
    static constexpr int kSweepIntervalMs = 20;
    // Called by the receiver of frameReady or spectrumReady, from any thread. While two frames wait for it, acquisition pauses.
    void frameTaken() { --framesWaiting_; }

public slots:
    // Opens the device, or the simulator when `simulate` is set or no device is found.
    void open(bool simulate);
    // The model the simulator acts as (starosc::make_simulator); empty for the ISDS205X. Set before open().
    void setSimulatorModel(const QString& model) { simulatorModel_ = model; }
    void setSettings(const starosc::ScopeSettings& settings);
    void setSpectrumSettings(const starosc::SpectrumSettings& settings);
    void setMode(AcquisitionWorker::Mode mode);
    // Starts the spectrum average and peak hold over.
    void resetSpectrum();
    void run();
    void stop();
    void single();
    void forceTrigger();
    void setCalibration(const starosc::UserCalibration& calibration);
    // Measures the zero of every stage; acquisition continues afterwards.
    void measureZero();
    // Looks for the signals and sets the scope up to show them (starosc::autoset), then runs.
    void autoset();
    // Applies the generator settings and runs their sweep, if any.
    void setGenerator(const starosc::GeneratorSettings& settings);
    // Parks the generator output near 0 V and switches it off (Source::park_generator).
    void parkGenerator();
    // The gain wizard, step by step (starosc/gain_transfer.hpp). From begin to end acquisition stands, so that nothing
    // changes the inputs while a multimeter is read, and readings are in factory volts. End brings all back.
    // The simulator has a bench of its own for this, with the generator on both inputs.
    void beginGainWizard();
    void checkGainBench();
    void measureGainLevel(int stage, double volts, double sine_vpp);
    void transferGains();
    void endGainWizard();

signals:
    void sourceOpened(const QString& name, bool simulated, const QString& note);
    // Emitted after sourceOpened with the source's stages and identity.
    void factoryCalibration(const starosc::UserCalibration& calibration);
    void calibrationFailed(const QString& message);
    void zeroMeasured(const StageZeros& zero);
    void zeroFailed(const QString& message);
    // `summary` is a line for the status bar; with no signal found the settings are the old ones.
    void autosetFinished(const starosc::ScopeSettings& settings, const QString& summary);
    void autosetFailed(const QString& message);
    // Emitted after sourceOpened: the source's sample rates, ascending.
    void sampleRates(const QList<quint32>& rates);
    // Emitted after factoryCalibration: whether the source has a generator.
    void generatorAvailable(bool available, bool levels);
    void generatorFailed(const QString& message);
    void gainBenchOk();
    void gainLevelMeasured(double ch1, double ch2);
    void gainTransferProgress(int done, int total);
    void gainTransferred(const starosc::GainTransfer& result);
    void gainStepFailed(const QString& message);
    void frameReady(FramePtr frame);
    void spectrumReady(SpectrumFramePtr frame);
    void stateChanged(AcquisitionWorker::State state);
    void failed(const QString& message);

private:
    void scheduleStep(int delayMs = 0);
    void step();
    void setState(State state);
    void applyGenerator(const starosc::GeneratorSettings& settings);
    void sweepStep();
    starosc::Source& gainSource() const { return gainBench_ ? *gainBench_ : *source_; }

    std::unique_ptr<starosc::Source> source_;
    QString simulatorModel_;
    std::unique_ptr<starosc::Source> gainBench_;
    std::optional<starosc::UserCalibration> calibration_;
    bool runAfterGainWizard_ = false;
    std::unique_ptr<starosc::Engine> engine_;
    std::unique_ptr<starosc::SpectrumEngine> spectrum_;
    starosc::ScopeSettings settings_;
    starosc::SpectrumSettings spectrumSettings_;
    Mode mode_ = Mode::Scope;
    State state_ = Stopped;
    bool stepQueued_ = false;
    bool singleShot_ = false;
    bool force_ = false;
    std::atomic<int> framesWaiting_ = 0;
    starosc::GeneratorSettings generator_;
    QTimer* sweepTimer_ = nullptr;
    QElapsedTimer sweepClock_;
};
