// SPDX-License-Identifier: GPL-3.0-or-later
#include "acquisitionworker.hpp"

#include <algorithm>
#include <exception>
#include <utility>

#include <QStringList>
#include <QTimer>

#include "starosc/autoset.hpp"
#include "starosc/scale.hpp"

AcquisitionWorker::AcquisitionWorker(QObject* parent) : QObject(parent) { qRegisterMetaType<starosc::GainTransfer>(); }

AcquisitionWorker::~AcquisitionWorker() = default;

void AcquisitionWorker::open(bool simulate)
{
    setState(Stopped);
    if (sweepTimer_)
        sweepTimer_->stop();
    engine_.reset();
    spectrum_.reset();
    source_.reset();
    gainBench_.reset();
    calibration_.reset();

    QString note;
    if (!simulate) {
        try {
            source_ = starosc::open_device();
        } catch (const std::exception& e) {
            note = QString::fromStdString(e.what());
        }
    }
    if (!source_)
        source_ = simulatorModel_.isEmpty() ? starosc::make_simulator()
                                            : starosc::make_simulator(simulatorModel_.toStdString());
    engine_ = std::make_unique<starosc::Engine>(*source_);
    spectrum_ = std::make_unique<starosc::SpectrumEngine>(*source_);
    emit sourceOpened(QString::fromStdString(source_->name()), source_->simulated(), note);
    QList<quint32> rates;
    for (std::uint32_t r : source_->sample_rates())
        rates.push_back(r);
    emit sampleRates(rates);
    // The generator first: the factory calibration enables the Calibration window, whose gain wizard depends on it.
    emit generatorAvailable(source_->has_generator(), source_->has_generator_levels());
    emit factoryCalibration(source_->factory_calibration());
}

void AcquisitionWorker::setGenerator(const starosc::GeneratorSettings& settings)
{
    generator_ = settings;
    const bool sweeping = settings.output && settings.sweep.enabled;
    if (sweeping) {
        if (!sweepTimer_) {
            sweepTimer_ = new QTimer(this);
            sweepTimer_->setInterval(kSweepIntervalMs);
            connect(sweepTimer_, &QTimer::timeout, this, &AcquisitionWorker::sweepStep);
        }
        sweepClock_.start();
        sweepTimer_->start();
        sweepStep();
        return;
    }
    if (sweepTimer_)
        sweepTimer_->stop();
    applyGenerator(settings);
}

void AcquisitionWorker::parkGenerator()
{
    if (sweepTimer_)
        sweepTimer_->stop();
    generator_.output = false;
    if (!source_)
        return;
    try {
        source_->park_generator();
    } catch (const std::exception& e) {
        emit generatorFailed(QString::fromStdString(e.what()));
    }
}

void AcquisitionWorker::sweepStep()
{
    starosc::GeneratorSettings g = generator_;
    g.frequency_hz = starosc::sweep_frequency(g.sweep, sweepClock_.elapsed() / 1000.0);
    applyGenerator(g);
}

void AcquisitionWorker::applyGenerator(const starosc::GeneratorSettings& settings)
{
    if (!source_)
        return;
    try {
        source_->set_generator(settings);
    } catch (const std::exception& e) {
        if (sweepTimer_)
            sweepTimer_->stop();
        emit generatorFailed(QString::fromStdString(e.what()));
    }
}

void AcquisitionWorker::setCalibration(const starosc::UserCalibration& calibration)
{
    calibration_ = calibration;
    if (!source_)
        return;
    try {
        source_->set_calibration(calibration);
    } catch (const std::exception& e) {
        emit calibrationFailed(QString::fromStdString(e.what()));
    }
}

void AcquisitionWorker::measureZero()
{
    if (!source_)
        return;
    try {
        emit zeroMeasured(source_->measure_zero());
    } catch (const std::exception& e) {
        emit zeroFailed(QString::fromStdString(e.what()));
    }
}

void AcquisitionWorker::beginGainWizard()
{
    if (!source_)
        return;
    runAfterGainWizard_ = state_ != Stopped;
    stop();
    if (sweepTimer_)
        sweepTimer_->stop();
    try {
        if (source_->simulated()) {
            // Readings and an output that behave as on the device measured in docs/measurements.md §3 and §5.
            starosc::SimulatedBench bench;
            bench.stage_gain = {{0.980, 0.980}, {0.930, 0.938}, {0.934, 0.942}, {0.931, 0.940}, {0.934, 0.936}, {0.940, 0.944}, {0.934, 0.938}};
            bench.output_gain_per_fine_input = 1.10;
            bench.output_shift_per_fine_input = -0.065;
            gainBench_ = starosc::make_simulator(bench);
        } else {
            source_->set_calibration(source_->factory_calibration());
        }
    } catch (const std::exception& e) {
        emit gainStepFailed(QString::fromStdString(e.what()));
    }
}

void AcquisitionWorker::checkGainBench()
{
    if (!source_)
        return;
    try {
        starosc::check_generator_on_inputs(gainSource());
        emit gainBenchOk();
    } catch (const std::exception& e) {
        emit gainStepFailed(QString::fromStdString(e.what()));
    }
}

void AcquisitionWorker::measureGainLevel(int stage, double volts, double sine_vpp)
{
    if (!source_)
        return;
    try {
        const auto read = starosc::measure_level(gainSource(), stage, volts, sine_vpp);
        emit gainLevelMeasured(read[0], read[1]);
    } catch (const std::exception& e) {
        emit gainStepFailed(QString::fromStdString(e.what()));
    }
}

