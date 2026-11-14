// SPDX-License-Identifier: GPL-3.0-or-later
#include "mainwindow.hpp"

#include <array>
#include <cmath>
#include <tuple>

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QDockWidget>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QScrollArea>
#include <QSaveFile>
#include <QScrollBar>
#include <QSettings>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTabWidget>
#include <QTextStream>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

#include "appicon.hpp"
#include "language.hpp"
#include "calibrationdialog.hpp"
#include "calibrationstore.hpp"
#include "controlpanel.hpp"
#include "displaystyle.hpp"
#include "frontpanel.hpp"
#include "gainwizard.hpp"
#include "generatorpanel.hpp"
#include "generatorstrip.hpp"
#include "hoverhelp.hpp"
#include "icons.hpp"
#include "measurementdialog.hpp"
#include "measurementtable.hpp"
#include "overviewstrip.hpp"
#include "scopeview.hpp"
#include "settingsstore.hpp"
#include "spectrumpanel.hpp"
#include "spectrumtable.hpp"
#include "spectrumview.hpp"
#include "starosc/cursor.hpp"
#include "starosc/math.hpp"
#include "starosc/scale.hpp"
#include "starosc/zoom.hpp"
#include "theme.hpp"
#include "tracecolorsdialog.hpp"

namespace {

// Bump when the dock layout changes so that old saved layouts are ignored.
constexpr int kLayoutVersion = 3;


starosc::ScopeSettings defaultSettings()
{
    starosc::ScopeSettings s;
    s.channel[0] = {true, 1.0, 1.0, starosc::Coupling::DC, 1.0};
    s.channel[1] = {true, 0.5, -2.0, starosc::Coupling::DC, 1.0};
    s.timebase = {500e-6, starosc::MemoryDepth::Small};
    s.trigger.level_v = 1.0;
    return s;
}

QString si(double value, const char* unit, int digits = 4, bool trim = false)
{
    return QString::fromStdString(starosc::format_si(value, unit, digits, trim));
}

// A cursor value right-aligned in a field as wide as its widest form ("-123.4 m" and the unit), so the readouts after
// it stay in place while it changes. For the cursor line, which is rich text in a fixed-width font.
QString cursorField(double value, const char* unit)
{
    const QString text = si(value, unit);
    const qsizetype width = 8 + QString::fromUtf8(unit).size();
    return QString("&nbsp;").repeated(std::max<qsizetype>(0, width - text.size())) + text.toHtmlEscaped();
}

QLabel* sunkenLabel(QWidget* parent)
{
    auto* label = new QLabel(parent);
    label->setFrameStyle(QFrame::Panel | QFrame::Sunken);
    label->setTextFormat(Qt::RichText);
    return label;
}

QFrame* sunkenFrame(QWidget* content, QWidget* parent)
{
    auto* frame = new QFrame(parent);
    frame->setFrameStyle(QFrame::Panel | QFrame::Sunken);
    auto* layout = new QVBoxLayout(frame);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->addWidget(content);
    return frame;
}



QString referencesDir() { return QFileInfo(QSettings().fileName()).absolutePath() + "/references"; }


}  // namespace

MainWindow::MainWindow(bool simulate, const QString& simulatorModel, QWidget* parent)
    : QMainWindow(parent), settings_(defaultSettings())
{
    setWindowTitle(tr("Star Oscilloscope"));
    QSettings saved;
    settings_store::load(saved, settings_, cursors_);
    settings_store::loadGenerator(saved, generator_, parkGenerator_);
    settings_store::loadSpectrum(saved, spectrum_);
    settings_.zoom = starosc::constrained(settings_.zoom, settings_.timebase, 0);
    display_style::load(saved);

    hover_help::install();  // help in the status bar, no tooltips
    createActions();
    frontPanelAction_->setChecked(saved.value("window/controlPanel").toString() == "front");
    createCentralWidget();
    createDocks();
    createMenus();
    createToolBar();
    createStatusBar();
    connectDisplay();

    if (!restoreGeometry(saved.value("window/geometry").toByteArray()))
        resize(1180, 760);
    const bool layoutRestored = restoreState(saved.value("window/state").toByteArray(), kLayoutVersion);

    MeasurementSelection selection = MeasurementSelection::defaults();
    settings_store::loadMeasurements(saved, selection);
    measurements_->setSelection(selection);
    if (saved.contains("measure/columns"))
        measurements_->setColumns(saved.value("measure/columns").toStringList());
    zoomOffAction_->setEnabled(settings_.zoom.on());
    {
        const QSignalBlocker block(xyAction_);
        xyAction_->setChecked(settings_.display == starosc::DisplayMode::XY);
    }
    controls_->setSettings(settings_);
    frontPanel_->setSettings(settings_);
    spectrumPanel_->setScopeSettings(settings_);
    spectrumPanel_->setSettings(spectrum_);
    spectrumView_->setSettings(spectrum_);
    view_->setSettings(settings_);
    overview_->setSettings(settings_);
    setCursors(cursors_);
    interpolationAction_->setChecked(saved.value("display/interpolation", true).toBool());
    view_->setInterpolation(interpolationAction_->isChecked());
    dotsAction_->setChecked(saved.value("display/dots", false).toBool());
    view_->setDots(dotsAction_->isChecked());
    printThemeAction_->setChecked(display_style::printTheme());
    loadKeptReferences();
    generatorPanel_->setSettings(generator_);
    generatorPanel_->setParkOutput(parkGenerator_);
    generatorStrip_->setSettings(generator_);
    startWorker(simulate, simulatorModel);
    showTab(saved.value("window/tab").toString());
    showControlPage();
    if (!layoutRestored)
        fitControlsDock();
}

MainWindow::~MainWindow()
{
    if (generatorAvailable_ && generatorLevels_ && parkGenerator_)
        QMetaObject::invokeMethod(worker_, &AcquisitionWorker::parkGenerator, Qt::BlockingQueuedConnection);
    QMetaObject::invokeMethod(worker_, &AcquisitionWorker::stop, Qt::QueuedConnection);
    thread_.quit();
    thread_.wait();
}

void MainWindow::startWorker(bool simulate, const QString& simulatorModel)
{
    worker_ = new AcquisitionWorker;
    worker_->setSimulatorModel(simulatorModel);
    worker_->moveToThread(&thread_);
    connect(&thread_, &QThread::finished, worker_, &QObject::deleteLater);
    connect(worker_, &AcquisitionWorker::frameReady, this, &MainWindow::onFrame);
    connect(worker_, &AcquisitionWorker::spectrumReady, this, &MainWindow::onSpectrum);
    connect(worker_, &AcquisitionWorker::sampleRates, this, [this](const QList<quint32>& rates) {
        spectrumPanel_->setSampleRates(std::vector<std::uint32_t>(rates.begin(), rates.end()));
    });
    connect(worker_, &AcquisitionWorker::stateChanged, this, &MainWindow::onState);
    connect(worker_, &AcquisitionWorker::sourceOpened, this, &MainWindow::onSourceOpened);
    connect(worker_, &AcquisitionWorker::factoryCalibration, this, &MainWindow::onFactoryCalibration);
    connect(worker_, &AcquisitionWorker::calibrationFailed, this,
            [this](const QString& message) { statusBar()->showMessage(tr("Calibration not applied: %1").arg(message)); });
    connect(worker_, &AcquisitionWorker::failed, this, &MainWindow::onFailed);
    connect(worker_, &AcquisitionWorker::autosetFinished, this,
            [this](const starosc::ScopeSettings& settings, const QString& summary) {
                applySettings(settings);
                statusBar()->showMessage(summary, 8000);
                autosetAction_->setEnabled(true);
            });
    connect(worker_, &AcquisitionWorker::autosetFailed, this, [this](const QString& message) {
        statusBar()->showMessage(tr("Autoset failed: %1").arg(message), 8000);
        autosetAction_->setEnabled(true);
    });
    connect(worker_, &AcquisitionWorker::generatorAvailable, this, &MainWindow::onGeneratorAvailable);
    connect(worker_, &AcquisitionWorker::generatorFailed, this,
            [this](const QString& message) { statusBar()->showMessage(tr("Generator: %1").arg(message), 8000); });
    thread_.start();

    sourceLabel_->setText(tr("Connecting…"));
    QMetaObject::invokeMethod(worker_, [w = worker_, simulate] { w->open(simulate); }, Qt::QueuedConnection);
}

