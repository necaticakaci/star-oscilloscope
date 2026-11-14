// SPDX-License-Identifier: GPL-3.0-or-later
// Main window: instrument tabs, display, docked controls and measurements.
#pragma once

#include <array>

#include <QElapsedTimer>
#include <QMainWindow>
#include <QThread>

#include "acquisitionworker.hpp"
#include "starosc/reference.hpp"
#include "starosc/settings.hpp"
#include "types.hpp"

class CalibrationDialog;
class ControlPanel;
class FrontPanel;
class GeneratorPanel;
class GeneratorStrip;
class MeasurementTable;
class OverviewStrip;
class QAction;
class QActionGroup;
class QDockWidget;
class QLabel;
class QMenu;
class QScrollArea;
class QStackedWidget;
class QTabWidget;
class ScopePanel;
class ScopeView;
class SpectrumPanel;
class SpectrumTable;
class SpectrumView;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    // `simulatorModel`: the model the simulator acts as, for what depends on it (the generator); empty for the
    // ISDS205X.
    explicit MainWindow(bool simulate, const QString& simulatorModel = {}, QWidget* parent = nullptr);
    ~MainWindow() override;

    // Saves a picture of the window after `frames` frames have been shown, then quits.
    void screenshotAfterFrames(const QString& path, int frames);
    // Shows the "oscilloscope", "spectrum" or "generator" tab (the last for pictures: it opens before the source says
    // whether it has a generator).
    bool showTab(const QString& name);

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void createActions();
    void createMenus();
    void createToolBar();
    void createCentralWidget();
    void createDocks();
    std::array<ScopePanel*, 2> scopePanels() const;
    // The control page for the tab on show and the panel the user chose.
    void showControlPage();
    // As wide as the page on show needs, so the display gets the rest.
    void fitControlsDock();
    void resetLayout();  // docks back in their default places, e.g. after one was torn off by accident
    void createStatusBar();
    void connectDisplay();
    void startWorker(bool simulate, const QString& simulatorModel);

    // Single entry point for setting changes from the controls, the display or the menus.
    // `origin` is the panel the change came from, which shows it already.
    void applySettings(const starosc::ScopeSettings& settings, ScopePanel* origin = nullptr);
    void setCursors(const starosc::CursorSettings& cursors);
    void setZoom(const starosc::ZoomSettings& zoom);
    // After a trace colour or the display theme changed.
    void refreshDisplayStyle();
    void chooseTraceColors();
    // Sample rate of the record on the screen; 0 before the first frame.
    double recordRate() const;
    void selectMeasurements();

    void onFrame(FramePtr frame);
    void onSpectrum(SpectrumFramePtr frame);
    // Counts a shown frame for the frame rate and --screenshot.
    void frameShown();
    // Single entry point for spectrum setting changes from its panel and display.
    void applySpectrum(const starosc::SpectrumSettings& settings, bool updatePanel);
    void onTabChanged();
    bool spectrumShown() const;
    void onState(AcquisitionWorker::State state);
    void onSourceOpened(const QString& name, bool simulated, const QString& note);
    void onFactoryCalibration(const starosc::UserCalibration& factory);
    void onFailed(const QString& message);
    void openCalibration();
    void runGainWizard(CalibrationDialog& dialog);
    void applyCalibration(const starosc::UserCalibration& calibration);
    // Single entry point for generator changes from the Generator tab and the strip.
    void applyGenerator(const starosc::GeneratorSettings& settings);
    void onGeneratorAvailable(bool available, bool levels);
    // Reference waveforms R1 and R2: `source` 0 or 1 for a channel, starosc::kMathSource for math.
    void storeReference(int ref, int source);
    void saveReference(int ref);
    void loadReference(int ref);
    void clearReference(int ref);
    void showReferences();
    void loadKeptReferences();
    void keepReferences() const;
    void updateCursorInfo();
    void toggleRun();
    void exportCsv();
    void exportSpectrumCsv();
    void saveScreenshot();
    void about();

    QThread thread_;
    AcquisitionWorker* worker_ = nullptr;
    AcquisitionWorker::State state_ = AcquisitionWorker::Stopped;
    starosc::ScopeSettings settings_;
    starosc::CursorSettings cursors_;
    starosc::UserCalibration factoryCalibration_;  // of the open source
    starosc::UserCalibration calibration_;         // in use
    starosc::GeneratorSettings generator_;
    starosc::SpectrumSettings spectrum_;
    SpectrumFramePtr lastSpectrum_;
    bool parkGenerator_ = true;  // park the output at 0 V when connecting and closing
    bool generatorAvailable_ = false;
    bool generatorLevels_ = false;  // amplitude and offset can be set, and the output parked
    FramePtr lastFrame_;
    std::array<starosc::Reference, starosc::kReferences> references_;

    ScopeView* view_ = nullptr;
    OverviewStrip* overview_ = nullptr;
    ControlPanel* controls_ = nullptr;
    FrontPanel* frontPanel_ = nullptr;
    QWidget* formPage_ = nullptr;
    QWidget* frontPage_ = nullptr;
    QWidget* spectrumPage_ = nullptr;
    MeasurementTable* measurements_ = nullptr;
    QDockWidget* controlsDock_ = nullptr;
    QDockWidget* measurementsDock_ = nullptr;
    QDockWidget* generatorDock_ = nullptr;
    QTabWidget* tabs_ = nullptr;
    GeneratorPanel* generatorPanel_ = nullptr;
    QScrollArea* generatorPage_ = nullptr;
    GeneratorStrip* generatorStrip_ = nullptr;
    QWidget* spectrumTab_ = nullptr;
    SpectrumView* spectrumView_ = nullptr;
    QLabel* spectrumInfo_ = nullptr;
    SpectrumPanel* spectrumPanel_ = nullptr;
    SpectrumTable* spectrumTable_ = nullptr;
    QStackedWidget* controlsStack_ = nullptr;
    QStackedWidget* measurementsStack_ = nullptr;
    QLabel* cursorInfo_ = nullptr;
    QLabel* stateLabel_ = nullptr;
    QLabel* sourceLabel_ = nullptr;
    QLabel* calibrationLabel_ = nullptr;
    QLabel* generatorLabel_ = nullptr;
    QLabel* samplingLabel_ = nullptr;
    QLabel* rateLabel_ = nullptr;

    QAction* runAction_ = nullptr;
    QAction* singleAction_ = nullptr;
    QAction* forceAction_ = nullptr;
    QAction* autosetAction_ = nullptr;
    QAction* simulatorAction_ = nullptr;
    QAction* reconnectAction_ = nullptr;
    QAction* calibrationAction_ = nullptr;
    QAction* exportAction_ = nullptr;
    QAction* screenshotAction_ = nullptr;
    QAction* quitAction_ = nullptr;
    QAction* resetStatsAction_ = nullptr;
    QAction* classicPaletteAction_ = nullptr;
    QAction* resetLayoutAction_ = nullptr;
    QAction* formPanelAction_ = nullptr;
    QAction* frontPanelAction_ = nullptr;
    QAction* interpolationAction_ = nullptr;
    QAction* zoomOffAction_ = nullptr;
    QAction* xyAction_ = nullptr;
    QMenu* languageMenu_ = nullptr;
    QAction* traceColorsAction_ = nullptr;
    QAction* printThemeAction_ = nullptr;
    QAction* dotsAction_ = nullptr;
    QAction* selectMeasurementsAction_ = nullptr;
    QAction* tableColumnsAction_ = nullptr;
    QAction* aboutAction_ = nullptr;
    std::array<QAction*, 3> cursorModeActions_{};
    std::array<QAction*, 2> cursorSourceActions_{};
    QMenu* cursorMenu_ = nullptr;

    QElapsedTimer frameClock_;
    double framesPerSecond_ = 0;
    QString screenshotPath_;
    int screenshotFrames_ = 0;
    int framesShown_ = 0;
};