void AcquisitionWorker::transferGains()
{
    if (!source_)
        return;
    try {
        emit gainTransferred(starosc::transfer_gains(gainSource(), [this](int done, int total) { emit gainTransferProgress(done, total); }));
    } catch (const std::exception& e) {
        emit gainStepFailed(QString::fromStdString(e.what()));
    }
}

void AcquisitionWorker::endGainWizard()
{
    if (!source_)
        return;
    gainBench_.reset();
    try {
        source_->set_calibration(calibration_ ? *calibration_ : source_->factory_calibration());
    } catch (const std::exception& e) {
        emit calibrationFailed(QString::fromStdString(e.what()));
    }
    if (generator_.output)
        setGenerator(generator_);
    else
        parkGenerator();
    if (runAfterGainWizard_)
        run();
}

void AcquisitionWorker::autoset()
{
    if (!source_)
        return;
    try {
        const starosc::AutosetResult r = starosc::autoset(*source_, settings_);
        settings_ = r.settings;  // the next frame uses them before the GUI sends them back
        QStringList channels;
        for (int ch = 0; ch < starosc::kChannels; ++ch)
            if (r.found[ch])
                channels << tr("CH%1").arg(ch + 1);
        QString summary;
        if (channels.isEmpty())
            summary = tr("Autoset: no signal found, settings unchanged");
        else if (r.frequency_hz > 0)
            summary = tr("Autoset: %1; trigger on CH%2 at %3")
                          .arg(channels.join(", "))
                          .arg(r.settings.trigger.source + 1)
                          .arg(QString::fromStdString(starosc::format_si(r.frequency_hz, "Hz")));
        else
            summary = tr("Autoset: %1; no periodic signal, timebase kept").arg(channels.join(", "));
        emit autosetFinished(r.settings, summary);
    } catch (const std::exception& e) {
        emit autosetFailed(QString::fromStdString(e.what()));
        return;
    }
    run();
}

void AcquisitionWorker::setSettings(const starosc::ScopeSettings& settings) { settings_ = settings; }

void AcquisitionWorker::setSpectrumSettings(const starosc::SpectrumSettings& settings) { spectrumSettings_ = settings; }

void AcquisitionWorker::setMode(Mode mode)
{
    mode_ = mode;
    if (spectrum_)
        spectrum_->reset();
}

void AcquisitionWorker::resetSpectrum()
{
    if (spectrum_)
        spectrum_->reset();
}

void AcquisitionWorker::run()
{
    singleShot_ = false;
    if (state_ == Stopped)
        setState(Running);
    scheduleStep();
}

void AcquisitionWorker::stop()
{
    singleShot_ = false;
    setState(Stopped);
}

void AcquisitionWorker::single()
{
    singleShot_ = true;
    setState(Waiting);
    scheduleStep();
}

void AcquisitionWorker::forceTrigger()
{
    force_ = true;
    if (state_ == Stopped) {
        singleShot_ = true;
        setState(Waiting);
    }
    scheduleStep();
}

void AcquisitionWorker::scheduleStep(int delayMs)
{
    if (stepQueued_)
        return;
    stepQueued_ = true;
    QTimer::singleShot(delayMs, this, &AcquisitionWorker::step);
}

void AcquisitionWorker::step()
{
    stepQueued_ = false;
    if (state_ == Stopped || !engine_)
        return;
    if (framesWaiting_ >= 2) {  // the GUI is behind; do not pile frames up in its queue
        scheduleStep(kMinFrameIntervalMs);
        return;
    }
    QElapsedTimer clock;
    clock.start();
    int delay = 0;

    if (mode_ == Mode::Spectrum) {
        force_ = false;
        try {
            auto frame = std::make_shared<const starosc::SpectrumFrame>(spectrum_->acquire(settings_, spectrumSettings_));
            ++framesWaiting_;
            emit spectrumReady(frame);
        } catch (const std::exception& e) {
            setState(Stopped);
            emit failed(QString::fromStdString(e.what()));
            return;
        }
        if (singleShot_) {
            singleShot_ = false;
            setState(Stopped);
            return;
        }
        setState(Running);
        scheduleStep(std::max(0, kMinFrameIntervalMs - static_cast<int>(clock.elapsed())));
        return;
    }


    starosc::ScopeSettings settings = settings_;
    const bool once = singleShot_ || settings.trigger.mode == starosc::TriggerMode::Single;
    if (singleShot_)
        settings.trigger.mode = starosc::TriggerMode::Single;
    try {
        auto frame = engine_->acquire(settings, std::exchange(force_, false));
        if (frame) {
            ++framesWaiting_;
            emit frameReady(std::make_shared<const starosc::Frame>(std::move(*frame)));
            if (once) {
                singleShot_ = false;
                setState(Stopped);
                return;
            }
            setState(Running);
            // Only shown frames are paced: while no trigger comes, captures follow each other at full speed.
            delay = std::max(0, kMinFrameIntervalMs - static_cast<int>(clock.elapsed()));
        } else {
            setState(Waiting);
        }
    } catch (const std::exception& e) {
        setState(Stopped);
        emit failed(QString::fromStdString(e.what()));
        return;
    }
    scheduleStep(delay);
}

void AcquisitionWorker::setState(State state)
{
    if (state_ == state)
        return;
    state_ = state;
    emit stateChanged(state);
}