void MainWindow::createActions()
{
    runAction_ = new QAction(icons::run(), tr("&Run"), this);
    runAction_->setShortcut(Qt::Key_F5);
    runAction_->setStatusTip(tr("Start or stop continuous acquisition"));
    connect(runAction_, &QAction::triggered, this, &MainWindow::toggleRun);

    singleAction_ = new QAction(icons::single(), tr("Sin&gle"), this);
    singleAction_->setShortcut(Qt::Key_F6);
    singleAction_->setStatusTip(tr("Acquire one triggered frame, then stop"));
    connect(singleAction_, &QAction::triggered, this,
            [this] { QMetaObject::invokeMethod(worker_, &AcquisitionWorker::single, Qt::QueuedConnection); });

    forceAction_ = new QAction(icons::forceTrigger(), tr("&Force Trigger"), this);
    forceAction_->setShortcut(Qt::Key_F7);
    forceAction_->setStatusTip(tr("Show the next frame without waiting for a trigger"));
    connect(forceAction_, &QAction::triggered, this,
            [this] { QMetaObject::invokeMethod(worker_, &AcquisitionWorker::forceTrigger, Qt::QueuedConnection); });

    autosetAction_ = new QAction(icons::autoset(), tr("&Autoset"), this);
    autosetAction_->setObjectName("autosetAction");
    autosetAction_->setShortcut(Qt::Key_F8);
    autosetAction_->setStatusTip(tr("Find the signals and set V/div, position, timebase and trigger to show them"));
    connect(autosetAction_, &QAction::triggered, this, [this] {
        autosetAction_->setEnabled(false);
        statusBar()->showMessage(tr("Autoset…"));
        QMetaObject::invokeMethod(worker_, &AcquisitionWorker::autoset, Qt::QueuedConnection);
    });

    simulatorAction_ = new QAction(tr("Use S&imulator"), this);
    simulatorAction_->setStatusTip(tr("Show the built-in simulator's signals instead of the device's"));
    simulatorAction_->setCheckable(true);
    connect(simulatorAction_, &QAction::triggered, this, [this](bool on) {
        QMetaObject::invokeMethod(worker_, [w = worker_, on] { w->open(on); }, Qt::QueuedConnection);
    });

    reconnectAction_ = new QAction(tr("Reco&nnect Device"), this);
    reconnectAction_->setStatusTip(tr("Look for the device again and connect to it"));
    connect(reconnectAction_, &QAction::triggered, this, [this] {
        simulatorAction_->setChecked(false);
        QMetaObject::invokeMethod(worker_, [w = worker_] { w->open(false); }, Qt::QueuedConnection);
    });

    calibrationAction_ = new QAction(tr("&Calibration…"), this);
    calibrationAction_->setObjectName("calibrationAction");
    calibrationAction_->setStatusTip(tr("Correct the zero and gain of the connected device"));
    calibrationAction_->setEnabled(false);
    connect(calibrationAction_, &QAction::triggered, this, &MainWindow::openCalibration);

    exportAction_ = new QAction(icons::exportData(), tr("&Export Waveform as CSV…"), this);
    exportAction_->setShortcut(QKeySequence(tr("Ctrl+E")));
    exportAction_->setStatusTip(tr("Save the data of the tab, the waveforms or the spectrum, to a CSV file"));
    connect(exportAction_, &QAction::triggered, this, &MainWindow::exportCsv);

    screenshotAction_ = new QAction(icons::screenshot(), tr("Save &Screenshot…"), this);
    screenshotAction_->setShortcut(QKeySequence(tr("Ctrl+Shift+S")));
    screenshotAction_->setStatusTip(tr("Save a picture of the display"));
    connect(screenshotAction_, &QAction::triggered, this, &MainWindow::saveScreenshot);

    quitAction_ = new QAction(tr("&Quit"), this);
    quitAction_->setShortcut(QKeySequence::Quit);
    quitAction_->setStatusTip(tr("Close the program; the settings are kept"));
    connect(quitAction_, &QAction::triggered, this, &QWidget::close);

    formPanelAction_ = new QAction(tr("&Form"), this);
    formPanelAction_->setObjectName("formPanelAction");
    formPanelAction_->setStatusTip(tr("Set the oscilloscope with lists and number boxes"));
    frontPanelAction_ = new QAction(tr("Front &Panel"), this);
    frontPanelAction_->setObjectName("frontPanelAction");
    frontPanelAction_->setStatusTip(tr("Set the oscilloscope with knobs and keys, as on an instrument"));
    auto* panels = new QActionGroup(this);
    for (QAction* a : {formPanelAction_, frontPanelAction_}) {
        a->setCheckable(true);
        panels->addAction(a);
        connect(a, &QAction::triggered, this, [this] {
            controlsDock_->show();
            showControlPage();
            fitControlsDock();
        });
    }
    formPanelAction_->setChecked(true);

    resetStatsAction_ = new QAction(icons::resetStatistics(), tr("&Reset Statistics"), this);
    resetStatsAction_->setStatusTip(tr("Start the minimum, maximum, mean and count of the measurement table over"));

    auto* modes = new QActionGroup(this);
    const std::array modeNames{tr("&Off"), tr("&Time"), tr("&Voltage")};
    for (int i = 0; i < 3; ++i) {
        cursorModeActions_[i] = modes->addAction(modeNames[i]);
        cursorModeActions_[i]->setCheckable(true);
        connect(cursorModeActions_[i], &QAction::triggered, this, [this, i] {
            starosc::CursorSettings c = cursors_;
            c.mode = static_cast<starosc::CursorMode>(i);
            setCursors(c);
        });
    }
    cursorModeActions_[0]->setStatusTip(tr("No cursors on the display"));
    cursorModeActions_[1]->setStatusTip(tr("Two time cursors: the time between them and each channel's voltage change"));
    cursorModeActions_[2]->setStatusTip(tr("Two voltage cursors on the chosen channel"));
    cursorModeActions_[1]->setShortcut(QKeySequence(tr("Ctrl+T")));
    cursorModeActions_[2]->setShortcut(QKeySequence(tr("Ctrl+U")));
    auto* sources = new QActionGroup(this);
    for (int ch = 0; ch < starosc::kChannels; ++ch) {
        cursorSourceActions_[ch] = sources->addAction(tr("Voltage on CH%1").arg(ch + 1));
        cursorSourceActions_[ch]->setCheckable(true);
        cursorSourceActions_[ch]->setStatusTip(tr("The voltage cursors measure CH%1").arg(ch + 1));
        connect(cursorSourceActions_[ch], &QAction::triggered, this, [this, ch] {
            starosc::CursorSettings c = cursors_;
            c.source = ch;
            setCursors(c);
        });
    }

    classicPaletteAction_ = new QAction(tr("&Classic Palette"), this);
    classicPaletteAction_->setStatusTip(tr("The grey palette of the classic look, instead of the system's"));
    classicPaletteAction_->setCheckable(true);
    classicPaletteAction_->setChecked(theme::classicPalette());
    connect(classicPaletteAction_, &QAction::toggled, this, [](bool on) { theme::apply(theme::style(), on); });

    interpolationAction_ = new QAction(tr("Sin(x)/x &Interpolation"), this);
    interpolationAction_->setObjectName("interpolationAction");
    interpolationAction_->setCheckable(true);
    interpolationAction_->setStatusTip(tr("With few samples on screen, draw the signal through them instead of straight lines"));
    connect(interpolationAction_, &QAction::toggled, this, [this](bool on) { view_->setInterpolation(on); });

    xyAction_ = new QAction(icons::xy(), tr("&X-Y"), this);
    xyAction_->setObjectName("xyAction");
    xyAction_->setCheckable(true);
    xyAction_->setStatusTip(tr("Show CH2 against CH1; each display mode keeps its own channel positions"));
    connect(xyAction_, &QAction::toggled, this, [this](bool on) {
        auto s = settings_;
        s.display = on ? starosc::DisplayMode::XY : starosc::DisplayMode::YT;
        applySettings(s);
    });

    zoomOffAction_ = new QAction(tr("&Zoom Off"), this);
    zoomOffAction_->setObjectName("zoomOffAction");
    zoomOffAction_->setShortcut(QKeySequence(tr("Ctrl+0")));
    zoomOffAction_->setStatusTip(tr("Show the whole record again"));
    connect(zoomOffAction_, &QAction::triggered, this, [this] { setZoom({0, settings_.zoom.center}); });

    selectMeasurementsAction_ = new QAction(icons::measurements(), tr("Select &Measurements…"), this);
    selectMeasurementsAction_->setObjectName("selectMeasurementsAction");
    selectMeasurementsAction_->setStatusTip(tr("Choose the measurements of the table and their thresholds"));
    connect(selectMeasurementsAction_, &QAction::triggered, this, &MainWindow::selectMeasurements);
    tableColumnsAction_ = new QAction(tr("&Table Columns…"), this);
    tableColumnsAction_->setObjectName("tableColumnsAction");
    tableColumnsAction_->setStatusTip(tr("Choose the columns of the measurement table and their order"));

    traceColorsAction_ = new QAction(tr("Trace &Colors…"), this);
    traceColorsAction_->setObjectName("traceColorsAction");
    traceColorsAction_->setStatusTip(tr("Choose the colour of each trace"));
    connect(traceColorsAction_, &QAction::triggered, this, &MainWindow::chooseTraceColors);

    printThemeAction_ = new QAction(tr("&Print Theme"), this);
    printThemeAction_->setObjectName("printThemeAction");
    printThemeAction_->setCheckable(true);
    printThemeAction_->setStatusTip(tr("White background and dark traces, for pictures and documents"));
    connect(printThemeAction_, &QAction::toggled, this, [this](bool on) {
        display_style::setPrintTheme(on);
        refreshDisplayStyle();
    });

    dotsAction_ = new QAction(tr("Draw &Samples as Dots"), this);
    dotsAction_->setObjectName("dotsAction");
    dotsAction_->setCheckable(true);
    dotsAction_->setStatusTip(tr("Show the samples alone, without lines through them"));
    connect(dotsAction_, &QAction::toggled, this, [this](bool on) { view_->setDots(on); });

    resetLayoutAction_ = new QAction(icons::resetLayout(), tr("&Reset Layout"), this);
    resetLayoutAction_->setObjectName("resetLayoutAction");
    resetLayoutAction_->setStatusTip(tr("Dock the Controls and Measurements panels in their default places"));
    connect(resetLayoutAction_, &QAction::triggered, this, &MainWindow::resetLayout);

    aboutAction_ = new QAction(tr("&About Star Oscilloscope"), this);
    aboutAction_->setStatusTip(tr("Version and licence of the program"));
    aboutAction_->setObjectName("aboutAction");
    connect(aboutAction_, &QAction::triggered, this, &MainWindow::about);
}

void MainWindow::createMenus()
{
    QMenu* file = menuBar()->addMenu(tr("&File"));
    file->addAction(exportAction_);
    file->addAction(screenshotAction_);
    file->addSeparator();
    file->addAction(quitAction_);

    // The oscilloscope display first, then the windows around it, and last the look of the program's own windows
    // (the Qt style and palette), apart from the display's colours.
    QMenu* view = menuBar()->addMenu(tr("&View"));
    view->setObjectName("viewMenu");
    view->addAction(xyAction_);
    view->addAction(zoomOffAction_);
    view->addSeparator();
    view->addAction(interpolationAction_);
    view->addAction(dotsAction_);
    view->addSeparator();
    view->addAction(traceColorsAction_);
    view->addAction(printThemeAction_);
    view->addSeparator();
    QMenu* panelMenu = view->addMenu(tr("C&ontrol Panel"));
    panelMenu->addAction(formPanelAction_);
    panelMenu->addAction(frontPanelAction_);
    for (QDockWidget* dock : {controlsDock_, measurementsDock_, generatorDock_})
        view->addAction(dock->toggleViewAction());
    view->addAction(resetLayoutAction_);
    view->addSeparator();
    QMenu* appearance = view->addMenu(tr("&Window Appearance"));
    appearance->setObjectName("appearanceMenu");
    auto* styles = new QActionGroup(this);
    for (const QString& name : theme::styles()) {
        QAction* a = appearance->addAction(name);
        a->setCheckable(true);
        a->setChecked(name.compare(theme::style(), Qt::CaseInsensitive) == 0);
        styles->addAction(a);
        connect(a, &QAction::triggered, this, [this, name] { theme::apply(name, classicPaletteAction_->isChecked()); });
    }
    appearance->addSeparator();
    appearance->addAction(classicPaletteAction_);

    // Each language under its own name, so that it is found whatever language the menu is in.
    languageMenu_ = view->addMenu(tr("&Language"));
    languageMenu_->setObjectName("languageMenu");
    auto* languages = new QActionGroup(this);
    QList<std::pair<QString, QString>> entries{{QString(), tr("System Default")}};
    for (const language::Language& l : language::available())
        entries.push_back({l.code, l.name});
    for (const auto& [code, name] : entries) {
        QAction* a = languageMenu_->addAction(name);
        a->setData(code);
        a->setCheckable(true);
        a->setChecked(code == language::chosen());
        languages->addAction(a);
        connect(a, &QAction::triggered, this, [this, code] {
            if (code == language::chosen())
                return;
            language::choose(code);
            QMessageBox::information(this, tr("Language"), tr("The new language is used from the next start of the program."));
        });
    }

    QMenu* acquire = menuBar()->addMenu(tr("&Acquire"));
    acquire->addAction(runAction_);
    acquire->addAction(singleAction_);
    acquire->addAction(forceAction_);
    acquire->addAction(autosetAction_);
    acquire->addSeparator();
    acquire->addAction(simulatorAction_);
    acquire->addAction(reconnectAction_);
    acquire->addSeparator();
    acquire->addAction(calibrationAction_);

    QMenu* measure = menuBar()->addMenu(tr("&Measure"));
    cursorMenu_ = measure->addMenu(tr("&Cursors"));
    for (QAction* a : cursorModeActions_)
        cursorMenu_->addAction(a);
    cursorMenu_->addSeparator();
    for (QAction* a : cursorSourceActions_)
        cursorMenu_->addAction(a);
    measure->addSeparator();
    measure->addAction(selectMeasurementsAction_);
    measure->addAction(resetStatsAction_);
    measure->addAction(tableColumnsAction_);

    QMenu* help = menuBar()->addMenu(tr("&Help"));
    help->addAction(aboutAction_);
}

void MainWindow::createToolBar()
{
    QToolBar* bar = addToolBar(tr("Acquisition"));
    bar->setObjectName("acquisitionToolBar");
    bar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    bar->setIconSize({16, 16});
    bar->setMovable(false);
    bar->addAction(runAction_);
    bar->addAction(singleAction_);
    bar->addAction(forceAction_);
    bar->addAction(autosetAction_);
    bar->addSeparator();
    bar->addAction(xyAction_);
    auto* cursors = new QToolButton(bar);
    cursors->setText(tr("Cursors"));
    cursors->setStatusTip(tr("Time or voltage cursors on the display"));
    cursors->setIcon(icons::cursors());
    cursors->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    cursors->setMenu(cursorMenu_);
    cursors->setPopupMode(QToolButton::InstantPopup);
    bar->addWidget(cursors);
    bar->addAction(selectMeasurementsAction_);
    bar->addAction(resetStatsAction_);
    bar->addSeparator();
    generatorDock_->toggleViewAction()->setIcon(icons::generator());
    bar->addAction(generatorDock_->toggleViewAction());
}

void MainWindow::createCentralWidget()
{
    auto* tabs = new QTabWidget(this);
    tabs_ = tabs;

    auto* scope = new QWidget(tabs);
    auto* layout = new QVBoxLayout(scope);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(3);
    view_ = new ScopeView;
    overview_ = new OverviewStrip(view_);
    view_->setOverview(overview_);
    layout->addWidget(sunkenFrame(view_, scope), 1);
    cursorInfo_ = sunkenLabel(scope);
    cursorInfo_->setObjectName("cursorInfo");
    cursorInfo_->setFont(display_style::fixedFont());
    layout->addWidget(cursorInfo_);
    tabs->addTab(scope, tr("Oscilloscope"));

    spectrumTab_ = new QWidget(tabs);
    auto* spectrumLayout = new QVBoxLayout(spectrumTab_);
    spectrumLayout->setContentsMargins(4, 4, 4, 4);
    spectrumLayout->setSpacing(3);
    spectrumView_ = new SpectrumView;
    spectrumLayout->addWidget(sunkenFrame(spectrumView_, spectrumTab_), 1);
    spectrumInfo_ = sunkenLabel(spectrumTab_);
    spectrumInfo_->setObjectName("spectrumInfo");
    spectrumInfo_->setTextFormat(Qt::PlainText);
    spectrumInfo_->setFont(display_style::fixedFont());
    spectrumLayout->addWidget(spectrumInfo_);
    tabs->addTab(spectrumTab_, tr("Spectrum"));

    generatorPanel_ = new GeneratorPanel;
    // In a scroll area: a tab widget is as tall as its tallest page, and this form held the display, and the
    // Measurements dock under it, at its height.
    generatorPage_ = new QScrollArea(tabs);
    generatorPage_->setWidget(generatorPanel_);
    generatorPage_->setWidgetResizable(true);
    generatorPage_->setFrameShape(QFrame::NoFrame);
    tabs->setTabEnabled(tabs->addTab(generatorPage_, tr("Generator")), false);
    connect(generatorPanel_, &GeneratorPanel::settingsChanged, this, &MainWindow::applyGenerator);
    connect(generatorPanel_, &GeneratorPanel::parkOutputChanged, this, [this](bool on) { parkGenerator_ = on; });
    connect(tabs, &QTabWidget::currentChanged, this, &MainWindow::onTabChanged);
    setCentralWidget(tabs);
}

void MainWindow::createDocks()
{
    // Controls run the full height; measurements sit under the display only.
    setCorner(Qt::TopRightCorner, Qt::RightDockWidgetArea);
    setCorner(Qt::BottomRightCorner, Qt::RightDockWidgetArea);

    // The Controls and Measurements docks show the panels of the current tab: the oscilloscope's, or the
    // spectrum's on the Spectrum tab.
    const auto scrolled = [](QWidget* panel) {
        auto* scroll = new QScrollArea;
        scroll->setWidget(panel);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll->setProperty("panelWidth", panel->sizeHint().width() + scroll->verticalScrollBar()->sizeHint().width());
        return scroll;
    };
    controls_ = new ControlPanel;
    frontPanel_ = new FrontPanel;
    spectrumPanel_ = new SpectrumPanel;
    controlsStack_ = new QStackedWidget;
    formPage_ = scrolled(controls_);
    frontPage_ = scrolled(frontPanel_);
    spectrumPage_ = scrolled(spectrumPanel_);
    for (QWidget* page : {formPage_, spectrumPage_, frontPage_})
        controlsStack_->addWidget(page);
    controlsDock_ = new QDockWidget(tr("Controls"), this);
    controlsDock_->setObjectName("controlsDock");
    controlsDock_->setWidget(controlsStack_);
    connect(spectrumPanel_, &SpectrumPanel::settingsChanged, this,
            [this](const starosc::SpectrumSettings& s) { applySpectrum(s, false); });
    connect(spectrumPanel_, &SpectrumPanel::scopeSettingsChanged, this,
            [this](const starosc::ScopeSettings& s) { applySettings(s); });
    connect(spectrumPanel_, &SpectrumPanel::restartRequested, this,
            [this] { QMetaObject::invokeMethod(worker_, &AcquisitionWorker::resetSpectrum, Qt::QueuedConnection); });
    for (ScopePanel* panel : scopePanels()) {
        connect(panel, &ScopePanel::settingsChanged, this,
                [this, panel](const starosc::ScopeSettings& s) { applySettings(s, panel); });
        connect(panel, &ScopePanel::referenceStoreRequested, this, &MainWindow::storeReference);
        connect(panel, &ScopePanel::referenceSaveRequested, this, &MainWindow::saveReference);
        connect(panel, &ScopePanel::referenceLoadRequested, this, &MainWindow::loadReference);
        connect(panel, &ScopePanel::referenceClearRequested, this, &MainWindow::clearReference);
        connect(panel, &ScopePanel::referenceShown, this, [this](int ref, bool shown) {
            references_[static_cast<std::size_t>(ref)].shown = shown;
            showReferences();
        });
    }

    measurements_ = new MeasurementTable;
    spectrumTable_ = new SpectrumTable;
    measurementsStack_ = new QStackedWidget;
    measurementsStack_->addWidget(measurements_);
    measurementsStack_->addWidget(spectrumTable_);
    measurementsDock_ = new QDockWidget(tr("Measurements"), this);
    measurementsDock_->setObjectName("measurementsDock");
    measurementsDock_->setWidget(measurementsStack_);
    connect(resetStatsAction_, &QAction::triggered, measurements_, &MeasurementTable::resetStatistics);
    connect(measurements_, &MeasurementTable::selectRequested, this, &MainWindow::selectMeasurements);
    connect(tableColumnsAction_, &QAction::triggered, measurements_, &MeasurementTable::editColumns);
    connect(measurements_, &MeasurementTable::selectionChanged, view_, &ScopeView::setMeasurementSelection);


    generatorStrip_ = new GeneratorStrip;
    generatorDock_ = new QDockWidget(tr("Generator"), this);
    generatorDock_->setObjectName("generatorDock");
    generatorDock_->setWidget(generatorStrip_);
    generatorDock_->toggleViewAction()->setStatusTip(tr("Show the generator controls under the display"));
    generatorDock_->hide();
    connect(generatorStrip_, &GeneratorStrip::settingsChanged, this, &MainWindow::applyGenerator);
    connect(generatorStrip_, &GeneratorStrip::moreRequested, this, [this] { tabs_->setCurrentWidget(generatorPage_); });
    showControlPage();
    resetLayout();
}

std::array<ScopePanel*, 2> MainWindow::scopePanels() const { return {controls_, frontPanel_}; }

void MainWindow::showControlPage()
{
    QWidget* page = spectrumShown() ? spectrumPage_ : frontPanelAction_->isChecked() ? frontPage_ : formPage_;
    controlsStack_->setCurrentWidget(page);
    // A stack asks for the width of the widest page; only the one on show should set the width of the dock.
    for (QWidget* each : {formPage_, spectrumPage_, frontPage_})
        each->setMinimumWidth(each == page ? each->property("panelWidth").toInt() : 0);
}

void MainWindow::fitControlsDock()
{
    resizeDocks({controlsDock_}, {controlsStack_->currentWidget()->minimumWidth()}, Qt::Horizontal);
}

void MainWindow::resetLayout()
{
    // Dragging a floating dock back does not work under every window system (WSLg), so this is the way back.
    // The generator strip keeps its visibility; it goes right under the display, above the measurements.
    const bool generatorShown = !generatorDock_->isHidden();
    const std::array<std::tuple<QDockWidget*, Qt::DockWidgetArea, Qt::Orientation>, 3> docks{
        {{controlsDock_, Qt::RightDockWidgetArea, Qt::Vertical},
         {generatorDock_, Qt::BottomDockWidgetArea, Qt::Vertical},
         {measurementsDock_, Qt::BottomDockWidgetArea, Qt::Vertical}}};
    for (const auto& [dock, area, orientation] : docks) {
        dock->setFloating(false);  // while still in the layout; Qt does not re-dock a removed dock
        removeDockWidget(dock);
        addDockWidget(area, dock, orientation);
        dock->show();
    }
    generatorDock_->setVisible(generatorShown);
    resizeDocks({measurementsDock_}, {190}, Qt::Vertical);
    fitControlsDock();
}

void MainWindow::applyGenerator(const starosc::GeneratorSettings& settings)
{
    generator_ = starosc::constrained(settings);
    generatorPanel_->setSettings(generator_);
    generatorStrip_->setSettings(generator_);
    generatorLabel_->setText(generator_.output ? tr("Gen: %1").arg(generator_ui::summary(generator_)) : tr("Gen: off"));
    QMetaObject::invokeMethod(worker_, [w = worker_, g = generator_] { w->setGenerator(g); }, Qt::QueuedConnection);
}

void MainWindow::onGeneratorAvailable(bool available, bool levels)
{
    generatorAvailable_ = available;
    generatorLevels_ = available && levels;
    generatorPanel_->setLevelControl(generatorLevels_);
    generatorStrip_->setLevelControl(generatorLevels_);
    tabs_->setTabEnabled(tabs_->indexOf(generatorPage_), available);
    tabs_->setTabToolTip(tabs_->indexOf(generatorPage_), available ? QString() : tr("This device has no generator"));
    generatorDock_->toggleViewAction()->setEnabled(available);
    generatorStrip_->setEnabled(available);
    generatorLabel_->setVisible(available);
    if (!available)
        return;
    if (parkGenerator_ && generatorLevels_) {
        generator_.output = false;
        QMetaObject::invokeMethod(worker_, &AcquisitionWorker::parkGenerator, Qt::QueuedConnection);
    }
    applyGenerator(generator_);
}

void MainWindow::createStatusBar()
{
    stateLabel_ = new QLabel(tr("Stopped"));
    sourceLabel_ = new QLabel;
    calibrationLabel_ = new QLabel;
    generatorLabel_ = new QLabel;
    generatorLabel_->setVisible(false);
    samplingLabel_ = new QLabel;
    rateLabel_ = new QLabel;
    // Fields whose text changes with the frames hold the width of their longest text, so that the others stay where
    // they are.
    const auto reserve = [](QLabel* label, const QStringList& longest) {
        const QFontMetrics metrics(label->font());
        int width = 0;
        for (const QString& text : longest)
            width = std::max(width, metrics.horizontalAdvance(text));
        label->setMinimumWidth(width + 2 * label->margin() + 4);
        label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    };
    samplingLabel_->setObjectName("samplingInfo");
    reserve(samplingLabel_, {tr("%1 · %2 pts").arg(QStringLiteral("888 kSa/s")).arg(8888888),
                             tr("%1 · %2 pts").arg(QStringLiteral("8.88 MSa/s")).arg(8888888)});
    rateLabel_->setObjectName("rateInfo");
    reserve(rateLabel_, {tr("%1 frames/s").arg(888.8, 0, 'f', 1)});
    calibrationLabel_->setObjectName("calibrationInfo");
    reserve(calibrationLabel_, {tr("Cal: default"), tr("Cal: user")});
    statusBar()->addWidget(stateLabel_, 1);
    statusBar()->addPermanentWidget(sourceLabel_);
    statusBar()->addPermanentWidget(calibrationLabel_);
    statusBar()->addPermanentWidget(generatorLabel_);
    statusBar()->addPermanentWidget(samplingLabel_);
    statusBar()->addPermanentWidget(rateLabel_);
}

void MainWindow::connectDisplay()
{
    connect(view_, &ScopeView::channelPositionChanged, this, [this](int ch, double div) {
        auto s = settings_;
        s.channel[ch].position_div = div;
        applySettings(s);
    });
    connect(view_, &ScopeView::triggerLevelChanged, this, [this](double volts) {
        auto s = settings_;
        s.trigger.level_v = volts;
        applySettings(s);
    });
    connect(view_, &ScopeView::preTriggerChanged, this, [this](double fraction) {
        auto s = settings_;
        s.trigger.pre_trigger = fraction;
        applySettings(s);
    });
    connect(view_, &ScopeView::voltsPerDivStepped, this, [this](int ch, int steps) {
        auto s = settings_;
        auto& c = s.channel[ch];
        c.volts_per_div = starosc::step_value(starosc::volts_per_div_steps(c.probe), c.volts_per_div, steps);
        applySettings(s);
    });
    connect(view_, &ScopeView::timebaseStepped, this, [this](int steps) {
        auto s = settings_;
        s.timebase.seconds_per_div =
            starosc::step_value(starosc::seconds_per_div_steps(), s.timebase.seconds_per_div, steps);
        applySettings(s);
    });
    connect(view_, &ScopeView::mathPositionChanged, this, [this](double div) {
        auto s = settings_;
        s.math.position_div = div;
        applySettings(s);
    });
    connect(view_, &ScopeView::mathScaleStepped, this, [this](int steps) {
        auto s = settings_;
        s.math.units_per_div = starosc::step_value(starosc::units_per_div_steps(), s.math.units_per_div, steps);
        applySettings(s);
    });
    connect(view_, &ScopeView::referencePositionChanged, this, [this](int ref, double div) {
        references_[static_cast<std::size_t>(ref)].position_div = div;
        showReferences();
    });
    connect(view_, &ScopeView::referenceScaleStepped, this, [this](int ref, int steps) {
        auto& r = references_[static_cast<std::size_t>(ref)];
        r.units_per_div = starosc::step_value(starosc::units_per_div_steps(), r.units_per_div, steps);
        showReferences();
    });
    connect(view_, &ScopeView::cursorsChanged, this, [this](const starosc::CursorSettings& c) {
        cursors_ = c;
        updateCursorInfo();
    });

    const auto stepZoom = [this](int steps, double x) {
        setZoom(starosc::zoom_stepped(settings_.zoom, settings_.timebase, recordRate(), steps, x));
    };
    const auto zoomToRange = [this](double begin, double end) {
        setZoom(starosc::zoom_to_range(settings_.timebase, recordRate(), begin, end));
    };
    const auto moveZoom = [this](double center) { setZoom({settings_.zoom.seconds_per_div, center}); };
    const auto zoomOff = [this] { setZoom({0, settings_.zoom.center}); };
    connect(view_, &ScopeView::zoomStepped, this, stepZoom);
    connect(view_, &ScopeView::zoomRangeSelected, this, zoomToRange);
    connect(view_, &ScopeView::zoomCenterChanged, this, moveZoom);
    connect(view_, &ScopeView::zoomOffRequested, this, zoomOff);
    connect(overview_, &OverviewStrip::zoomStepped, this, [stepZoom](int steps) { stepZoom(steps, 0.5); });
    connect(overview_, &OverviewStrip::zoomRangeSelected, this, zoomToRange);
    connect(overview_, &OverviewStrip::zoomCenterChanged, this, moveZoom);
    connect(overview_, &OverviewStrip::zoomOffRequested, this, zoomOff);


    connect(spectrumView_, &SpectrumView::spanChanged, this, [this](double start, double stop) {
        auto s = spectrum_;
        s.start_hz = start;
        s.stop_hz = stop;
        applySpectrum(s, true);
    });
    connect(spectrumView_, &SpectrumView::referenceLevelChanged, this, [this](double top) {
        auto s = spectrum_;
        s.top_dbv = top;
        applySpectrum(s, true);
    });
}

void MainWindow::applySpectrum(const starosc::SpectrumSettings& settings, bool updatePanel)
{
    spectrum_ = starosc::constrained(settings);
    if (updatePanel)
        spectrumPanel_->setSettings(spectrum_);
    spectrumView_->setSettings(spectrum_);
    QMetaObject::invokeMethod(worker_, [w = worker_, s = spectrum_] { w->setSpectrumSettings(s); }, Qt::QueuedConnection);
}

bool MainWindow::spectrumShown() const { return tabs_->currentWidget() == spectrumTab_; }

bool MainWindow::showTab(const QString& name)
{
    if (name == "spectrum")
        tabs_->setCurrentWidget(spectrumTab_);
    else if (name == "oscilloscope")
        tabs_->setCurrentIndex(0);
    else if (name == "generator")
        tabs_->setCurrentWidget(generatorPage_);
    else
        return false;
    return true;
}

void MainWindow::onTabChanged()
{
    const bool spectrum = spectrumShown();
    showControlPage();
    measurementsStack_->setCurrentIndex(spectrum ? 1 : 0);
    forceAction_->setEnabled(!spectrum);
    xyAction_->setEnabled(!spectrum);
    tableColumnsAction_->setEnabled(!spectrum);  // the spectrum's table has a column for each trace
    exportAction_->setText(spectrum ? tr("&Export Spectrum as CSV…") : tr("&Export Waveform as CSV…"));
    frameClock_.invalidate();
    framesPerSecond_ = 0;
    const auto mode = spectrum ? AcquisitionWorker::Mode::Spectrum : AcquisitionWorker::Mode::Scope;
    QMetaObject::invokeMethod(worker_, [w = worker_, mode] { w->setMode(mode); }, Qt::QueuedConnection);
}

void MainWindow::applySettings(const starosc::ScopeSettings& requested, ScopePanel* origin)
{
    // Another display mode brings back the channel positions it had.
    starosc::ScopeSettings settings = requested;
    if (requested.display != settings_.display) {
        settings.display = settings_.display;
        settings = starosc::with_display(settings, requested.display);
    }

    // Zooming changes only what is drawn, so the statistics go on.
    starosc::ScopeSettings withOldZoom = settings;
    withOldZoom.zoom = settings_.zoom;
    const bool measuredAlike = withOldZoom == settings_;
    const bool thresholdsChanged = settings.thresholds != settings_.thresholds;

    settings.zoom = starosc::constrained(settings.zoom, settings.timebase, recordRate());
    settings_ = settings;
    for (ScopePanel* panel : scopePanels())
        if (panel != origin || settings != requested)
            panel->setSettings(settings);
    {
        const QSignalBlocker block(xyAction_);
        xyAction_->setChecked(settings.display == starosc::DisplayMode::XY);
    }
    view_->setSettings(settings);
    overview_->setSettings(settings);
    spectrumPanel_->setScopeSettings(settings);
    zoomOffAction_->setEnabled(settings.zoom.on());
    if (!measuredAlike)
        measurements_->resetStatistics();
    updateCursorInfo();
    QMetaObject::invokeMethod(worker_, [w = worker_, settings] { w->setSettings(settings); }, Qt::QueuedConnection);

    // No new frame comes while stopped, so the one on the screen is measured again.
    if (thresholdsChanged && state_ == AcquisitionWorker::Stopped && lastFrame_) {
        auto frame = std::make_shared<starosc::Frame>(*lastFrame_);
        frame->settings.thresholds = settings.thresholds;
        starosc::measure_frame(*frame);
        lastFrame_ = frame;
        view_->setFrame(lastFrame_);
        overview_->setFrame(lastFrame_);
        measurements_->setFrame(*lastFrame_);
    }
}

void MainWindow::setZoom(const starosc::ZoomSettings& zoom)
{
    starosc::ScopeSettings s = settings_;
    s.zoom = zoom;
    applySettings(s);
}

double MainWindow::recordRate() const { return lastFrame_ ? lastFrame_->plan.sample_rate_hz : 0; }

void MainWindow::refreshDisplayStyle()
{
    view_->update();
    overview_->update();
    spectrumView_->update();
    measurements_->refreshColors();
    spectrumTable_->refreshColors();
    spectrumPanel_->refreshColors();
    for (ScopePanel* panel : scopePanels())
        panel->refreshColors();
    updateCursorInfo();
}

void MainWindow::chooseTraceColors()
{
    TraceColorsDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    dialog.apply();
    refreshDisplayStyle();
}

void MainWindow::selectMeasurements()
{
    MeasurementDialog dialog(measurements_->selection(), settings_.thresholds, this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    measurements_->setSelection(dialog.selection());
    starosc::ScopeSettings s = settings_;
    s.thresholds = dialog.thresholds();
    applySettings(s);
    if (lastFrame_)
        measurements_->setFrame(*lastFrame_);
}

void MainWindow::setCursors(const starosc::CursorSettings& cursors)
{
    cursors_ = cursors;
    cursorModeActions_[static_cast<int>(cursors.mode)]->setChecked(true);
    cursorSourceActions_[cursors.source]->setChecked(true);
    view_->setCursors(cursors);
    updateCursorInfo();
}

void MainWindow::onSourceOpened(const QString& name, bool simulated, const QString& note)
{
    simulatorAction_->setChecked(simulated);
    sourceLabel_->setText(simulated ? (note.isEmpty() ? tr("Simulator") : tr("Simulator (no device)")) : name);
    sourceLabel_->setStatusTip(note);
    if (!note.isEmpty())
        statusBar()->showMessage(note, 8000);
    // Acquisition starts in onFactoryCalibration, which follows, so that no frame misses the calibration.
}

void MainWindow::onFailed(const QString& message)
{
    statusBar()->showMessage(tr("Acquisition stopped: %1").arg(message));
}

void MainWindow::onFactoryCalibration(const starosc::UserCalibration& factory)
{
    factoryCalibration_ = factory;
    QString error;
    const starosc::UserCalibration saved = calibration_store::loadFor(factory, &error);
    if (!error.isEmpty())
        statusBar()->showMessage(tr("Calibration file ignored: %1").arg(error), 8000);
    calibrationAction_->setEnabled(!factory.stages.empty());
    applyCalibration(saved);
    QMetaObject::invokeMethod(worker_, [w = worker_, s = settings_] { w->setSettings(s); }, Qt::QueuedConnection);
    QMetaObject::invokeMethod(worker_, [w = worker_, s = spectrum_] { w->setSpectrumSettings(s); }, Qt::QueuedConnection);
    onTabChanged();  // the worker of a new source starts in oscilloscope mode
    QMetaObject::invokeMethod(worker_, &AcquisitionWorker::run, Qt::QueuedConnection);
}

void MainWindow::applyCalibration(const starosc::UserCalibration& calibration)
{
    calibration_ = calibration;
    QMetaObject::invokeMethod(worker_, [w = worker_, calibration] { w->setCalibration(calibration); },
                              Qt::QueuedConnection);
    const bool factory = calibration.is_factory();
    // "Default": the table the device carries, as it came; "user": corrected here (zero, gain or both).
    calibrationLabel_->setText(factory ? tr("Cal: default") : tr("Cal: user"));
    calibrationLabel_->setStatusTip(
        factory ? tr("The device's own default calibration (Acquire > Calibration…)")
                : tr("User calibration — zero: %1, gain: %2")
                      .arg(calibration.zero_time.empty() ? tr("default") : QString::fromStdString(calibration.zero_time),
                           calibration.gain_time.empty() ? tr("default") : QString::fromStdString(calibration.gain_time)));
}

void MainWindow::openCalibration()
{
    CalibrationDialog dialog(factoryCalibration_, calibration_, this);
    connect(&dialog, &CalibrationDialog::measureZeroRequested, worker_, &AcquisitionWorker::measureZero,
            Qt::QueuedConnection);
    connect(worker_, &AcquisitionWorker::zeroMeasured, &dialog, &CalibrationDialog::zeroMeasured);
    connect(worker_, &AcquisitionWorker::zeroFailed, &dialog, &CalibrationDialog::zeroFailed);
    connect(&dialog, &CalibrationDialog::gainWizardRequested, this, [this, &dialog] { runGainWizard(dialog); });
    dialog.setGainWizardAvailable(generatorLevels_);  // it sets DC levels with the generator's offset
    if (dialog.exec() != QDialog::Accepted)
        return;
    QString error;
    if (!calibration_store::store(dialog.calibration(), &error))
        QMessageBox::warning(this, tr("Calibration"), tr("The calibration could not be saved: %1").arg(error));
    applyCalibration(dialog.calibration());
    resetStatsAction_->trigger();  // earlier frames used the old calibration
}

void MainWindow::runGainWizard(CalibrationDialog& dialog)
{
    GainWizard wizard(dialog.calibration(), &dialog);
    connect(&wizard, &GainWizard::checkBenchRequested, worker_, &AcquisitionWorker::checkGainBench, Qt::QueuedConnection);
    connect(&wizard, &GainWizard::levelRequested, worker_, &AcquisitionWorker::measureGainLevel, Qt::QueuedConnection);
    connect(&wizard, &GainWizard::transferRequested, worker_, &AcquisitionWorker::transferGains, Qt::QueuedConnection);
    connect(worker_, &AcquisitionWorker::gainBenchOk, &wizard, &GainWizard::benchChecked);
    connect(worker_, &AcquisitionWorker::gainLevelMeasured, &wizard, &GainWizard::levelMeasured);
    connect(worker_, &AcquisitionWorker::gainTransferProgress, &wizard, &GainWizard::transferProgress);
    connect(worker_, &AcquisitionWorker::gainTransferred, &wizard, &GainWizard::transferred);
    connect(worker_, &AcquisitionWorker::gainStepFailed, &wizard, &GainWizard::stepFailed);
    QMetaObject::invokeMethod(worker_, &AcquisitionWorker::beginGainWizard, Qt::QueuedConnection);
    const bool accepted = wizard.exec() == QDialog::Accepted;
    QMetaObject::invokeMethod(worker_, &AcquisitionWorker::endGainWizard, Qt::QueuedConnection);
    if (accepted)
        dialog.adoptGains(wizard.result());
}

void MainWindow::onState(AcquisitionWorker::State state)
{
    state_ = state;
    if (state != AcquisitionWorker::Running)
        frontPanel_->setTriggered(false);
    switch (state) {
    case AcquisitionWorker::Stopped:
        stateLabel_->setText(tr("Stopped"));
        view_->setRunState(ScopeView::RunState::Stopped);
        break;
    case AcquisitionWorker::Running:
        stateLabel_->setText(tr("Running"));
        view_->setRunState(ScopeView::RunState::Running);
        break;
    case AcquisitionWorker::Waiting:
        stateLabel_->setText(tr("Waiting for trigger"));
        view_->setRunState(ScopeView::RunState::Waiting);
        break;
    }
    runAction_->setText(state == AcquisitionWorker::Stopped ? tr("&Run") : tr("&Stop"));
    runAction_->setIcon(state == AcquisitionWorker::Stopped ? icons::run() : icons::stop());
}

void MainWindow::toggleRun()
{
    if (state_ == AcquisitionWorker::Stopped)
        QMetaObject::invokeMethod(worker_, &AcquisitionWorker::run, Qt::QueuedConnection);
    else
        QMetaObject::invokeMethod(worker_, &AcquisitionWorker::stop, Qt::QueuedConnection);
}

void MainWindow::onFrame(FramePtr frame)
{
    worker_->frameTaken();
    const double oldRate = recordRate();
    lastFrame_ = frame;
    view_->setFrame(frame);
    overview_->setFrame(frame);
    measurements_->setFrame(*frame);
    if (recordRate() != oldRate) {
        for (ScopePanel* panel : scopePanels())
            panel->setSampleRate(recordRate());
        setZoom(settings_.zoom);  // a slower record may not allow the zoom timebase
    }
    if (cursors_.mode == starosc::CursorMode::Time)
        updateCursorInfo();

    const auto& plan = frame->plan;
    const QString sampling = tr("%1 · %2 pts").arg(si(plan.sample_rate_hz, "Sa/s", 3, true)).arg(plan.window_samples);
    samplingLabel_->setText(sampling);
    for (ScopePanel* panel : scopePanels())
        panel->setAcquisitionInfo(sampling);
    frontPanel_->setTriggered(frame->triggered);
    generatorPanel_->setSensitiveInputs(frame->sensitive);
    stateLabel_->setText(frame->triggered ? tr("Trig'd") : tr("Auto (no trigger)"));
    frameShown();
}

void MainWindow::onSpectrum(SpectrumFramePtr frame)
{
    worker_->frameTaken();
    if (!spectrumShown())
        return;  // left over from before a tab change
    lastSpectrum_ = frame;
    spectrumView_->setFrame(frame);
    spectrumTable_->setFrame(frame);

    const starosc::SpectrumFrame& f = *frame;
    const auto [lo, hi] = spectrumView_->visibleSpan();
    const QString window = windowName(f.settings.window);
    const QString rbw = si(f.noise_bandwidth * f.bin_hz, "Hz", 3);
    QString resolution = tr("bin %1 · RBW %2 · record %3")
                             .arg(si(f.bin_hz, "Hz", 4), rbw, si(static_cast<double>(f.record) / f.sample_rate_hz, "s", 3));
    if (f.points < f.settings.points && f.records > 1)
        resolution += tr(" · %1 points: the device delivers this rate in blocks of that length; %2 records averaged")
                          .arg(f.points)
                          .arg(f.records);
    else if (f.points < f.settings.points)
        resolution += tr(" · %1 points: the device delivers only %2 contiguous samples at this rate")
                          .arg(f.points)
                          .arg(f.record);
    spectrumPanel_->setResolutionInfo(resolution);
    spectrumInfo_->setText(tr("Span %1 – %2   %3   %4 points   bin %5   RBW %6 (%7)")
                               .arg(si(lo, "Hz", 4, true), si(hi, "Hz", 4, true), si(f.sample_rate_hz, "Sa/s", 3, true),
                                    QLocale().toString(static_cast<qulonglong>(f.points)), si(f.bin_hz, "Hz", 4),
                                    rbw, window));
    samplingLabel_->setText(tr("%1 · %2 pts").arg(si(f.sample_rate_hz, "Sa/s", 3, true)).arg(f.points));
    stateLabel_->setText(tr("Running"));
    frameShown();
}

void MainWindow::frameShown()
{
    if (frameClock_.isValid()) {
        const double dt = frameClock_.restart() / 1000.0;
        if (dt > 0)
            framesPerSecond_ = framesPerSecond_ ? 0.8 * framesPerSecond_ + 0.2 / dt : 1 / dt;
    } else {
        frameClock_.start();
    }
    rateLabel_->setText(tr("%1 frames/s").arg(framesPerSecond_, 0, 'f', 1));
    if (state_ == AcquisitionWorker::Stopped)
        stateLabel_->setText(tr("Stopped"));

    if (!screenshotPath_.isEmpty() && ++framesShown_ == screenshotFrames_)
        QTimer::singleShot(300, this, [this] {
            grab().save(screenshotPath_);
            QApplication::quit();
        });
}

void MainWindow::updateCursorInfo()
{
    if (cursors_.mode == starosc::CursorMode::Off) {
        cursorInfo_->hide();
        return;
    }
    const starosc::CursorReadout r = starosc::cursor_readout(settings_, cursors_);
    // A square of the channel's colour, since light colours do not read as text on the grey panel.
    const auto channelTag = [](int ch) {
        return QString("<span style=\"background:%1\">&nbsp;&nbsp;</span>&nbsp;CH%2")
            .arg(display_style::channel(ch).name())
            .arg(ch + 1);
    };
    QString text;
    if (cursors_.mode == starosc::CursorMode::Time) {
        text = tr("T1 %1&nbsp;&nbsp;&nbsp;T2 %2&nbsp;&nbsp;&nbsp;ΔT %3&nbsp;&nbsp;&nbsp;1/ΔT %4")
                   .arg(cursorField(r.time_s[0], "s"), cursorField(r.time_s[1], "s"), cursorField(r.delta_t, "s"),
                        cursorField(r.inverse_delta_t, "Hz"));
        if (lastFrame_)
            for (int ch = 0; ch < starosc::kChannels; ++ch) {
                if (lastFrame_->volts[ch].empty())
                    continue;
                const starosc::VisibleRange range = starosc::visible_range(settings_.timebase, settings_.zoom);
                const double v1 = starosc::value_at(*lastFrame_, ch, range.begin + cursors_.x[0] * range.span);
                const double v2 = starosc::value_at(*lastFrame_, ch, range.begin + cursors_.x[1] * range.span);
                text += tr("&nbsp;&nbsp;&nbsp;%1 ΔV %2").arg(channelTag(ch), cursorField(v2 - v1, "V"));
            }
    } else {
        text = tr("%1&nbsp;&nbsp;&nbsp;V1 %2&nbsp;&nbsp;&nbsp;V2 %3&nbsp;&nbsp;&nbsp;ΔV %4")
                   .arg(channelTag(cursors_.source), cursorField(r.volts[0], "V"), cursorField(r.volts[1], "V"),
                        cursorField(r.delta_v, "V"));
    }
    cursorInfo_->setText(text);
    cursorInfo_->show();
}

void MainWindow::storeReference(int ref, int source)
{
    const QString name = source == starosc::kMathSource ? tr("Math") : tr("CH%1").arg(source + 1);
    const auto r = lastFrame_ ? starosc::make_reference(*lastFrame_, source) : std::nullopt;
    if (!r) {
        statusBar()->showMessage(tr("%1 is not on the screen; nothing stored").arg(name), 6000);
        return;
    }
    references_[static_cast<std::size_t>(ref)] = *r;
    showReferences();
    statusBar()->showMessage(tr("Stored %1 in R%2").arg(name).arg(ref + 1), 4000);
}

void MainWindow::saveReference(int ref)
{
    const starosc::Reference& r = references_[static_cast<std::size_t>(ref)];
    if (r.empty()) {
        statusBar()->showMessage(tr("R%1 is empty").arg(ref + 1), 4000);
        return;
    }
    const QString path = QFileDialog::getSaveFileName(this, tr("Save Reference"), QString("R%1.csv").arg(ref + 1),
                                                      tr("CSV files (*.csv)"));
    if (path.isEmpty())
        return;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text) ||
        file.write(QByteArray::fromStdString(starosc::reference_to_csv(r))) < 0)
        QMessageBox::warning(this, tr("Save Reference"), tr("Cannot write %1").arg(path));
}

void MainWindow::loadReference(int ref)
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Load Reference"), QString(), tr("CSV files (*.csv)"));
    if (path.isEmpty())
        return;
    QFile file(path);
    std::string error = "cannot read the file";
    const auto r = file.open(QIODevice::ReadOnly | QIODevice::Text)
                       ? starosc::reference_from_csv(file.readAll().toStdString(), &error)
                       : std::nullopt;
    if (!r) {
        QMessageBox::warning(this, tr("Load Reference"), tr("%1: %2").arg(path, QString::fromStdString(error)));
        return;
    }
    references_[static_cast<std::size_t>(ref)] = *r;
    showReferences();
}

void MainWindow::clearReference(int ref)
{
    references_[static_cast<std::size_t>(ref)] = {};
    showReferences();
}

void MainWindow::showReferences()
{
    view_->setReferences(references_);
    for (int i = 0; i < starosc::kReferences; ++i) {
        const starosc::Reference& r = references_[static_cast<std::size_t>(i)];
        QString text;
        if (!r.empty())
            text = tr("%1 · %2 · %3")
                       .arg(QString::fromStdString(r.source))
                       .arg(si(r.units_per_div, (r.unit + "/div").c_str(), 3, true))
                       .arg(si(static_cast<double>(r.samples.size()) * r.sample_interval_s, "s", 3, true));
        for (ScopePanel* panel : scopePanels())
            panel->setReferenceState(i, text, r.shown);
    }
}

void MainWindow::loadKeptReferences()
{
    QSettings saved;
    for (int i = 0; i < starosc::kReferences; ++i) {
        QFile file(QString("%1/R%2.csv").arg(referencesDir()).arg(i + 1));
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
            continue;
        if (auto r = starosc::reference_from_csv(file.readAll().toStdString())) {
            r->shown = saved.value(QString("references/R%1/shown").arg(i + 1), true).toBool();
            references_[static_cast<std::size_t>(i)] = *r;
        }
    }
    showReferences();
}

void MainWindow::keepReferences() const
{
    QSettings saved;
    QDir().mkpath(referencesDir());
    for (int i = 0; i < starosc::kReferences; ++i) {
        const starosc::Reference& r = references_[static_cast<std::size_t>(i)];
        const QString path = QString("%1/R%2.csv").arg(referencesDir()).arg(i + 1);
        saved.setValue(QString("references/R%1/shown").arg(i + 1), r.shown);
        if (r.empty()) {
            QFile::remove(path);
            continue;
        }
        QSaveFile file(path);
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            file.write(QByteArray::fromStdString(starosc::reference_to_csv(r)));
            file.commit();
        }
    }
}

void MainWindow::exportSpectrumCsv()
{
    if (!lastSpectrum_) {
        statusBar()->showMessage(tr("Nothing to export yet"), 4000);
        return;
    }
    const QString path = QFileDialog::getSaveFileName(this, tr("Export Spectrum"), "spectrum.csv", tr("CSV files (*.csv)"));
    if (path.isEmpty())
        return;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("Export Spectrum"), tr("Cannot write %1").arg(path));
        return;
    }
    const starosc::SpectrumFrame& f = *lastSpectrum_;
    std::array<starosc::TraceUnit, starosc::kSpectrumSources> units;
    for (int src = 0; src < starosc::kSpectrumSources; ++src)
        units[static_cast<std::size_t>(src)] = starosc::trace_unit(spectrum_.unit, src, f.scope.math.operation);
    QTextStream out(&file);
    out << "frequency_hz";
    std::size_t bins = 0;
    for (int src = 0; src < starosc::kSpectrumSources; ++src)
        if (!f.power[src].empty()) {
            out << ',' << (src < starosc::kChannels ? QString("ch%1").arg(src + 1) : QStringLiteral("math")) << '_'
                << QString::fromStdString(units[static_cast<std::size_t>(src)].column);
            bins = f.power[src].size();
        }
    out << "\n";
    for (std::size_t k = 0; k < bins; ++k) {
        out << QString::number(static_cast<double>(k) * f.bin_hz, 'g', 10);
        for (int src = 0; src < starosc::kSpectrumSources; ++src)
            if (!f.power[src].empty())
                out << ',' << QString::number(
                                  starosc::level_in_unit(f.power[src][k], units[static_cast<std::size_t>(src)].unit), 'g', 7);
        out << "\n";
    }
    statusBar()->showMessage(tr("Exported %1 bins to %2").arg(bins).arg(path), 6000);
}

void MainWindow::exportCsv()
{
    if (spectrumShown()) {
        exportSpectrumCsv();
        return;
    }
    if (!lastFrame_) {
        statusBar()->showMessage(tr("Nothing to export yet"), 4000);
        return;
    }
    const QString path = QFileDialog::getSaveFileName(this, tr("Export Waveform"), "waveform.csv", tr("CSV files (*.csv)"));
    if (path.isEmpty())
        return;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("Export Waveform"), tr("Cannot write %1").arg(path));
        return;
    }
    const starosc::Frame& f = *lastFrame_;
    QTextStream out(&file);
    out << "time_s";
    for (int ch = 0; ch < starosc::kChannels; ++ch)
        if (!f.volts[ch].empty())
            out << ",ch" << ch + 1 << "_v";
    if (!f.math.empty())
        out << ",math_" << (f.settings.math.operation == starosc::MathOperation::Divide ? "ratio"
                            : f.settings.math.operation == starosc::MathOperation::Multiply ? "v2" : "v");
    out << "\n";
    const std::size_t n = f.plan.window_samples;
    for (std::size_t i = 0; i < n; ++i) {
        const double t = (static_cast<double>(i) - f.start_fraction - static_cast<double>(f.plan.pre_samples)) *
                         f.sample_interval_s;
        out << QString::number(t, 'g', 10);
        for (int ch = 0; ch < starosc::kChannels; ++ch)
            if (!f.volts[ch].empty())
                out << ',' << (i < f.volts[ch].size() ? QString::number(f.volts[ch][i], 'g', 7) : QString());
        if (!f.math.empty())
            out << ',' << (i < f.math.size() && std::isfinite(f.math[i]) ? QString::number(f.math[i], 'g', 7) : QString());
        out << "\n";
    }
    statusBar()->showMessage(tr("Exported %1 samples to %2").arg(n).arg(path), 6000);
}

void MainWindow::saveScreenshot()
{
    const QString path = QFileDialog::getSaveFileName(this, tr("Save Screenshot"), "star-oscilloscope.png", tr("PNG images (*.png)"));
    QWidget* display = spectrumShown() ? static_cast<QWidget*>(spectrumView_) : view_;
    if (!path.isEmpty() && !display->grab().save(path))
        QMessageBox::warning(this, tr("Save Screenshot"), tr("Cannot write %1").arg(path));
}

void MainWindow::about()
{
    QMessageBox box(this);
    box.setObjectName("aboutBox");
    box.setWindowTitle(tr("About Star Oscilloscope"));
    // The name, what it is for, and the licence in small print.
    box.setText(tr("<h3>Star Oscilloscope %1</h3>"
                   "<p>Open-source oscilloscope software for the Instrustar ISDS205 series, working through its own "
                   "USB driver.</p>")
                    .arg(QApplication::applicationVersion()));
    box.setInformativeText(tr("<small>Copyright © 2026 Necati Çakacı.<br>"
                              "GNU General Public License, version 3 or later. "
                              "This program comes with ABSOLUTELY NO WARRANTY.</small>"));
    box.setIconPixmap(app_icon::icon().pixmap(QSize(64, 64), devicePixelRatio()));
    box.exec();
}

void MainWindow::screenshotAfterFrames(const QString& path, int frames)
{
    screenshotPath_ = path;
    screenshotFrames_ = std::max(1, frames);
    QTimer::singleShot(20000, this, [this] {
        grab().save(screenshotPath_);
        QApplication::quit();
    });
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (screenshotPath_.isEmpty()) {
        QSettings saved;
        saved.setValue("window/geometry", saveGeometry());
        saved.setValue("window/state", saveState(kLayoutVersion));
        settings_store::save(saved, settings_, cursors_);
        settings_store::saveGenerator(saved, generator_, parkGenerator_);
        settings_store::saveSpectrum(saved, spectrum_);
        settings_store::saveMeasurements(saved, measurements_->selection());
        saved.setValue("measure/columns", measurements_->columns());
        display_style::save(saved);
        saved.setValue("display/dots", dotsAction_->isChecked());
        saved.setValue("window/tab", spectrumShown() ? "spectrum" : "oscilloscope");
        saved.setValue("window/controlPanel", frontPanelAction_->isChecked() ? "front" : "form");
        saved.setValue("display/interpolation", interpolationAction_->isChecked());
        keepReferences();
    }
    QMainWindow::closeEvent(event);
}
