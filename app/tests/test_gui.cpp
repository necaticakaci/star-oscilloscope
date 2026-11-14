// SPDX-License-Identifier: GPL-3.0-or-later
// Display interaction and settings persistence, run off-screen.
#include <algorithm>
#include <cmath>
#include <numbers>
#include <tuple>
#include <utility>

#include <QSet>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QWheelEvent>
#include <QtTest>

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QDir>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QPushButton>
#include <QFileInfo>
#include <QGroupBox>
#include <QHeaderView>
#include <QKeySequence>
#include <QLabel>
#include <QLibraryInfo>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QProgressBar>
#include <QRadioButton>
#include <QStatusBar>
#include <QFile>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QToolBar>
#include <QRegularExpression>
#include <QTextDocument>
#include <QXmlStreamReader>
#include <QTimer>
#include <QToolButton>
#include <QToolTip>
#include <QTranslator>

#include "calibrationdialog.hpp"
#include "calibrationstore.hpp"
#include "columnsdialog.hpp"
#include "controlpanel.hpp"
#include "displaystyle.hpp"
#include "frontpanel.hpp"
#include "gainwizard.hpp"
#include "acquisitionworker.hpp"
#include "appicon.hpp"
#include "language.hpp"
#include "generatorpanel.hpp"
#include "icons.hpp"
#include "knob.hpp"
#include "tracecolorsdialog.hpp"
#include "generatorstrip.hpp"
#include "mainwindow.hpp"
#include "measurementdialog.hpp"
#include "measurementtable.hpp"
#include "overviewstrip.hpp"
#include "scopeview.hpp"
#include "settingsstore.hpp"
#include "theme.hpp"
#include "sispinbox.hpp"
#include "spectrumpanel.hpp"
#include "spectrumtable.hpp"
#include "spectrumview.hpp"
#include "starosc/reference.hpp"
#include "starosc/source.hpp"

namespace {

starosc::ScopeSettings defaults()
{
    starosc::ScopeSettings s;
    s.channel[0] = {true, 1.0, 1.0, starosc::Coupling::DC, 1.0};
    s.channel[1] = {true, 0.5, -2.0, starosc::Coupling::DC, 1.0};
    s.trigger.level_v = 1.0;
    return s;
}

void mouse(QWidget* w, QEvent::Type type, QPointF pos, Qt::MouseButtons buttons = Qt::LeftButton)
{
    const Qt::MouseButton button = type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton;
    QMouseEvent event(type, pos, w->mapToGlobal(pos), button, buttons, Qt::NoModifier);
    QApplication::sendEvent(w, &event);
}

void drag(QWidget* w, QPointF from, QPointF to)
{
    mouse(w, QEvent::MouseButtonPress, from);
    mouse(w, QEvent::MouseMove, (from + to) / 2);
    mouse(w, QEvent::MouseMove, to);
    mouse(w, QEvent::MouseButtonRelease, to, Qt::NoButton);
}

void wheel(QWidget* w, QPointF pos, int notches, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
{
    QWheelEvent event(pos, w->mapToGlobal(pos), QPoint(), QPoint(0, 120 * notches), Qt::NoButton, modifiers,
                      Qt::NoScrollPhase, false);
    QApplication::sendEvent(w, &event);
}

void doubleClick(QWidget* w, QPointF pos)
{
    QMouseEvent event(QEvent::MouseButtonDblClick, pos, w->mapToGlobal(pos), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(w, &event);
}

// 1 ms/div on the screen, with a record of 1000 samples: -1 V in the left half, +1 V in the right one.
std::shared_ptr<starosc::Frame> stepFrame()
{
    auto frame = std::make_shared<starosc::Frame>();
    frame->settings = defaults();
    frame->settings.timebase.seconds_per_div = 1e-3;
    frame->plan.window_samples = 1000;
    frame->plan.sample_rate_hz = 100'000;
    frame->sample_interval_s = 10e-6;
    for (int i = 0; i <= 1000; ++i)
        frame->volts[0].push_back(i < 500 ? -1.0f : 1.0f);
    frame->overview.window_begin = 0.25;
    frame->overview.window_end = 0.75;
    frame->overview.min[0].assign(400, -1.0f);
    frame->overview.max[0].assign(400, 1.0f);
    return frame;
}

}  // namespace

class GuiTest : public QObject {
    Q_OBJECT

private:
    std::unique_ptr<ScopeView> view_;
    double div_ = 0;  // pixels per vertical division
    QTemporaryDir config_;  // default QSettings location, so the user's own files are never touched

    static starosc::UserCalibration calibrated()
    {
        starosc::UserCalibration cal = starosc::make_simulator()->factory_calibration();
        cal.stages[2].channel[1] = {-0.0139, 0.9421};
        cal.stages[6].channel[0] = {-0.1103, 0.9336};
        cal.zero_time = "2026-11-01T18:20:00";
        cal.gain_time = "2026-11-01T20:45:00";
        return cal;
    }

private slots:
    void initTestCase()
    {
        QVERIFY(config_.isValid());
        QCoreApplication::setOrganizationName("StarOscilloscopeTest");
        QCoreApplication::setApplicationName("StarOscilloscopeTest");
        settings_store::useFiles();
        settings_store::useFolder(config_.path());
        QVERIFY(calibration_store::pathFor("X").startsWith(config_.path()));
        QVERIFY(QFileInfo(QSettings().fileName()).absolutePath().startsWith(config_.path()));  // a file, also on Windows
    }

    void init()
    {
        view_ = std::make_unique<ScopeView>();
        view_->resize(656, 534);
        view_->setSettings(defaults());
        div_ = view_->plotRect().height() / starosc::kDivisionsY;
    }

    void dragChannelMarker()
    {
        QSignalSpy spy(view_.get(), &ScopeView::channelPositionChanged);
        const QPointF from = view_->markerPosition(ScopeView::Marker::Channel1);
        drag(view_.get(), from, from - QPointF(0, div_));
        QVERIFY(!spy.isEmpty());
        QCOMPARE(spy.last().at(0).toInt(), 0);
        QCOMPARE(spy.last().at(1).toDouble(), 2.0);
    }

    void dragTriggerLevel()
    {
        QSignalSpy spy(view_.get(), &ScopeView::triggerLevelChanged);
        const QPointF from = view_->markerPosition(ScopeView::Marker::TriggerLevel);
        drag(view_.get(), from, from + QPointF(0, div_ / 2));
        QVERIFY(!spy.isEmpty());
        QCOMPARE(spy.last().at(0).toDouble(), 0.5);  // CH1 at 1 V/div
    }

    void dragTriggerPosition()
    {
        QSignalSpy spy(view_.get(), &ScopeView::preTriggerChanged);
        const QPointF from = view_->markerPosition(ScopeView::Marker::TriggerPosition);
        drag(view_.get(), from, from - QPointF(view_->plotRect().width() * 0.3, 0));
        QVERIFY(!spy.isEmpty());
        QCOMPARE(spy.last().at(0).toDouble(), 0.2);
    }

    void arrowShowsTraceOffScreen()
    {
        auto frame = std::make_shared<starosc::Frame>();
        frame->measurements[0].min = 2.49;  // CH1: 1 V/div, +1 div, so the screen spans -5..3 V
        frame->measurements[0].max = 2.51;
        frame->measurements[1].min = -1.51;  // CH2: 0.5 V/div, -2 div, so the screen spans -1..3 V
        frame->measurements[1].max = -1.49;
        view_->setFrame(frame);
        QCOMPARE(view_->offScreen(0), ScopeView::OffScreen::No);
        QCOMPARE(view_->offScreen(1), ScopeView::OffScreen::Below);
        QVERIFY(!view_->grab().isNull());  // paints the arrow

        starosc::ScopeSettings s = defaults();
        s.channel[0].position_div = 2.72;  // screen top at 1.28 V, the case seen on the bench
        s.channel[1].position_div = 0;
        view_->setSettings(s);
        QCOMPARE(view_->offScreen(0), ScopeView::OffScreen::Above);
        QCOMPARE(view_->offScreen(1), ScopeView::OffScreen::No);

        s.channel[0].enabled = false;
        s.channel[1].position_div = -2;
        frame->measurements[1].max = -0.5;  // partly on screen
        view_->setSettings(s);
        QCOMPARE(view_->offScreen(0), ScopeView::OffScreen::No);
        QCOMPARE(view_->offScreen(1), ScopeView::OffScreen::No);
    }

    void resetLayoutDocksPanels()
    {
        MainWindow window(true);
        auto* measurements = window.findChild<QDockWidget*>("measurementsDock");
        auto* controls = window.findChild<QDockWidget*>("controlsDock");
        auto* reset = window.findChild<QAction*>("resetLayoutAction");
        QVERIFY(measurements && controls && reset);
        measurements->setFloating(true);
        window.addDockWidget(Qt::LeftDockWidgetArea, controls);
        controls->close();

        reset->trigger();
        QVERIFY(!measurements->isFloating());
        QVERIFY(!controls->isFloating());
        QCOMPARE(window.dockWidgetArea(measurements), Qt::BottomDockWidgetArea);
        QCOMPARE(window.dockWidgetArea(controls), Qt::RightDockWidgetArea);
        QVERIFY(!measurements->isHidden());
        QVERIFY(!controls->isHidden());
    }

    void autosetReportsTheSimulatorSignals()
    {
        MainWindow window(true);
        auto* autoset = window.findChild<QAction*>("autosetAction");
        QVERIFY(autoset);
        autoset->trigger();
        QVERIFY(!autoset->isEnabled());
        QTRY_VERIFY_WITH_TIMEOUT(autoset->isEnabled(), 10000);
        QVERIFY2(window.statusBar()->currentMessage().startsWith("Autoset: CH1, CH2; trigger on CH1 at 1.000 kHz"),
                 qPrintable(window.statusBar()->currentMessage()));
    }

    void doubleClickResetsChannelPosition()
    {
        QSignalSpy spy(view_.get(), &ScopeView::channelPositionChanged);
        const QPointF at = view_->markerPosition(ScopeView::Marker::Channel2);
        mouse(view_.get(), QEvent::MouseButtonDblClick, at);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.last().at(0).toInt(), 1);
        QCOMPARE(spy.last().at(1).toDouble(), 0.0);
    }

    void wheelStepsTimebaseAndVoltsPerDiv()
    {
        view_->setRunState(ScopeView::RunState::Running);
        QSignalSpy timebase(view_.get(), &ScopeView::timebaseStepped);
        QSignalSpy volts(view_.get(), &ScopeView::voltsPerDivStepped);
        wheel(view_.get(), view_->plotRect().center(), 1);
        QCOMPARE(timebase.count(), 1);
        QCOMPARE(timebase.last().at(0).toInt(), -1);

        wheel(view_.get(), view_->markerPosition(ScopeView::Marker::Channel2), -2);
        QCOMPARE(volts.count(), 1);
        QCOMPARE(volts.last().at(0).toInt(), 1);
        QCOMPARE(volts.last().at(1).toInt(), 2);
    }

    void wheelZoomsWhenZoomedStoppedOrWithControl()
    {
        QSignalSpy timebase(view_.get(), &ScopeView::timebaseStepped);
        QSignalSpy zoom(view_.get(), &ScopeView::zoomStepped);
        const QRectF plot = view_->plotRect();
        const QPointF at(plot.left() + plot.width() * 0.25, plot.center().y());

        wheel(view_.get(), at, 1, Qt::ControlModifier);
        QCOMPARE(zoom.count(), 1);
        QCOMPARE(zoom.last().at(0).toInt(), -1);
        QCOMPARE(zoom.last().at(1).toDouble(), 0.25);

        view_->setRunState(ScopeView::RunState::Stopped);
        wheel(view_.get(), at, 1);
        QCOMPARE(zoom.count(), 2);
        view_->setRunState(ScopeView::RunState::Running);

        starosc::ScopeSettings s = defaults();
        s.zoom = {50e-6, 0.5};
        view_->setSettings(s);  // 500 µs/div main
        wheel(view_.get(), at, -2);
        QCOMPARE(zoom.count(), 3);
        QCOMPARE(zoom.last().at(0).toInt(), 2);
        QCOMPARE(timebase.count(), 0);
    }

    void dragInThePlotSelectsARangeToZoom()
    {
        QSignalSpy range(view_.get(), &ScopeView::zoomRangeSelected);
        const QRectF plot = view_->plotRect();
        const double y = plot.center().y() + 20;
        drag(view_.get(), {plot.left() + plot.width() * 0.5, y}, {plot.left() + plot.width() * 0.25, y});
        QCOMPARE(range.count(), 1);
        QCOMPARE(range.last().at(0).toDouble(), 0.5);
        QCOMPARE(range.last().at(1).toDouble(), 0.25);

        // A click is not a selection.
        drag(view_.get(), {plot.left() + 100, y}, {plot.left() + 103, y});
        QCOMPARE(range.count(), 1);
    }

    void dragMovesTheZoomedPart()
    {
        starosc::ScopeSettings s = defaults();
        s.timebase.seconds_per_div = 1e-3;
        s.zoom = {100e-6, 0.5};
        view_->setSettings(s);
        QSignalSpy center(view_.get(), &ScopeView::zoomCenterChanged);
        QSignalSpy range(view_.get(), &ScopeView::zoomRangeSelected);
        QSignalSpy off(view_.get(), &ScopeView::zoomOffRequested);
        const QRectF plot = view_->plotRect();
        const double y = plot.center().y() + 20;
        // A tenth of a screen to the right brings up what lies a tenth of a screen to the left: 1 % of the record.
        drag(view_.get(), {plot.left() + 100, y}, {plot.left() + 100 + plot.width() / 10, y});
        QVERIFY(!center.isEmpty());
        QCOMPARE(center.last().at(0).toDouble(), 0.49);
        QCOMPARE(range.count(), 0);

        doubleClick(view_.get(), {plot.left() + 100, y});
        QCOMPARE(off.count(), 1);
    }

    void triggerMarkerFollowsTheZoom()
    {
        starosc::ScopeSettings s = defaults();
        s.timebase.seconds_per_div = 1e-3;
        s.trigger.pre_trigger = 0.5;
        s.zoom = {100e-6, 0.48};  // 43 % .. 53 % of the record
        view_->setSettings(s);
        const QRectF plot = view_->plotRect();
        QCOMPARE(view_->markerPosition(ScopeView::Marker::TriggerPosition).x(), plot.left() + 0.7 * plot.width());

        // It only shows where the trigger is: dragging it would change the capture for a small part of the record.
        QSignalSpy pre(view_.get(), &ScopeView::preTriggerChanged);
        const QPointF from = view_->markerPosition(ScopeView::Marker::TriggerPosition);
        drag(view_.get(), from, from + QPointF(30, 0));
        QCOMPARE(pre.count(), 0);

        s.zoom.center = 0.2;  // the trigger lies to the right of the screen
        view_->setSettings(s);
        QCOMPARE(view_->markerPosition(ScopeView::Marker::TriggerPosition).x(), plot.right());
        QVERIFY(!view_->grab().isNull());
    }

    void zoomShowsThePartOfTheRecord()
    {
        const auto frame = stepFrame();
        starosc::ScopeSettings s = frame->settings;
        s.channel[0].position_div = 0;
        s.channel[1].enabled = false;
        const QRectF plot = view_->plotRect();
        const double div = plot.height() / starosc::kDivisionsY;
        const double above = plot.center().y() - div;  // +1 V at 1 V/div
        const double below = plot.center().y() + div;  // -1 V
        const auto traceAt = [&](const QImage& image, double y) {
            const int x = static_cast<int>(plot.left() + 0.3 * plot.width());
            for (int dy = -1; dy <= 1; ++dy)
                if (image.pixelColor(x, static_cast<int>(y) + dy).red() > 100)
                    return true;
            return false;
        };
        view_->setFrame(frame);

        s.zoom = {100e-6, 0.25};
        view_->setSettings(s);
        QImage image = view_->grab().toImage();
        QVERIFY(traceAt(image, below));
        QVERIFY(!traceAt(image, above));

        s.zoom.center = 0.75;
        view_->setSettings(s);
        image = view_->grab().toImage();
        QVERIFY(!traceAt(image, below));
        QVERIFY(traceAt(image, above));
    }

    void overviewStripSetsTheZoom()
    {
        OverviewStrip strip;
        strip.resize(402, 24);  // one pixel of margin on each side: 400 wide
        const auto frame = stepFrame();
        starosc::ScopeSettings s = frame->settings;
        strip.setFrame(frame);
        strip.setSettings(s);
        QSignalSpy range(&strip, &OverviewStrip::zoomRangeSelected);
        QSignalSpy center(&strip, &OverviewStrip::zoomCenterChanged);
        QSignalSpy stepped(&strip, &OverviewStrip::zoomStepped);
        QSignalSpy off(&strip, &OverviewStrip::zoomOffRequested);

        // Not zoomed, the strip is the capture and the record its middle half; a selection stops at the record.
        drag(&strip, {1 + 200, 12}, {1 + 250, 12});
        QCOMPARE(range.count(), 1);
        QCOMPARE(range.last().at(0).toDouble(), 0.5);
        QCOMPARE(range.last().at(1).toDouble(), 0.75);
        drag(&strip, {1 + 200, 12}, {1 + 390, 12});
        QCOMPARE(range.last().at(1).toDouble(), 1.0);
        doubleClick(&strip, {100, 12});
        QCOMPARE(off.count(), 0);

        // Zoomed, the strip is the record. Dragging inside the frame keeps the grip on it.
        s.zoom = {100e-6, 0.5};  // 45 % .. 55 %
        strip.setSettings(s);
        QVERIFY(!strip.grab().isNull());
        drag(&strip, {1 + 190, 12}, {1 + 230, 12});  // 47.5 % to 57.5 %: 10 % of the record
        QCOMPARE(center.last().at(0).toDouble(), 0.6);
        // Outside the frame, the centre jumps to the pointer.
        mouse(&strip, QEvent::MouseButtonPress, {1 + 100, 12});
        QCOMPARE(center.last().at(0).toDouble(), 0.25);
        mouse(&strip, QEvent::MouseButtonRelease, {1 + 100, 12}, Qt::NoButton);

        wheel(&strip, {100, 12}, 1);
        QCOMPARE(stepped.last().at(0).toInt(), -1);
        doubleClick(&strip, {100, 12});
        QCOMPARE(off.count(), 1);
    }

    void linesStayWholeDevicePixelsWhenScaled()
    {
        // On a screen scaled to 125 % a pen of one logical pixel is 1.25 device pixels and leaves Qt's fast line drawing:
        // the display took a whole core. The display's lines keep whole device pixels.
        for (const auto& [ratio, width] : {std::pair{1.0, 1.0}, std::pair{1.25, 1.0}, std::pair{1.5, 1.0}, std::pair{2.0, 2.0}}) {
            QImage image(10, 10, QImage::Format_ARGB32_Premultiplied);
            image.setDevicePixelRatio(ratio);
            const QPen pen = display_style::hairline(Qt::red, &image, Qt::DashLine);
            QVERIFY(pen.isCosmetic());
            QCOMPARE(pen.widthF(), width);
            QCOMPARE(pen.style(), Qt::DashLine);
        }
    }

    void overviewFrameStaysInTheMiddle()
    {
        // Wherever the trigger put the record in the capture, the frame stays; the capture moves around it.
        OverviewStrip strip;
        strip.resize(402, 24);
        auto early = stepFrame();
        early->overview.window_begin = 0.1;
        early->overview.window_end = 0.6;
        strip.setSettings(early->settings);
        strip.setFrame(early);
        const QRectF frame = strip.frameRect();
        QVERIFY2(std::abs(frame.center().x() - 201) < 1.5, qPrintable(QString::number(frame.center().x())));
        QCOMPARE(frame.width(), 200.0);
        // The record lay at 0.1 of that capture, and the frame begins at 0.25: the capture begins 0.15 into the strip.
        const auto drawnAt = [&](double at) {
            const QImage image = strip.grab().toImage();
            const int x = static_cast<int>(std::round(1 + at * 400));
            for (int y = 3; y < 21; ++y)
                if (image.pixelColor(x, y).red() > 100 && image.pixelColor(x, y).blue() < 100)  // CH1's yellow
                    return true;
            return false;
        };
        QVERIFY(!drawnAt(0.05));
        QVERIFY(drawnAt(0.95));

        auto late = std::make_shared<starosc::Frame>(*early);
        late->overview.window_begin = 0.4;
        late->overview.window_end = 0.9;
        strip.setFrame(late);
        QCOMPARE(strip.frameRect(), frame);
        QVERIFY(drawnAt(0.05));  // this record lay at 0.4: the capture ends 0.15 before the strip's end
        QVERIFY(!drawnAt(0.95));

        // A selection still maps onto the record.
        QSignalSpy range(&strip, &OverviewStrip::zoomRangeSelected);
        drag(&strip, {1 + 100, 12}, {1 + 200, 12});
        QCOMPARE(range.count(), 1);
        QCOMPARE(range.last().at(0).toDouble(), 0.0);
        QCOMPARE(range.last().at(1).toDouble(), 0.5);
    }

    void controlPanelOffersZoomSteps()
    {
        ControlPanel panel;
        starosc::ScopeSettings s = defaults();
        s.timebase.seconds_per_div = 1e-3;
        panel.setSettings(s);
        panel.setSampleRate(1e6);
        auto* scale = panel.findChild<QComboBox*>("zoomScale");
        auto* position = panel.findChild<QDoubleSpinBox*>("zoomPosition");
        QVERIFY(scale && position);
        QCOMPARE(scale->itemText(0), QString("Off"));
        QCOMPARE(scale->itemText(1), QString("500 µs/div"));
        QCOMPARE(scale->itemText(scale->count() - 1), QString("500 ns/div"));
        QCOMPARE(scale->currentIndex(), 0);
        QVERIFY(!position->isEnabled());

        QSignalSpy spy(&panel, &ControlPanel::settingsChanged);
        scale->setCurrentIndex(3);
        QCOMPARE(spy.count(), 1);
        auto got = spy.last().at(0).value<starosc::ScopeSettings>();
        QCOMPARE(got.zoom.seconds_per_div, 100e-6);
        QVERIFY(position->isEnabled());
        position->setValue(25);
        got = spy.last().at(0).value<starosc::ScopeSettings>();
        QCOMPARE(got.zoom.center, 0.25);

        // Another main timebase has its own list; X-Y has no zoom.
        s = got;
        s.timebase.seconds_per_div = 200e-6;
        panel.setSettings(s);
        QCOMPARE(scale->itemText(1), QString("100 µs/div"));
        s.display = starosc::DisplayMode::XY;
        panel.setSettings(s);
        QVERIFY(!scale->isEnabled());
        QVERIFY(!position->isEnabled());
    }

    void windowZoomsThroughItsDisplay()
    {
        MainWindow window(true);
        auto* view = window.findChild<ScopeView*>();
        auto* table = window.findChild<MeasurementTable*>();
        auto* scale = window.findChild<QComboBox*>("zoomScale");
        auto* off = window.findChild<QAction*>("zoomOffAction");
        QVERIFY(view && table && scale && off);
        view->resize(656, 534);
        QTRY_VERIFY_WITH_TIMEOUT(table->rowCount() > 0 && table->item(0, 7)->text().toInt() >= 2, 10000);
        QVERIFY(!off->isEnabled());

        wheel(view, view->plotRect().center(), 1, Qt::ControlModifier);
        QVERIFY(off->isEnabled());
        QVERIFY(scale->currentData().toDouble() > 0);
        QVERIFY(scale->currentData().toDouble() < 500e-6);

        // Zooming does not restart the statistics.
        const int count = table->item(0, 7)->text().toInt();
        QTRY_VERIFY_WITH_TIMEOUT(table->item(0, 7)->text().toInt() > count, 5000);

        off->trigger();
        QVERIFY(!off->isEnabled());
        QCOMPARE(scale->currentIndex(), 0);
    }

    void measurementTableShowsTheSelection()
    {
        MeasurementTable table;
        const auto cell = [&](int row, int column) { return table.item(row, column)->text().remove(QChar(QChar::Nbsp)); };
        auto frame = stepFrame();
        frame->volts[1] = frame->volts[0];
        frame->lsb_v = {0.01, 0.01};
        starosc::measure_frame(*frame);
        table.setFrame(*frame);
        QCOMPARE(table.rowCount(), 16);  // the defaults on both channels
        QCOMPARE(cell(0, 0), QString("CH1"));
        QCOMPARE(cell(0, 1), QString("Frequency"));
        QCOMPARE(cell(3, 1), QString("Peak-to-peak"));
        QCOMPARE(cell(3, 2), QString("2.000 V"));

        MeasurementSelection selection;
        selection.trace[0] = {starosc::Parameter::RiseTime, starosc::Parameter::Top};
        selection.pair = {starosc::Parameter::Delay, starosc::Parameter::Phase};
        table.setSelection(selection);
        table.setFrame(*frame);
        QCOMPARE(table.rowCount(), 4);
        // A step between two samples at 100 kSa/s cannot be timed.
        QCOMPARE(cell(0, 1), QString("Rise time"));
        QCOMPARE(cell(0, 2), QString("< 20.0 µs"));
        QCOMPARE(cell(0, 7), QString("0"));
        QCOMPARE(cell(1, 2), QString("1.000 V"));
        QCOMPARE(cell(1, 7), QString("1"));
        QCOMPARE(cell(2, 0), QString("CH1→CH2"));
        QCOMPARE(cell(2, 1), QString("Delay"));
        QCOMPARE(cell(2, 2), QString("0.000 s"));

        // Without CH2 there is nothing to compare.
        frame->volts[1].clear();
        table.setFrame(*frame);
        QCOMPARE(table.rowCount(), 2);
        QCOMPARE(table.selection().pair.size(), std::size_t{2});
    }

    void measurementColumnsAreChosenMovedAndKept()
    {
        const QStringList all = MeasurementTable::defaultColumns();
        QCOMPARE(all, QStringList({"Source", "Parameter", "Value", "Min", "Max", "Mean", "Std dev", "Count"}));
        MeasurementTable table;
        QHeaderView* header = table.horizontalHeader();
        QVERIFY(header->sectionsMovable());
        QCOMPARE(table.columns(), all);

        // Chosen and ordered by name, the others hidden. Parameter is always there; unknown names are skipped.
        table.setColumns({"Parameter", "Value", "Source", "Count"});
        QCOMPARE(table.columns(), QStringList({"Parameter", "Value", "Source", "Count"}));
        QCOMPARE(header->logicalIndex(0), 1);
        QVERIFY(table.isColumnHidden(3));  // Min
        table.setColumns({"Source", "mean", "Bogus"});
        QCOMPARE(table.columns(), QStringList({"Source", "Parameter", "Mean"}));

        // A heading dragged to the front.
        header->moveSection(header->visualIndex(5), 0);
        QCOMPARE(table.columns(), QStringList({"Mean", "Source", "Parameter"}));

        // The headings' menu: a check box for each column, Parameter's fixed.
        QMenu menu;
        table.addColumnActions(&menu);
        const auto action = [&](const QString& text) {
            for (QAction* a : menu.actions())
                if (a->text() == text)
                    return a;
            return static_cast<QAction*>(nullptr);
        };
        QVERIFY(action("Min") && action("Parameter") && action("Restore Default Columns") && action("Table Columns…"));
        QVERIFY(!action("Parameter")->isEnabled());
        QVERIFY(!action("Min")->isChecked());
        action("Min")->trigger();
        QCOMPARE(table.columns(), QStringList({"Mean", "Source", "Parameter", "Min"}));
        action("Restore Default Columns")->trigger();
        QCOMPARE(table.columns(), all);

        // The dialog: Count unchecked, Value moved to the top; Parameter cannot be unchecked.
        bool seen = false;
        QTimer::singleShot(0, &table, [&] {
            auto* dialog = table.findChild<ColumnsDialog*>();
            if (!dialog)
                return;
            seen = true;
            auto* list = dialog->findChild<QListWidget*>("columnList");
            auto* up = dialog->findChild<QPushButton*>("moveUp");
            list->item(7)->setCheckState(Qt::Unchecked);
            list->item(1)->setCheckState(Qt::Unchecked);
            list->setCurrentRow(2);
            up->click();
            up->click();
            seen = !up->isEnabled();  // at the top
            dialog->accept();
        });
        table.editColumns();
        QVERIFY(seen);
        QCOMPARE(table.columns(), QStringList({"Value", "Source", "Parameter", "Min", "Max", "Mean", "Std dev"}));

        // Kept in the settings by their English names, whatever the language.
        const QStringList chosen{"Parameter", "Mean", "Source"};
        {
            MainWindow window(true);
            window.findChild<MeasurementTable*>()->setColumns(chosen);
            window.close();
        }
        QCOMPARE(QSettings().value("measure/columns").toStringList(), chosen);
        {
            MainWindow window(true);
            QCOMPARE(window.findChild<MeasurementTable*>()->columns(), chosen);
            auto* columns = window.findChild<QAction*>("tableColumnsAction");
            QVERIFY(columns && columns->isEnabled());
            QVERIFY(window.showTab("spectrum"));
            QVERIFY(!columns->isEnabled());  // the spectrum's table is another one
            QVERIFY(window.showTab("oscilloscope"));
        }
        QSettings().remove("measure/columns");
    }

    void helpShowsInAStatusLineNotInATooltip()
    {
        MainWindow window(true);
        window.resize(1180, 760);
        window.show();
        QApplication::processEvents();
        QStatusBar* status = window.statusBar();
        const auto enter = [](QWidget* w) {
            QEnterEvent event(QPointF(2, 2), QPointF(2, 2), w->mapToGlobal(QPointF(2, 2)));
            QApplication::sendEvent(w, &event);
        };
        const auto leave = [](QWidget* w) {
            QEvent event(QEvent::Leave);
            QApplication::sendEvent(w, &event);
        };

        // A toolbar key shows no tooltip: it would only repeat its label.
        QToolButton* autoset = nullptr;
        for (auto* b : window.findChildren<QToolButton*>())
            if (b->defaultAction() && b->defaultAction()->text() == "&Autoset")
                autoset = b;
        QVERIFY(autoset);
        QHelpEvent tip(QEvent::ToolTip, QPoint(4, 4), autoset->mapToGlobal(QPoint(4, 4)));
        QApplication::sendEvent(autoset, &tip);
        QVERIFY(!QToolTip::isVisible());

        // A panel's box: its description in the status bar while the pointer is on it.
        QWidget* zoom = nullptr;
        for (auto* w : window.findChildren<QComboBox*>())
            if (w->statusTip().startsWith("Show a part of the record"))
                zoom = w;
        QVERIFY(zoom);
        enter(zoom);
        QCOMPARE(status->currentMessage(), zoom->statusTip());
        leave(zoom);
        QVERIFY(status->currentMessage().isEmpty());

        // A column heading of the measurement table.
        auto* header = window.findChild<MeasurementTable*>()->horizontalHeader();
        enter(header->viewport());
        const QPoint first(header->sectionViewportPosition(header->logicalIndex(0)) + 5, header->height() / 2);
        QMouseEvent move(QEvent::MouseMove, first, header->viewport()->mapToGlobal(first), Qt::NoButton, Qt::NoButton,
                         Qt::NoModifier);
        QApplication::sendEvent(header->viewport(), &move);
        QCOMPARE(status->currentMessage(), QString("Drag to move the column; right-click to choose the columns"));
        leave(header->viewport());
        QVERIFY(status->currentMessage().isEmpty());
        window.close();

        // A dialog has no status bar: its own help line.
        MeasurementDialog dialog(MeasurementSelection::defaults(), {});
        auto* line = dialog.findChild<QLabel*>("hoverHelp");
        QVERIFY(line);
        QCheckBox* phase = nullptr;
        for (auto* c : dialog.findChildren<QCheckBox*>())
            if (c->text() == "Phase")
                phase = c;
        QVERIFY(phase && !phase->statusTip().isEmpty());
        enter(phase);
        QCOMPARE(line->text(), phase->statusTip());
        leave(phase);
        QVERIFY(line->text().isEmpty());
        // The other dialogs have too little help for a line of their own.
        ColumnsDialog columns({{0, "Parameter", true, true}}, {});
        QVERIFY(!columns.findChild<QLabel*>("hoverHelp"));
    }

    void selectMeasurementsIsNotTheMeasurementsPanel()
    {
        // The dialog's entry and key say what they do; "Measurements" alone is the panel in the View menu.
        MainWindow window(true);
        auto* select = window.findChild<QAction*>("selectMeasurementsAction");
        QVERIFY(select);
        QCOMPARE(select->text(), QString("Select &Measurements…"));
        QCOMPARE(window.findChild<QDockWidget*>("measurementsDock")->toggleViewAction()->text(), QString("Measurements"));
    }

    void measurementDialogReturnsTheChoice()
    {
        MeasurementSelection selection;
        selection.trace[1] = {starosc::Parameter::Overshoot};
        selection.pair = {starosc::Parameter::Phase};
        MeasurementDialog dialog(selection, {20, 40, 70});
        QVERIFY(dialog.checkBox(starosc::Parameter::Overshoot, 1)->isChecked());
        QVERIFY(!dialog.checkBox(starosc::Parameter::Overshoot, 0)->isChecked());
        QVERIFY(dialog.checkBox(starosc::Parameter::Phase, -1)->isChecked());
        QCOMPARE(dialog.selection(), selection);
        QCOMPARE(dialog.thresholds().mid, 40.0);

        dialog.checkBox(starosc::Parameter::Frequency, 2)->setChecked(true);
        dialog.checkBox(starosc::Parameter::Phase, -1)->setChecked(false);
        const MeasurementSelection edited = dialog.selection();
        QCOMPARE(edited.trace[2].size(), std::size_t{1});
        QVERIFY(edited.trace[2][0] == starosc::Parameter::Frequency);
        QVERIFY(edited.pair.empty());

        // The thresholds cannot cross.
        dialog.midThreshold()->setValue(5);
        QCOMPARE(dialog.midThreshold()->value(), 21);
        QCOMPARE(dialog.lowThreshold()->maximum(), 20);
        QCOMPARE(dialog.highThreshold()->minimum(), 22);

        dialog.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::RestoreDefaults)->click();
        QCOMPARE(dialog.selection(), MeasurementSelection::defaults());
        QCOMPARE(dialog.thresholds().low, 10.0);
        QCOMPARE(dialog.thresholds().mid, 50.0);
        QCOMPARE(dialog.thresholds().high, 90.0);
    }

    void controlPanelTakesAFreeProbeFactor()
    {
        ControlPanel panel;
        starosc::ScopeSettings s = defaults();
        s.channel[0].volts_per_div = 1.0;
        s.trigger.source = 0;
        s.trigger.level_v = 1.0;
        panel.setSettings(s);
        auto* probe = panel.findChild<QComboBox*>("probe1");
        QVERIFY(probe);
        QSignalSpy spy(&panel, &ControlPanel::settingsChanged);
        const auto last = [&] { return spy.last().at(0).value<starosc::ScopeSettings>().channel[0]; };
        const auto type = [&](const QString& text) {
            probe->lineEdit()->setText(text);
            emit probe->lineEdit()->editingFinished();
        };
        QCOMPARE(probe->currentText(), QString("×1"));
        QCOMPARE(probe->count(), 3);

        // Typed, with a comma or a point. V/div stays a round step; the trigger stays at the same input voltage.
        type("10,15");
        QCOMPARE(spy.count(), 1);
        QCOMPARE(last().probe, 10.15);
        QCOMPARE(last().custom_probe, 10.15);
        QCOMPARE(last().volts_per_div, 10.0);
        QCOMPARE(spy.last().at(0).value<starosc::ScopeSettings>().trigger.level_v, 10.15);
        QCOMPARE(probe->currentText(), QString("×10.15"));
        QCOMPARE(probe->count(), 4);

        // Not a factor: nothing changes.
        type("abc");
        QCOMPARE(spy.count(), 1);
        QCOMPARE(probe->currentText(), QString("×10.15"));
        type("0");
        QCOMPARE(spy.count(), 1);

        // The typed factor stays on offer.
        probe->setCurrentIndex(probe->findData(1.0));
        QCOMPARE(spy.count(), 2);
        QCOMPARE(last().probe, 1.0);
        QCOMPARE(last().custom_probe, 10.15);
        QCOMPARE(last().volts_per_div, 1.0);
        probe->setCurrentIndex(probe->findData(10.15));
        QCOMPARE(last().probe, 10.15);

        type("x9.8");
        QCOMPARE(last().probe, 9.8);
        QCOMPARE(probe->count(), 4);
        QCOMPARE(probe->findData(10.15), -1);

        // The scale list follows, in round steps.
        auto* group = probe->parentWidget();
        const auto scales = group->findChildren<QComboBox*>();
        bool found = false;
        for (QComboBox* c : scales)
            found = found || c->findText("50 mV/div") >= 0;
        QVERIFY(found);
    }

    void traceColorsFollowTheBackground()
    {
        const QColor yellow = display_style::defaultColor(display_style::Ch1);
        QCOMPARE(display_style::channel(0), yellow);
        QCOMPARE(display_style::background(), QColor(Qt::black));
        QCOMPARE(display_style::textOn(yellow), QColor(Qt::black));

        display_style::setPrintTheme(true);
        QCOMPARE(display_style::background(), QColor(Qt::white));
        QVERIFY(display_style::channel(0).lightnessF() < 0.37);  // readable on white
        QVERIFY(qAbs(display_style::channel(0).hslHue() - yellow.hslHue()) <= 1);
        QCOMPARE(display_style::textOn(display_style::channel(1)), QColor(Qt::white));
        const QRectF plot = view_->plotRect();
        const QImage image = view_->grab().toImage();
        QCOMPARE(image.pixelColor(static_cast<int>(plot.left() + 7), static_cast<int>(plot.top() + 7)), QColor(Qt::white));
        display_style::setPrintTheme(false);
    }

    void traceColorsAreSaved()
    {
        QTemporaryDir dir;
        QSettings store(dir.filePath("t.ini"), QSettings::IniFormat);
        display_style::setChosenColor(display_style::Math, QColor("#123456"));
        display_style::setPrintTheme(true);
        display_style::save(store);
        display_style::setChosenColor(display_style::Math, QColor("#ffffff"));
        display_style::setPrintTheme(false);
        display_style::load(store);
        QCOMPARE(display_style::chosenColor(display_style::Math), QColor("#123456"));
        QVERIFY(display_style::printTheme());

        // Nothing saved, or nonsense: the defaults.
        QSettings empty(dir.filePath("empty.ini"), QSettings::IniFormat);
        empty.setValue("display/color/ch2", "not a colour");
        display_style::load(empty);
        QCOMPARE(display_style::chosenColor(display_style::Math), display_style::defaultColor(display_style::Math));
        QCOMPARE(display_style::chosenColor(display_style::Ch2), display_style::defaultColor(display_style::Ch2));
        QVERIFY(!display_style::printTheme());
    }

    void traceColorsDialogAppliesItsChoice()
    {
        TraceColorsDialog dialog;
        dialog.setColor(display_style::Ch1, Qt::red);
        QCOMPARE(display_style::chosenColor(display_style::Ch1), display_style::defaultColor(display_style::Ch1));  // not yet
        dialog.apply();
        QCOMPARE(display_style::chosenColor(display_style::Ch1), QColor(Qt::red));

        dialog.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::RestoreDefaults)->click();
        dialog.apply();
        QCOMPARE(display_style::chosenColor(display_style::Ch1), display_style::defaultColor(display_style::Ch1));
    }

    void barsShowStateAndMeasurements()
    {
        auto frame = stepFrame();
        frame->lsb_v = {0.01, 0.01};
        starosc::measure_frame(*frame);
        view_->setFrame(frame);
        const QStringList readouts = view_->measurementReadouts();
        QVERIFY2(readouts.contains("Vpp 2.000 V"), qPrintable(readouts.join(" | ")));
        QCOMPARE(readouts.size(), 2);  // frequency and peak-to-peak of the channel with samples
        QVERIFY(readouts.front().startsWith("Freq"));

        // A chosen parameter takes the room when frequency and peak-to-peak are not chosen.
        MeasurementSelection selection;
        selection.trace[0] = {starosc::Parameter::Top, starosc::Parameter::Rms, starosc::Parameter::Mean};
        view_->setMeasurementSelection(selection);
        QCOMPARE(view_->measurementReadouts().size(), 2);
        QVERIFY(view_->measurementReadouts().front().startsWith("Top"));
        QVERIFY(!view_->grab().isNull());
    }

    void iconFilesMatchTheDrawing()
    {
        // The files are made by tools/make_icon.cpp; after a change to the drawing they have to be made again.
        for (int size : app_icon::kPngSizes) {
            const QString path = QString(STAROSC_ICON_DIR "/icons/star-oscilloscope-%1.png").arg(size);
            const QImage file(path);
            QVERIFY2(!file.isNull(), qPrintable(path));
            const QImage drawn = app_icon::image(size).convertToFormat(QImage::Format_ARGB32);
            const QImage saved = file.convertToFormat(QImage::Format_ARGB32);
            QCOMPARE(saved.size(), drawn.size());
            double total = 0;
            int worst = 0;
            for (int y = 0; y < size; ++y)
                for (int x = 0; x < size; ++x) {
                    const QRgb a = drawn.pixel(x, y), b = saved.pixel(x, y);
                    for (int shift : {0, 8, 16, 24}) {
                        const int d = std::abs(int((a >> shift) & 0xff) - int((b >> shift) & 0xff));
                        total += d;
                        worst = std::max(worst, d);
                    }
                }
            QVERIFY2(total / (4.0 * size * size) < 1 && worst < 64,
                     qPrintable(QString("%1: mean %2, worst %3").arg(path).arg(total / (4.0 * size * size)).arg(worst)));
        }
        QFile ico(STAROSC_ICON_DIR "/star-oscilloscope.ico");
        QVERIFY(ico.open(QIODevice::ReadOnly));
        const QByteArray head = ico.read(6);
        QCOMPARE(head.left(4), QByteArray("\0\0\1\0", 4));
        QCOMPARE(int(head[4]), int(std::size(app_icon::kIcoSizes)));
    }

    void iconStaysClearWhenSmall()
    {
        const QImage small = app_icon::image(16);
        const QColor sparkle = small.pixelColor(11, 4);
        QVERIFY2(sparkle.blue() > 150 && sparkle.red() < 150, qPrintable(sparkle.name()));
        QCOMPARE(small.pixelColor(0, 0).alpha(), 255);  // the frame fills the small icon to its edge
        // A light LCD with a dark trace: the screen beside the sparkle, and the trace's first pixel.
        QVERIFY2(small.pixelColor(6, 4).lightness() > 150, qPrintable(small.pixelColor(6, 4).name()));
        QVERIFY2(small.pixelColor(2, 10).lightness() < 60, qPrintable(small.pixelColor(2, 10).name()));
        QVERIFY(app_icon::icon().availableSizes().contains(QSize(256, 256)));
    }

    void aboutShowsTheIcon()
    {
        MainWindow window(true);
        auto* about = window.findChild<QAction*>("aboutAction");
        QVERIFY(about);
        QPixmap shown;
        QString text;
        QString informative;
        QTimer::singleShot(0, &window, [&] {
            if (auto* box = window.findChild<QMessageBox*>("aboutBox")) {
                shown = box->iconPixmap();
                text = box->text();
                informative = box->informativeText();
                box->accept();
            }
        });
        about->trigger();
        QVERIFY(!shown.isNull());
        QCOMPARE(shown.deviceIndependentSize().toSize(), QSize(64, 64));
        QVERIFY2(text.contains("ISDS205 series") && text.contains("its own USB driver") && !text.contains("Tested"),
                 qPrintable(text));
        QVERIFY(informative.contains("GNU General Public License"));
    }

    void generatorWarnsOfASensitiveInputOnlyThen()
    {
        GeneratorPanel panel;
        starosc::GeneratorSettings g;
        g.amplitude_vpp = 1;
        panel.setSettings(g);
        auto* warnings = panel.findChild<QLabel*>("generatorWarnings");
        QVERIFY(warnings);
        QVERIFY(warnings->isHidden());  // 1 Vpp at 1 kHz, no input on a fine scale: nothing to say
        panel.setSensitiveInputs({true, false});
        QVERIFY(!warnings->isHidden());
        QCOMPARE(warnings->text(), QString("CH1 on a fine scale: the output is up to 20 % higher than set."));
        g.amplitude_vpp = 5;
        panel.setSettings(g);
        panel.setSensitiveInputs({true, true});
        QVERIFY2(warnings->text().startsWith("CH1 and CH2 on a fine scale: at this level"), qPrintable(warnings->text()));
        // The ranges went from the form into the boxes' help.
        QVERIFY(panel.findChild<SiSpinBox*>("amplitude")->statusTip().startsWith("0.08 – 9.7 Vpp"));
    }

    void generatorFollowsTheModel()
    {
        // Whether the Calibration window offers the gain wizard, which sets DC levels with the generator's offset.
        // The worker reports the generator before the factory calibration that enables the window.
        const auto wizardOffered = [](MainWindow& window) {
            auto* calibration = window.findChild<QAction*>("calibrationAction");
            if (!calibration || !QTest::qWaitFor([&] { return calibration->isEnabled(); }, 5000))
                return -1;
            int offered = -1;
            QTimer::singleShot(0, &window, [&] {
                if (auto* dialog = window.findChild<CalibrationDialog*>()) {
                    offered = dialog->findChild<QPushButton*>("gainWizard")->isVisibleTo(dialog) ? 1 : 0;
                    dialog->reject();
                }
            });
            calibration->trigger();
            return offered;
        };
        const auto generatorTab = [](MainWindow& window) {
            auto* tabs = window.findChild<QTabWidget*>();
            for (int i = 0; i < tabs->count(); ++i)
                if (tabs->tabText(i) == "Generator")
                    return tabs->isTabEnabled(i);
            return false;
        };
        {
            // A plain ISDS205: no generator, no gain wizard.
            MainWindow window(true, "ISDS205");
            QCOMPARE(wizardOffered(window), 0);
            QVERIFY(!generatorTab(window));
        }
        {
            // ISDS205B: a generator without amplitude and offset, and the wizard needs them.
            MainWindow window(true, "ISDS205B");
            QCOMPARE(wizardOffered(window), 0);
            QVERIFY(generatorTab(window));
            auto* panel = window.findChild<GeneratorPanel*>();
            QVERIFY(panel->findChild<QLabel*>("noLevels")->isVisibleTo(panel));
            QVERIFY(!panel->findChild<SiSpinBox*>("amplitude")->isEnabled());
            QVERIFY(!panel->findChild<SiSpinBox*>("offset")->isEnabled());
            auto* strip = window.findChild<GeneratorStrip*>();
            QVERIFY(!strip->findChild<SiSpinBox*>("amplitude")->isEnabled());
            QVERIFY(!strip->findChild<SiSpinBox*>("offset")->isEnabled());
        }
        {
            // ISDS205X: all of it.
            MainWindow window(true);
            QCOMPARE(wizardOffered(window), 1);
            QVERIFY(generatorTab(window));
            auto* panel = window.findChild<GeneratorPanel*>();
            QVERIFY(!panel->findChild<QLabel*>("noLevels")->isVisibleTo(panel));
            QVERIFY(panel->findChild<SiSpinBox*>("amplitude")->isEnabled());
            QVERIFY(panel->findChild<SiSpinBox*>("offset")->isEnabled());
        }
    }

    void measurementsChangeAFewTimesASecond()
    {
        auto first = stepFrame();
        first->lsb_v = {0.01, 0.01};
        starosc::measure_frame(*first);
        auto second = std::make_shared<starosc::Frame>(*first);
        for (float& v : second->volts[0])
            v *= 0.5f;
        starosc::measure_frame(*second);

        QElapsedTimer clock;
        clock.start();
        view_->setFrame(first);
        view_->setFrame(second);  // too soon after the first to show
        QVERIFY(view_->measurementReadouts().contains("Vpp 2.000 V"));
        QTRY_VERIFY_WITH_TIMEOUT(view_->measurementReadouts().contains("Vpp 1.000 V"), 2000);
        QVERIFY(clock.elapsed() >= 300);

        // The table counts every frame in the statistics but shows them at the same pace.
        MeasurementTable table;
        const auto cell = [&](int row, int column) { return table.item(row, column)->text().remove(QChar(QChar::Nbsp)); };
        table.setFrame(*first);
        table.setFrame(*second);
        QCOMPARE(cell(3, 1), QString("Peak-to-peak"));
        QCOMPARE(cell(3, 2), QString("2.000 V"));
        QCOMPARE(cell(3, 7), QString("1"));
        QTRY_COMPARE_WITH_TIMEOUT(cell(3, 2), QString("1.000 V"), 2000);
        QCOMPARE(cell(3, 7), QString("2"));
        QCOMPARE(cell(3, 3), QString("1.000 V"));  // min
        QCOMPARE(cell(3, 4), QString("2.000 V"));  // max

        // A reset shows at once, also with no frame coming.
        table.resetStatistics();
        QCOMPARE(cell(3, 7), QString("0"));
    }

    void channelBadgeSaysWhyItWarns()
    {
        auto frame = stepFrame();
        view_->setFrame(frame);
        QVERIFY(!view_->grab().isNull());
        const QPointF onBadge(30, view_->height() - 15);
        QVERIFY(view_->warningAt(onBadge).isEmpty());
        frame->clipped[0] = true;
        QVERIFY(!view_->grab().isNull());
        QVERIFY2(view_->warningAt(onBadge).contains("input range"), qPrintable(view_->warningAt(onBadge)));
        QVERIFY(view_->warningAt(view_->plotRect().center()).isEmpty());
        frame->clipped[0] = false;
        frame->aliased[0] = true;
        QVERIFY(!view_->grab().isNull());
        QVERIFY(view_->warningAt(onBadge).contains("half the sample rate"));
    }

    void memoryStripSitsInTheBar()
    {
        OverviewStrip strip(view_.get());
        view_->setOverview(&strip);
        const QRectF plot = view_->plotRect();
        QVERIFY(strip.geometry().bottom() < plot.top());
        QVERIFY(strip.geometry().left() > 0);
        QVERIFY(strip.geometry().right() < view_->width());
        const int wide = strip.width();

        // The zoom's text takes room from it.
        starosc::ScopeSettings s = defaults();
        s.timebase.seconds_per_div = 1e-3;
        s.zoom = {100e-6, 0.5};
        view_->setSettings(s);
        QVERIFY(strip.width() < wide);

        // It has no use in X-Y.
        s.display = starosc::DisplayMode::XY;
        view_->setSettings(s);
        QVERIFY(strip.isHidden());
        view_->setOverview(nullptr);
    }

    void dotsDrawOnlyTheSamples()
    {
        auto frame = std::make_shared<starosc::Frame>();
        frame->settings = defaults();
        frame->plan.window_samples = 10;
        frame->volts[0].assign(11, 0.0f);
        starosc::ScopeSettings s = frame->settings;
        s.channel[0].position_div = 0;
        s.channel[1].enabled = false;
        view_->setSettings(s);
        view_->setFrame(frame);
        const QRectF plot = view_->plotRect();
        const int between = static_cast<int>(plot.left() + 0.25 * plot.width() + 0.05 * plot.width() / 2);  // not on a sample
        const auto traced = [&] {
            const QImage image = view_->grab().toImage();
            const int y = static_cast<int>(plot.center().y());
            for (int dy = -2; dy <= 2; ++dy)
                if (image.pixelColor(between, y + dy).red() > 150)
                    return true;
            return false;
        };
        QVERIFY(traced());
        view_->setDots(true);
        QVERIFY(!traced());
        view_->setDots(false);
    }

    void toolbarHasIconsBesideItsText()
    {
        MainWindow window(true);
        auto* bar = window.findChild<QToolBar*>("acquisitionToolBar");
        QVERIFY(bar);
        QVERIFY(bar->toolButtonStyle() == Qt::ToolButtonTextBesideIcon);
        for (QAction* action : bar->actions())
            QVERIFY(action->isSeparator() || !action->icon().isNull() || action->text().isEmpty());
        for (const QIcon& icon : {icons::run(), icons::stop(), icons::single(), icons::forceTrigger(), icons::autoset(),
                                  icons::cursors(), icons::measurements(), icons::resetStatistics(), icons::generator(),
                                  icons::exportData(), icons::screenshot(), icons::resetLayout()}) {
            const QImage image = icon.pixmap(16, 16).toImage();
            QVERIFY(!image.isNull());
            bool drawn = false;
            for (int y = 0; y < image.height() && !drawn; ++y)
                for (int x = 0; x < image.width() && !drawn; ++x)
                    drawn = image.pixelColor(x, y).alpha() > 0;
            QVERIFY(drawn);
        }
    }

    void iconsDrawOpenLinesInThePaletteInk()
    {
        struct PaletteGuard {
            QPalette saved = QGuiApplication::palette();
            ~PaletteGuard() { QApplication::setPalette(saved); }
        } guard;
        QPalette light = guard.saved;
        light.setColor(QPalette::ButtonText, Qt::black);
        light.setColor(QPalette::Base, Qt::white);
        QPalette dark = guard.saved;
        dark.setColor(QPalette::ButtonText, QColor(0xf0, 0xf0, 0xf0));
        dark.setColor(QPalette::Base, QColor(0x24, 0x24, 0x24));
        dark.setColor(QPalette::HighlightedText, Qt::black);

        // The darkest and the lightest of the clearly drawn pixels.
        const auto inkRange = [](const QIcon& icon, QSize size, QIcon::Mode mode = QIcon::Normal) {
            const QImage image = icon.pixmap(size, 1.0, mode).toImage().convertToFormat(QImage::Format_ARGB32);
            int darkest = 256;
            int lightest = -1;
            for (int y = 0; y < image.height(); ++y)
                for (int x = 0; x < image.width(); ++x)
                    if (const QColor c = image.pixelColor(x, y); c.alpha() > 160) {
                        darkest = std::min(darkest, c.lightness());
                        lightest = std::max(lightest, c.lightness());
                    }
            return std::pair{darkest, lightest};
        };

        // The generator's waveform icons follow the palette, also after it changes, like the toolbar icons.
        for (int i = 0; i < starosc::kWaveforms; ++i) {
            const QIcon icon = generator_ui::waveformIcon(static_cast<starosc::Waveform>(i));
            QCOMPARE(icon.actualSize(QSize(16, 16)), QSize(16, 8));  // keeps its shape
            QApplication::setPalette(light);
            const int lightest = inkRange(icon, QSize(56, 28)).second;
            QVERIFY2(lightest >= 0 && lightest < 60, qPrintable(QString("waveform %1 on light: %2").arg(i).arg(lightest)));
            QApplication::setPalette(dark);
            const int darkest = inkRange(icon, QSize(56, 28)).first;
            QVERIFY2(darkest > 200 && darkest < 256, qPrintable(QString("waveform %1 on dark: %2").arg(i).arg(darkest)));
            QVERIFY(inkRange(icon, QSize(56, 28), QIcon::Selected).second < 60);  // a selected item's text colour
        }
        QVERIFY(inkRange(icons::exportData(), QSize(32, 32)).first > 200);

        // The framed icons are outlines: their inside shows what is behind them. Points on a 16-unit canvas.
        const std::tuple<const char*, QIcon, QPointF> insides[] = {
            {"measurements", icons::measurements(), {8, 9}}, {"generator", icons::generator(), {12.5, 10.5}},
            {"exportData", icons::exportData(), {8, 13}},    {"screenshot", icons::screenshot(), {4, 11}},
            {"screenshot lens", icons::screenshot(), {8, 9}}, {"resetLayout", icons::resetLayout(), {6, 6}}};
        for (const auto& [name, icon, at] : insides) {
            const QImage image = icon.pixmap(QSize(32, 32), 1.0).toImage().convertToFormat(QImage::Format_ARGB32);
            QVERIFY2(image.pixelColor(int(at.x() * 2), int(at.y() * 2)).alpha() == 0, name);
        }
    }

    void measurementSettingsRoundTrip()
    {
        QTemporaryDir dir;
        QSettings store(dir.filePath("t.ini"), QSettings::IniFormat);
        MeasurementSelection untouched = MeasurementSelection::defaults();
        settings_store::loadMeasurements(store, untouched);
        QCOMPARE(untouched, MeasurementSelection::defaults());

        MeasurementSelection s;
        s.trace[0] = {starosc::Parameter::Frequency, starosc::Parameter::RiseTime};
        s.trace[2] = {starosc::Parameter::Mean};
        s.pair = {starosc::Parameter::Delay};
        settings_store::saveMeasurements(store, s);
        store.sync();
        MeasurementSelection loaded = MeasurementSelection::defaults();
        settings_store::loadMeasurements(store, loaded);
        QCOMPARE(loaded, s);  // an empty list stays empty

        store.setValue("measure/ch2", QStringList{"Period", "No such thing"});
        settings_store::loadMeasurements(store, loaded);
        QCOMPARE(loaded.trace[1].size(), std::size_t{1});
        QVERIFY(loaded.trace[1][0] == starosc::Parameter::Period);
    }

    void dragTimeCursor()
    {
        starosc::CursorSettings c;
        c.mode = starosc::CursorMode::Time;
        c.x = {0.3, 0.7};
        view_->setCursors(c);
        QSignalSpy spy(view_.get(), &ScopeView::cursorsChanged);
        const QPointF from = view_->markerPosition(ScopeView::Marker::Cursor1);
        drag(view_.get(), from, from + QPointF(view_->plotRect().width() * 0.1, 0));
        QVERIFY(!spy.isEmpty());
        const auto moved = spy.last().at(0).value<starosc::CursorSettings>();
        QVERIFY(std::abs(moved.x[0] - 0.4) < 1e-9);
        QCOMPARE(moved.x[1], 0.7);
    }

    void cursorsAreNotGrabbedWhenOff()
    {
        QSignalSpy spy(view_.get(), &ScopeView::cursorsChanged);
        const QPointF at(view_->plotRect().left() + 0.3 * view_->plotRect().width(), view_->plotRect().center().y());
        drag(view_.get(), at, at + QPointF(40, 0));
        QCOMPARE(spy.count(), 0);
    }

    void siSpinBoxReadsAndShowsPrefixes()
    {
        QCOMPARE(SiSpinBox::parse("1,5k", "Hz").value(), 1500.0);
        QCOMPARE(SiSpinBox::parse("2 MHz", "Hz").value(), 2e6);
        QCOMPARE(SiSpinBox::parse("300 mV", "V").value(), 0.3);
        QCOMPARE(SiSpinBox::parse("-1.2", "V").value(), -1.2);
        QCOMPARE(SiSpinBox::parse("5u", "V").value(), 5e-6);
        QVERIFY(!SiSpinBox::parse("abc", "V"));
        QVERIFY(!SiSpinBox::parse("", "Hz"));

        SiSpinBox hz("Hz", 1);
        hz.setRange(1, 20e6);
        hz.setValue(1000);
        QCOMPARE(hz.text(), QString("1.000 kHz"));
        hz.setValue(12.6e6);
        QCOMPARE(hz.text(), QString("12.600000 MHz"));
        hz.setStepsOf125(true);
        hz.setValue(1000);
        hz.stepBy(1);
        QCOMPARE(hz.value(), 2000.0);

        SiSpinBox volts("V", 0.001);
        volts.setRange(-5, 5);
        volts.setValue(0.25);
        QCOMPARE(volts.text(), QString("250 mV"));
        volts.setValue(-1.48);
        QCOMPARE(volts.text(), QString("-1.480 V"));
    }

    void generatorPanelFollowsTheWaveform()
    {
        GeneratorPanel panel;
        QSignalSpy spy(&panel, &GeneratorPanel::settingsChanged);
        auto* duty = panel.findChildren<QSpinBox*>().value(0);
        QVERIFY(duty);
        QVERIFY(!duty->isEnabled());

        auto* square = panel.findChildren<QToolButton*>().value(1);
        QVERIFY(square);
        square->click();
        QCOMPARE(spy.count(), 1);
        const auto g = spy.last().at(0).value<starosc::GeneratorSettings>();
        QVERIFY(g.waveform == starosc::Waveform::Square);
        QVERIFY(duty->isEnabled());

        starosc::GeneratorSettings fast = g;
        fast.frequency_hz = 10e6;  // above what a square wave can do
        panel.setSettings(fast);
        QCOMPARE(panel.settings().frequency_hz, 2e6);
        QCOMPARE(spy.count(), 1);  // setSettings does not echo
    }

    void generatorTabAndStripShareSettings()
    {
        MainWindow window(true);
        auto* panel = window.findChild<GeneratorPanel*>();
        auto* strip = window.findChild<GeneratorStrip*>();
        QVERIFY(panel && strip);
        starosc::GeneratorSettings g;
        g.output = true;
        g.waveform = starosc::Waveform::Triangle;
        g.frequency_hz = 2500;
        g.amplitude_vpp = 2;
        emit panel->settingsChanged(g);
        QVERIFY(strip->settings() == g);
        g.offset_v = -1;
        emit strip->settingsChanged(g);
        QVERIFY(panel->settings() == g);
    }

    void generatorSettingsRoundTrip()
    {
        QTemporaryDir dir;
        QSettings store(dir.filePath("g.ini"), QSettings::IniFormat);
        starosc::GeneratorSettings g;
        g.output = true;
        g.waveform = starosc::Waveform::RampDown;
        g.frequency_hz = 12345;
        g.amplitude_vpp = 2.5;
        g.offset_v = -0.75;
        g.duty_percent = 20;
        g.sweep = {true, 50, 5000, 2.5, false};
        settings_store::saveGenerator(store, g, false);

        starosc::GeneratorSettings loaded;
        bool park = true;
        settings_store::loadGenerator(store, loaded, park);
        QVERIFY(loaded == g);
        QVERIFY(!park);

        store.setValue("generator/amplitude", 500);  // out of range keeps the default
        starosc::GeneratorSettings fallback;
        settings_store::loadGenerator(store, fallback, park);
        QCOMPARE(fallback.amplitude_vpp, starosc::GeneratorSettings{}.amplitude_vpp);
    }

    void controlPanelSetsMathAndDisplay()
    {
        ControlPanel panel;
        QSignalSpy spy(&panel, &ControlPanel::settingsChanged);
        QGroupBox* math = nullptr;
        for (auto* box : panel.findChildren<QGroupBox*>())
            if (box->title() == "Math")
                math = box;
        QVERIFY(math);
        math->setChecked(true);
        QVERIFY(!spy.isEmpty());
        auto s = spy.last().at(0).value<starosc::ScopeSettings>();
        QVERIFY(s.math.enabled);
        QVERIFY(s.math.operation == starosc::MathOperation::Subtract);

        QComboBox* display = nullptr;
        for (auto* c : panel.findChildren<QComboBox*>())
            if (c->count() == 2 && c->itemText(0) == "Y-T")
                display = c;
        QVERIFY(display);
        display->setCurrentIndex(1);
        s = spy.last().at(0).value<starosc::ScopeSettings>();
        QVERIFY(s.display == starosc::DisplayMode::XY);
    }

    void xyModeDrawsAndIgnoresTheTimebaseWheel()
    {
        auto frame = std::make_shared<starosc::Frame>();
        for (int i = 0; i < 200; ++i) {
            frame->volts[0].push_back(static_cast<float>(std::sin(i * 0.1)));
            frame->volts[1].push_back(static_cast<float>(std::cos(i * 0.1)));
        }
        frame->plan.window_samples = 200;
        starosc::ScopeSettings s = defaults();
        s.display = starosc::DisplayMode::XY;
        view_->setSettings(s);
        view_->setFrame(frame);
        QVERIFY(!view_->grab().isNull());
        QSignalSpy timebase(view_.get(), &ScopeView::timebaseStepped);
        wheel(view_.get(), view_->plotRect().center(), 1);
        QCOMPARE(timebase.count(), 0);
    }

    void xyFigureMovesAndCentresOnTheScreen()
    {
        auto frame = std::make_shared<starosc::Frame>();
        frame->settings = defaults();
        frame->settings.display = starosc::DisplayMode::XY;
        frame->sample_interval_s = 10e-6;
        frame->lsb_v = {0.01, 0.01};
        for (int i = 0; i < 400; ++i) {
            frame->volts[0].push_back(static_cast<float>(1 + std::sin(i * 0.1)));  // around +1 V
            frame->volts[1].push_back(static_cast<float>(std::cos(i * 0.1)));      // around 0 V
        }
        frame->plan.window_samples = 400;
        starosc::measure_frame(*frame);
        starosc::ScopeSettings s = frame->settings;
        s.channel[0].position_div = 0;
        s.channel[1].position_div = 0;
        view_->setSettings(s);
        view_->setFrame(frame);
        QVERIFY(!view_->grab().isNull());

        QSignalSpy moved(view_.get(), &ScopeView::channelPositionChanged);
        const auto position = [&](int ch) {
            double last = std::nan("");
            for (const auto& args : moved)
                if (args.at(0).toInt() == ch)
                    last = args.at(1).toDouble();
            return last;
        };
        const QRectF plot = view_->plotRect();
        const double dx = plot.width() / starosc::kDivisionsX;
        const double dy = plot.height() / starosc::kDivisionsY;

        // A drag on the figure moves it: one division right and one up.
        const QPointF from = plot.center() + QPointF(-30, 20);
        drag(view_.get(), from, from + QPointF(dx, -dy));
        QCOMPARE(position(0), 1.0);
        QCOMPARE(position(1), 1.0);

        // CH1's marker on the bottom edge moves it sideways only.
        const QPointF marker = view_->markerPosition(ScopeView::Marker::Channel1);
        QVERIFY(std::abs(marker.x() - (plot.center().x() + dx)) < 1);
        moved.clear();
        drag(view_.get(), marker, marker - QPointF(2 * dx, 0));
        QCOMPARE(position(0), -1.0);
        QVERIFY(std::isnan(position(1)));

        // A double click away from it centres the figure: the middle of each signal on the centre of the screen.
        doubleClick(view_.get(), plot.center() + QPointF(40, -30));
        QVERIFY(std::abs(position(0) - -1.0) <= 0.02);  // +1 V at 1 V/div
        QVERIFY(std::abs(position(1)) <= 0.02);
    }

    void viewMenuKeepsTheWindowLookApart()
    {
        MainWindow window(true);
        auto* view = window.findChild<QMenu*>("viewMenu");
        auto* appearance = window.findChild<QMenu*>("appearanceMenu");
        QVERIFY(view && appearance);
        const auto texts = [](const QMenu* menu) {
            QStringList out;
            for (const QAction* a : menu->actions())
                if (!a->isSeparator())
                    out << QString(a->text()).remove('&');
            return out;
        };
        // The display's own settings come first; the Qt style and palette only in their submenu at the end.
        const QStringList top = texts(view);
        QCOMPARE(top.first(), QString("X-Y"));
        QVERIFY(top.contains("Trace Colors…"));
        QVERIFY(!top.contains("Classic Palette"));
        QCOMPARE(top.at(top.size() - 2), QString("Window Appearance"));
        QCOMPARE(top.last(), QString("Language"));
        const QStringList look = texts(appearance);
        QVERIFY(look.contains("Fusion"));
        QCOMPARE(look.last(), QString("Classic Palette"));

    }

    void translationsAreCompleteAndKeepTheirMarks()
    {
        // Every text of every translation is translated, keeps the %1… of its source, and has a key letter (&) where
        // the source has one. New texts come into the .ts files with the update_translations target (docs/translating.md).
        const QDir dir(STAROSC_TS_DIR);
        const QStringList files = dir.entryList({"*.ts"}, QDir::Files);
        QVERIFY(!files.isEmpty());
        const QRegularExpression placeholder("%\\d+");
        const auto marks = [&](const QString& text) {
            QStringList out;
            for (auto it = placeholder.globalMatch(text); it.hasNext();)
                out << it.next().captured();
            out.sort();
            out.removeDuplicates();
            return out;
        };
        const auto hasKey = [](const QString& text) {
            for (qsizetype i = 0; i + 1 < text.size(); ++i)
                if (text[i] == '&') {
                    if (text[i + 1] != '&' && !text[i + 1].isSpace())
                        return true;
                    ++i;
                }
            return false;
        };
        QStringList problems;
        int messages = 0;
        for (const QString& name : files) {
            QFile file(dir.filePath(name));
            QVERIFY(file.open(QIODevice::ReadOnly));
            QXmlStreamReader xml(&file);
            QString context, source, translation, type;
            while (!xml.atEnd()) {
                xml.readNext();
                if (xml.isStartElement()) {
                    if (xml.name() == u"name")
                        context = xml.readElementText();
                    else if (xml.name() == u"source")
                        source = xml.readElementText();
                    else if (xml.name() == u"translation") {
                        type = xml.attributes().value("type").toString();
                        translation = xml.readElementText(QXmlStreamReader::IncludeChildElements);
                    }
                } else if (xml.isEndElement() && xml.name() == u"message") {
                    ++messages;
                    const QString where = QString("%1 / %2: \"%3\"").arg(name, context, source);
                    if (type == "vanished" || type == "obsolete")
                        continue;
                    if (type == "unfinished" || translation.isEmpty())
                        problems << where + " is not translated";
                    else if (marks(source) != marks(translation))
                        problems << where + " lost a %N";
                    else if (hasKey(source) && !hasKey(translation))
                        problems << where + " has no key letter";
                }
            }
            QVERIFY2(!xml.hasError(), qPrintable(name + ": " + xml.errorString()));
        }
        QVERIFY2(problems.isEmpty(), qPrintable(QString("%1 of %2:\n").arg(problems.size()).arg(messages) +
                                                problems.mid(0, 25).join('\n')));
    }

    void interfaceSpeaksTurkish()
    {
#ifdef STAROSC_TRANSLATED
        const auto languages = language::available();
        QVERIFY(std::any_of(languages.begin(), languages.end(),
                            [](const language::Language& l) { return l.code == "tr" && l.name == QString("Türkçe"); }));
        QVERIFY(language::use("tr"));
        const QString view = QCoreApplication::translate("MainWindow", "&View");
        const QString frequency = parameterName(starosc::Parameter::Frequency);
        language::use("en");
        QVERIFY2(view != "&View", qPrintable(view));
        QVERIFY2(frequency != "Frequency", qPrintable(frequency));
        QCOMPARE(QCoreApplication::translate("MainWindow", "&View"), QString("&View"));
        QVERIFY(!language::use("xx"));
#else
        QSKIP("built without Qt's Linguist tools, so without translations");
#endif
    }

    void shortcutsSayCtrlInEveryLanguage()
    {
#ifdef STAROSC_TRANSLATED
        // Qt's Turkish writes "Kontrol+Üst Karakter+S"; the program keeps the names on the keys.
        QTranslator qt;
        if (!qt.load("qtbase_tr", QCoreApplication::applicationDirPath() + "/translations") &&
            !qt.load("qtbase_tr", QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
            QSKIP("Qt's own Turkish translation (qtbase_tr) is not installed");
        QVERIFY(language::use("tr"));
        const QString shortcut = QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S).toString(QKeySequence::NativeText);
        const QString alt = QKeySequence(Qt::ALT | Qt::Key_0).toString(QKeySequence::NativeText);
        language::use("en");
        QCOMPARE(shortcut, QString("Ctrl+Shift+S"));
        QCOMPARE(alt, QString("Alt+0"));
#else
        QSKIP("built without Qt's Linguist tools, so without translations");
#endif
    }

    void statusBarFieldsKeepTheirPlace()
    {
        MainWindow window(true);
        // The device's own calibration is the default one, not a "factory" one.
        QTRY_COMPARE_WITH_TIMEOUT(window.findChild<QLabel*>("calibrationInfo")->text(), QString("Cal: default"), 5000);
        window.resize(1180, 760);
        window.show();
        auto* sampling = window.findChild<QLabel*>("samplingInfo");
        auto* calibration = window.findChild<QLabel*>("calibrationInfo");
        auto* rate = window.findChild<QLabel*>("rateInfo");
        QVERIFY(sampling && calibration && rate);
        QApplication::processEvents();
        // The worker writes its own texts meanwhile; all of them fit the reserved widths.
        sampling->setText("1 MSa/s · 320 pts");
        rate->setText("1.0 frames/s");
        QApplication::processEvents();
        const int before = calibration->mapTo(&window, QPoint()).x();
        sampling->setText("16 MSa/s · 1048576 pts");
        rate->setText("123.4 frames/s");
        QApplication::processEvents();
        QCOMPARE(calibration->mapTo(&window, QPoint()).x(), before);
        window.close();
    }

    void generatorPreviewTextsAreInItsStatusStrip()
    {
        GeneratorPanel panel;
        panel.resize(900, 600);
        starosc::GeneratorSettings g;
        g.output = false;
        panel.setSettings(g);
        auto* state = panel.findChild<QLabel*>("previewOutput");
        auto* scale = panel.findChild<QLabel*>("previewScale");
        auto* limits = panel.findChild<QLabel*>("previewLimits");
        QVERIFY(state && scale && limits);
        QCOMPARE(state->text(), QString("Output off, about 0 V"));
        QCOMPARE(state->statusTip(), QString("Output off: DDS OUT is parked near 0 V"));
        QCOMPARE(scale->text(), QString("2.0 V/div · 2 periods"));
        QVERIFY2(limits->text().contains("Limits +4.86 V / ") && limits->text().contains("4.79 V"), qPrintable(limits->text()));
        g.output = true;
        panel.setSettings(g);
        QCOMPARE(state->text(), QString("Output on"));

        // Nothing written on the picture: its only grey is the dim trace of an output that is off, at most 0x70.
        GeneratorPreview preview;
        preview.resize(520, 380);
        g.output = false;
        preview.setSettings(g);
        const QImage image = preview.grab().toImage();
        int grey = 0;
        for (int y = 0; y < image.height(); ++y)
            for (int x = 0; x < image.width(); ++x) {
                const QColor c = image.pixelColor(x, y);
                grey += c.red() == c.green() && c.green() == c.blue() && c.red() > 0x80;
            }
        QCOMPARE(grey, 0);
    }

    void fusionIsTheDefaultLook()
    {
        QSettings().remove("appearance/style");
        QSettings().remove("appearance/classicPalette");
        QCOMPARE(theme::style().toLower(), QString("fusion"));
        QVERIFY(!theme::classicPalette());
    }

    void menuLettersAreUniqueInEveryLanguage()
    {
        // Every menu, in every language, gives each of its entries a key letter of its own.
        for (const language::Language& l : language::available()) {
            QVERIFY(language::use(l.code));
            MainWindow window(true);
            QList<QWidget*> menus{window.menuBar()};
            for (QMenu* menu : window.findChildren<QMenu*>())
                menus << menu;
            QStringList clashes;
            for (QWidget* menu : menus) {
                QStringList letters;
                for (const QAction* a : menu->actions()) {
                    const qsizetype at = a->text().indexOf('&');
                    if (a->isVisible() && at >= 0 && at + 1 < a->text().size() && a->text()[at + 1] != '&')
                        letters << a->text().mid(at + 1, 1).toLower();
                }
                if (QSet<QString>(letters.begin(), letters.end()).size() != letters.size())
                    clashes << QString("%1: %2").arg(menu->objectName(), letters.join(' '));
            }
            QVERIFY2(clashes.isEmpty(), qPrintable(l.code + ": " + clashes.join(" | ")));
        }
        language::use("en");
    }

    void cursorReadoutsKeepTheirPlace()
    {
        MainWindow window(true);
        auto* view = window.findChild<ScopeView*>();
        auto* info = window.findChild<QLabel*>("cursorInfo");
        QVERIFY(view && info);
        const auto plain = [&] {
            QTextDocument document;
            document.setHtml(info->text());
            return document.toPlainText();
        };
        starosc::CursorSettings c;
        c.mode = starosc::CursorMode::Time;
        c.x = {0.4, 0.4};  // ΔT and ΔV 0, 1/ΔT none
        emit view->cursorsChanged(c);
        QTRY_VERIFY_WITH_TIMEOUT(plain().contains("CH2"), 10000);
        const QString together = plain();

        // Values of other lengths leave the readouts after them where they were.
        c.x = {0.05, 0.65};
        emit view->cursorsChanged(c);
        const QString apart = plain();
        QVERIFY2(apart != together, qPrintable(apart));
        QCOMPARE(apart.indexOf("CH1"), together.indexOf("CH1"));
        QCOMPARE(apart.indexOf("CH2"), together.indexOf("CH2"));
        QCOMPARE(apart.size(), together.size());
    }

    void xyFigureHasItsOwnColour()
    {
        auto frame = std::make_shared<starosc::Frame>();
        for (int i = 0; i < 3000; ++i) {  // enough samples to draw without antialiasing, in the plain colour
            frame->volts[0].push_back(static_cast<float>(std::sin(i * 0.01)));
            frame->volts[1].push_back(static_cast<float>(std::sin(i * 0.02)));
        }
        frame->plan.window_samples = 3000;
        starosc::ScopeSettings s = defaults();
        s.display = starosc::DisplayMode::XY;
        view_->setSettings(s);
        view_->setFrame(frame);
        const QColor chosen(0, 255, 0);
        display_style::setChosenColor(display_style::XY, chosen);
        const QImage image = view_->grab().toImage();
        display_style::setChosenColor(display_style::XY, display_style::defaultColor(display_style::XY));
        int drawn = 0;
        for (int y = 0; y < image.height(); ++y)
            for (int x = 0; x < image.width(); ++x)
                drawn += image.pixelColor(x, y) == chosen;
        QVERIFY2(drawn > 100, qPrintable(QString::number(drawn)));

        TraceColorsDialog dialog;
        QCOMPARE(dialog.color(display_style::XY), display_style::defaultColor(display_style::XY));
    }

    void xyButtonKeepsEachModesPositions()
    {
        MainWindow window(true);
        auto* xy = window.findChild<QAction*>("xyAction");
        auto* position1 = window.findChild<QDoubleSpinBox*>("position1");
        QVERIFY(xy && position1);
        QComboBox* display = nullptr;
        for (auto* c : window.findChildren<QComboBox*>())
            if (c->count() == 2 && c->itemText(0) == "Y-T")
                display = c;
        QVERIFY(display);

        position1->setValue(2.0);
        xy->trigger();
        QVERIFY(xy->isChecked());
        QCOMPARE(display->currentIndex(), 1);
        QCOMPARE(position1->value(), 0.0);  // X-Y starts in the middle
        position1->setValue(-1.0);

        // The panel's box switches back, and Y-T has its positions again; X-Y keeps its own for next time.
        display->setCurrentIndex(0);
        QVERIFY(!xy->isChecked());
        QCOMPARE(position1->value(), 2.0);
        xy->trigger();
        QCOMPARE(position1->value(), -1.0);
    }

    void referenceIsStoredShownAndKept()
    {
        const QString kept = QFileInfo(QSettings().fileName()).absolutePath() + "/references/R1.csv";
        QFile::remove(kept);
        {
            MainWindow window(true);
            auto* controls = window.findChild<ControlPanel*>();
            QVERIFY(controls);
            // Wait for frames from the simulator, then store CH1 in R1.
            QTRY_VERIFY_WITH_TIMEOUT(!window.findChild<QLabel*>("acquisitionInfo") ||
                                         !window.findChild<QLabel*>("acquisitionInfo")->text().isEmpty(),
                                     5000);
            QTest::qWait(300);
            emit controls->referenceStoreRequested(0, 0);
            bool described = false;
            for (auto* label : controls->findChildren<QLabel*>())
                described |= label->text().startsWith("CH1 ·");
            QVERIFY(described);
            window.close();  // keeps the references
        }
        QFile file(kept);
        QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
        const auto ref = starosc::reference_from_csv(file.readAll().toStdString());
        QVERIFY(ref);
        QCOMPARE(QString::fromStdString(ref->source), QString("CH1"));
        QVERIFY(ref->samples.size() > 100);
        file.close();
        QFile::remove(kept);
    }

    void mathAndDisplaySettingsRoundTrip()
    {
        QTemporaryDir dir;
        QSettings store(dir.filePath("m.ini"), QSettings::IniFormat);
        starosc::ScopeSettings s = defaults();
        s.math = {true, starosc::MathOperation::Multiply, 1, 0, 0.2, -1.5};
        s.display = starosc::DisplayMode::XY;
        s.other_position_div = {2.0, -2.5};
        settings_store::save(store, s, {});
        starosc::ScopeSettings loaded = defaults();
        starosc::CursorSettings cursors;
        settings_store::load(store, loaded, cursors);
        QVERIFY(loaded.math.enabled);
        QVERIFY(loaded.math.operation == starosc::MathOperation::Multiply);
        QCOMPARE(loaded.math.a, 1);
        QCOMPARE(loaded.math.b, 0);
        QCOMPARE(loaded.math.units_per_div, 0.2);
        QCOMPARE(loaded.math.position_div, -1.5);
        QVERIFY(loaded.display == starosc::DisplayMode::XY);
        QCOMPARE(loaded.other_position_div[0], 2.0);
        QCOMPARE(loaded.other_position_div[1], -2.5);
    }

    void settingsRoundTrip()
    {
        QTemporaryDir dir;
        QSettings store(dir.filePath("t.ini"), QSettings::IniFormat);
        starosc::ScopeSettings s = defaults();
        s.channel[1] = {false, 0.2, 1.5, starosc::Coupling::AC, 10.15, 10.15};
        s.timebase = {2e-6, starosc::MemoryDepth::Large};
        s.trigger = {1, starosc::TriggerEdge::Either, starosc::TriggerMode::Normal, -0.3, 0.25, 0.7};
        s.zoom = {200e-9, 0.3};
        s.thresholds = {20, 45, 80};
        starosc::CursorSettings c;
        c.mode = starosc::CursorMode::Voltage;
        c.source = 1;
        c.x = {0.1, 0.9};
        c.y_div = {1.5, -0.5};
        settings_store::save(store, s, c);

        starosc::ScopeSettings loaded = defaults();
        starosc::CursorSettings loadedCursors;
        settings_store::load(store, loaded, loadedCursors);
        QCOMPARE(loaded.channel[1].enabled, false);
        QCOMPARE(loaded.channel[1].volts_per_div, 0.2);
        QCOMPARE(loaded.channel[1].position_div, 1.5);
        QVERIFY(loaded.channel[1].coupling == starosc::Coupling::AC);
        QCOMPARE(loaded.channel[1].probe, 10.15);
        QCOMPARE(loaded.channel[1].custom_probe, 10.15);
        QCOMPARE(loaded.timebase.seconds_per_div, 2e-6);
        QVERIFY(loaded.timebase.memory == starosc::MemoryDepth::Large);
        QCOMPARE(loaded.trigger.source, 1);
        QVERIFY(loaded.trigger.edge == starosc::TriggerEdge::Either);
        QVERIFY(loaded.trigger.mode == starosc::TriggerMode::Normal);
        QCOMPARE(loaded.trigger.level_v, -0.3);
        QCOMPARE(loaded.trigger.pre_trigger, 0.25);
        QCOMPARE(loaded.trigger.hysteresis_div, 0.7);
        QVERIFY(loaded.zoom == s.zoom);
        QVERIFY(loaded.thresholds == s.thresholds);
        QVERIFY(loadedCursors.mode == starosc::CursorMode::Voltage);
        QCOMPARE(loadedCursors.source, 1);
        QCOMPARE(loadedCursors.x[1], 0.9);
        QCOMPARE(loadedCursors.y_div[0], 1.5);
    }

    void settingsIgnoreInvalidValues()
    {
        QTemporaryDir dir;
        QSettings store(dir.filePath("t.ini"), QSettings::IniFormat);
        store.setValue("scope/ch1/voltsPerDiv", -5);
        store.setValue("scope/trigger/mode", 7);
        store.setValue("scope/trigger/source", "x");
        store.setValue("cursors/x1", 3.0);

        starosc::ScopeSettings s = defaults();
        starosc::CursorSettings c;
        settings_store::load(store, s, c);
        QCOMPARE(s.channel[0].volts_per_div, 1.0);
        QVERIFY(s.trigger.mode == starosc::TriggerMode::Auto);
        QCOMPARE(s.trigger.source, 0);
        QCOMPARE(c.x[0], 0.3);
    }

    void calibrationJsonRoundTrip()
    {
        const starosc::UserCalibration cal = calibrated();
        QString error;
        const auto back = calibration_store::fromJson(calibration_store::toJson(cal), &error);
        QVERIFY2(back, qPrintable(error));
        QCOMPARE(back->device_id, cal.device_id);
        QCOMPARE(back->gain_time, cal.gain_time);
        QCOMPARE(back->stages.size(), cal.stages.size());
        QVERIFY(back->stages[2].same_stage(cal.stages[2]));
        QCOMPARE(back->stages[2].channel[1].gain, 0.9421);
        QVERIFY(std::abs(back->stages[6].channel[0].zero_v - -0.1103) < 1e-12);

        // As written by tools/dc_gain.py --json.
        const auto tool = calibration_store::fromJson(R"json({"format": "star-oscilloscope-calibration", "version": 1,
            "model": "ISDS205X(1.1)", "device_id": "SIMULATOR", "zero_time": "2026-11-01T20:41:53",
            "gain_time": "2026-11-01T20:41:53", "stages": [{"gain_code": 265, "level_code": [512, 510],
            "factory_span_mv_ch1": [-229.8, 287.8], "ch1": {"zero_mv": -2.08, "gain": 0.93034},
            "ch2": {"zero_mv": -1.96, "gain": 0.938}}]})json",
                                                      &error);
        QVERIFY2(tool, qPrintable(error));
        starosc::UserCalibration fromTool = starosc::make_simulator()->factory_calibration();
        QCOMPARE(fromTool.adopt(*tool), 1);
        QCOMPARE(fromTool.stages[1].channel[0].gain, 0.93034);

        QVERIFY(!calibration_store::fromJson("{\"format\": \"other\"}", &error));
        QVERIFY(!calibration_store::fromJson(
            calibration_store::toJson(cal).replace("\"gain\": 0.9421", "\"gain\": -1"), &error));
    }

    void calibrationIsStoredPerDevice()
    {
        const starosc::UserCalibration factory = starosc::make_simulator()->factory_calibration();
        QString error;
        QVERIFY(calibration_store::loadFor(factory, &error).is_factory());

        QVERIFY2(calibration_store::store(calibrated(), &error), qPrintable(error));
        const QString path = calibration_store::pathFor("SIMULATOR");
        QVERIFY(QFile::exists(path));
        const starosc::UserCalibration loaded = calibration_store::loadFor(factory, &error);
        QVERIFY(error.isEmpty());
        QCOMPARE(loaded.stages[2].channel[1].gain, 0.9421);
        QCOMPARE(loaded.zero_time, std::string("2026-11-01T18:20:00"));

        // Another device never picks the file up, and factory values remove it.
        starosc::UserCalibration other = factory;
        other.device_id = "0000000000000001";
        QVERIFY(calibration_store::loadFor(other, &error).is_factory());
        QVERIFY(calibration_store::store(factory, &error));
        QVERIFY(!QFile::exists(path));
    }

    void calibrationDialogRefusesAnotherDevicesFile()
    {
        const starosc::UserCalibration factory = starosc::make_simulator()->factory_calibration();
        CalibrationDialog dialog(factory, factory);
        starosc::UserCalibration foreign = calibrated();
        foreign.device_id = "0000000000000001";
        QVERIFY(!dialog.adoptFile(foreign).isEmpty());
        QVERIFY(dialog.calibration().is_factory());
        QVERIFY(dialog.adoptFile(calibrated()).isEmpty());
        QCOMPARE(dialog.calibration().stages[6].channel[0].gain, 0.9336);
        QCOMPARE(dialog.table()->item(6, CalibrationDialog::Ch1Gain)->text(), QString("0.9336"));
    }

    void calibrationDialogNamesTheDevice()
    {
        starosc::UserCalibration factory = starosc::make_simulator()->factory_calibration();
        factory.model = "ISDS205X";
        factory.device_id = "0123456789ABCDEF";
        CalibrationDialog dialog(factory, factory);
        const auto text = [&](const char* name) {
            auto* label = dialog.findChild<QLabel*>(name);
            return label ? label->text() : QString("(missing %1)").arg(name);
        };
        QCOMPARE(text("model"), QString("ISDS205X"));
        QCOMPARE(text("serialNumber"), QString("0123456789ABCDEF"));
        QCOMPARE(text("zeroTime"), QString("never (default values)"));
        QVERIFY(dialog.findChild<QLabel*>("calibrationStatus")->isHidden());  // nothing to say yet

        auto zero = starosc::make_simulator()->measure_zero();
        dialog.zeroMeasured(zero);
        QVERIFY2(text("zeroTime").startsWith("20"), qPrintable(text("zeroTime")));
        QVERIFY(text("gainTime").startsWith("never"));
    }

    void calibrationDialogTakesZeroAndGainEdits()
    {
        auto sim = starosc::make_simulator();
        const starosc::UserCalibration factory = sim->factory_calibration();
        CalibrationDialog dialog(factory, factory);
        StageZeros zero = sim->measure_zero();
        zero[0].mean_v = {0.0012, -0.0004};
        dialog.zeroMeasured(zero);
        QCOMPARE(dialog.calibration().stages[0].channel[0].zero_v, 0.0012);
        QVERIFY(!dialog.calibration().zero_time.empty());
        QCOMPARE(dialog.table()->item(0, CalibrationDialog::Ch2Zero)->text(), QString("-0.40"));

        dialog.table()->item(3, CalibrationDialog::Ch2Gain)->setText("0,94");
        QCOMPARE(dialog.calibration().stages[3].channel[1].gain, 0.94);
        QVERIFY(!dialog.calibration().gain_time.empty());
        dialog.table()->item(3, CalibrationDialog::Ch2Gain)->setText("7");  // out of range: restored
        QCOMPARE(dialog.calibration().stages[3].channel[1].gain, 0.94);
        QCOMPARE(dialog.table()->item(3, CalibrationDialog::Ch2Gain)->text(), QString("0.9400"));
    }

    void spectrumTabShowsTheSimulatorSpectrum()
    {
        MainWindow window(true);
        QVERIFY(window.showTab("spectrum"));
        auto* panel = window.findChild<SpectrumPanel*>();
        auto* table = window.findChild<SpectrumTable*>();
        auto* info = window.findChild<QLabel*>("spectrumInfo");
        QVERIFY(panel && table && info);
        QTRY_VERIFY_WITH_TIMEOUT(!info->text().isEmpty(), 10000);
        QVERIFY2(info->text().startsWith("Span 0 Hz – 500 kHz   1 MSa/s   "), qPrintable(info->text()));
        QTRY_COMPARE_WITH_TIMEOUT(table->item(0, 1)->text(), QString("1.00000 kHz"), 5000);  // CH1 fundamental
        QCOMPARE(table->item(0, 2)->text(), QString("1.50000 kHz"));
        QVERIFY(table->isColumnHidden(3));  // no math trace
        QVERIFY2(table->item(2, 1)->text().contains("(41."), qPrintable(table->item(2, 1)->text()));  // square wave THD
        // Back on the oscilloscope tab, the scope's panels return.
        QVERIFY(window.showTab("oscilloscope"));
        QVERIFY(!panel->isVisibleTo(&window));
        QVERIFY(window.findChild<ControlPanel*>()->isVisibleTo(&window));
    }

    void spectrumPanelSharesTheInputScale()
    {
        MainWindow window(true);
        auto* panel = window.findChild<SpectrumPanel*>();
        auto* controls = window.findChild<ControlPanel*>();
        QVERIFY(panel && controls);
        auto* scale = panel->findChildren<QComboBox*>().value(0);  // CH1 scale
        QVERIFY(scale && scale->statusTip().startsWith("Input scale"));
        scale->setCurrentIndex(scale->findData(0.2));
        QCOMPARE(controls->settings().channel[0].volts_per_div, 0.2);

        starosc::ScopeSettings s = controls->settings();
        s.channel[1].volts_per_div = 2.0;
        emit controls->settingsChanged(s);
        bool shown = false;
        for (auto* label : panel->findChildren<QLabel*>())
            shown |= label->text().startsWith("Range -4 V to 12 V");  // CH2 at 2 V/div, position -2 div
        QVERIFY(shown);
    }

    void spectrumViewZoomsAndPans()
    {
        SpectrumView view;
        view.resize(652, 536);
        starosc::SpectrumSettings s;
        view.setSettings(s);
        const QRectF plot = view.plotRect();
        QSignalSpy span(&view, &SpectrumView::spanChanged);
        QSignalSpy level(&view, &SpectrumView::referenceLevelChanged);

        wheel(&view, plot.center(), 1);  // zoom in around 250 kHz
        QCOMPARE(span.count(), 1);
        QCOMPARE(span.last().at(0).toDouble(), 50e3);
        QCOMPARE(span.last().at(1).toDouble(), 450e3);

        s.start_hz = 50e3;
        s.stop_hz = 450e3;
        view.setSettings(s);
        drag(&view, plot.center(), plot.center() + QPointF(plot.width() / 4, plot.height() / 10));
        QCOMPARE(span.last().at(0).toDouble(), 0.0);  // panned left by 100 kHz, then held at 0 Hz
        QCOMPARE(span.last().at(1).toDouble(), 400e3);
        QCOMPARE(level.last().at(0).toDouble(), 20.0);  // down a division: the top is 10 dB higher

        mouse(&view, QEvent::MouseButtonDblClick, plot.center());
        QCOMPARE(span.last().at(0).toDouble(), 0.0);
        QCOMPARE(span.last().at(1).toDouble(), 0.0);  // the whole span

        wheel(&view, QPointF(plot.left() / 2, plot.center().y()), 1);  // over the level labels
        QCOMPARE(level.last().at(0).toDouble(), 0.0);
    }

    void spectrumProductReadsInItsOwnUnit()
    {
        // CH1 and a math product with the same power, 0.01 (−20 dB): the channel in dBm, the product relative to 1 V².
        starosc::SpectrumFrame f;
        f.settings.unit = starosc::SpectrumUnit::dBm;
        f.settings.harmonics = 2;
        f.scope.math.operation = starosc::MathOperation::Multiply;
        for (int src : {0, starosc::kChannels}) {
            f.power[src] = std::vector<float>(64, 0.01f);
            f.analysis[src].valid = true;
            f.analysis[src].fundamental_hz = 1000;
            f.analysis[src].fundamental_power = 0.01;
            f.analysis[src].noise_power = 1e-6;
        }
        SpectrumTable table;
        table.setFrame(std::make_shared<const starosc::SpectrumFrame>(f));
        QCOMPARE(table.item(1, 1)->text(), QString("−7.0 dBm"));
        QCOMPARE(table.item(1, 3)->text(), QString("−20.0 dB(V²)"));
        f.settings.unit = starosc::SpectrumUnit::Vrms;
        table.setFrame(std::make_shared<const starosc::SpectrumFrame>(f));
        QTRY_COMPARE(table.item(1, 3)->text(), QString("100.0 mV²"));
        QCOMPARE(table.item(1, 1)->text(), QString("100.0 mV"));
        f.scope.math.operation = starosc::MathOperation::Subtract;  // volts again
        table.setFrame(std::make_shared<const starosc::SpectrumFrame>(f));
        QTRY_COMPARE(table.item(1, 3)->text(), QString("100.0 mV"));

        // On the screen the product sits at its own numbers: 13 dB (the dBm offset) below the channel.
        SpectrumView view;
        view.resize(652, 536);
        starosc::SpectrumSettings s;
        s.unit = starosc::SpectrumUnit::dBm;
        view.setSettings(s);
        f.scope.math.operation = starosc::MathOperation::Multiply;
        view.setFrame(std::make_shared<const starosc::SpectrumFrame>(f));
        const QRectF plot = view.plotRect();
        const double div = plot.height() / starosc::kSpectrumDivisionsY;
        // The channel: −20 dBV is 3 divisions below the top, +10 dBV, in either unit.
        QVERIFY(std::abs((view.yOf(plot, 0.01, 0) - plot.top()) / div - (s.top_dbv + 20) / s.db_per_div) < 1e-6);
        const double below = (view.yOf(plot, 0.01, starosc::kChannels) - view.yOf(plot, 0.01, 0)) / div * s.db_per_div;
        QVERIFY2(std::abs(below - 13.0103) < 1e-3, qPrintable(QString::number(below)));
    }

    void spectrumSettingsRoundTrip()
    {
        QTemporaryDir dir;
        QSettings store(dir.filePath("s.ini"), QSettings::IniFormat);
        starosc::SpectrumSettings s;
        s.sample_rate_hz = 16'000'000;
        s.points = 262144;
        s.window = starosc::Window::FlatTop;
        s.unit = starosc::SpectrumUnit::dBm;
        s.averaging = starosc::SpectrumAveraging::PeakHold;
        s.averages = 32;
        s.shown = {false, true, true};
        s.harmonics = 5;
        s.marker_source = -1;
        s.log_frequency = true;
        s.start_hz = 100;
        s.stop_hz = 2e6;
        s.top_dbv = -12.5;
        s.db_per_div = 5;
        settings_store::saveSpectrum(store, s);

        starosc::SpectrumSettings loaded;
        settings_store::loadSpectrum(store, loaded);
        QCOMPARE(loaded.sample_rate_hz, 16'000'000u);
        QCOMPARE(loaded.points, std::size_t{262144});
        QVERIFY(loaded.window == starosc::Window::FlatTop);
        QVERIFY(loaded.unit == starosc::SpectrumUnit::dBm);
        QVERIFY(loaded.averaging == starosc::SpectrumAveraging::PeakHold);
        QCOMPARE(loaded.averages, 32);
        QVERIFY(!loaded.shown[0] && loaded.shown[1] && loaded.shown[2]);
        QCOMPARE(loaded.harmonics, 5);
        QCOMPARE(loaded.marker_source, -1);
        QVERIFY(loaded.log_frequency);
        QCOMPARE(loaded.start_hz, 100.0);
        QCOMPARE(loaded.stop_hz, 2e6);
        QCOMPARE(loaded.top_dbv, -12.5);
        QCOMPARE(loaded.db_per_div, 5.0);

        store.setValue("spectrum/window", 99);  // out of range keeps the default
        starosc::SpectrumSettings fallback;
        settings_store::loadSpectrum(store, fallback);
        QVERIFY(fallback.window == starosc::SpectrumSettings{}.window);
    }

    static starosc::GainTransfer flatTransfer(std::size_t stages)
    {
        starosc::GainTransfer t;
        t.relative[0].assign(stages, 1.0);
        t.relative[1].assign(stages, 1.0);
        t.relative[0][0] = 1.05;
        t.relative[1][3] = 0.99;
        t.channel_ratio = 0.99;
        return t;
    }

    void gainWizardGoesThroughItsSteps()
    {
        const starosc::UserCalibration cal = starosc::make_simulator()->factory_calibration();
        GainWizard w(cal);
        QSignalSpy check(&w, &GainWizard::checkBenchRequested);
        QSignalSpy level(&w, &GainWizard::levelRequested);
        QSignalSpy transfer(&w, &GainWizard::transferRequested);
        const auto meter = [&] { return w.currentPage()->findChild<QLineEdit*>("meterReading"); };
        w.restart();
        QCOMPARE(w.currentId(), int(GainWizard::IntroPageId));
        w.next();

        // Connecting: no way on before the check has passed, and a failed one says why and can be repeated.
        QCOMPARE(w.currentId(), int(GainWizard::ConnectPageId));
        QVERIFY(!w.currentPage()->isComplete());
        auto* button = w.currentPage()->findChild<QPushButton*>("checkConnection");
        button->click();
        QCOMPARE(check.count(), 1);
        QVERIFY(!button->isEnabled());
        w.stepFailed("CH2 reads about a tenth of the generator: is its probe on ×10?");
        QVERIFY(w.currentPage()->findChild<QLabel*>("connectStatus")->text().contains("×10"));
        QCOMPARE(w.currentId(), int(GainWizard::ConnectPageId));
        QVERIFY(button->isEnabled());
        button->click();
        w.benchChecked();

        // The two levels: on the reference stage, one on each side of zero.
        QCOMPARE(w.currentId(), int(GainWizard::ReferencePlusId));
        QCOMPARE(level.count(), 1);
        QCOMPARE(level.last().at(0).toInt(), starosc::reference_stage(cal.stages));
        QVERIFY(level.last().at(1).toDouble() > 1);
        QVERIFY(!w.currentPage()->isComplete());
        w.levelMeasured(1.50, 1.52);
        QVERIFY(!w.currentPage()->isComplete());  // nothing typed
        for (const char* wrong : {"abc", "-1.6", "5"}) {
            meter()->setText(wrong);
            QVERIFY2(!w.currentPage()->isComplete(), wrong);
            QVERIFY(!w.currentPage()->findChild<QLabel*>("levelHint")->text().isEmpty());
        }
        meter()->setText("1,6");
        QVERIFY(w.currentPage()->isComplete());
        w.next();

        QCOMPARE(w.currentId(), int(GainWizard::ReferenceMinusId));
        QCOMPARE(level.count(), 2);
        QVERIFY(level.last().at(1).toDouble() < -1);
        w.levelMeasured(-1.55, -1.57);
        meter()->setText("-1.7");
        w.next();

        // The stages; it goes on by itself.
        QCOMPARE(w.currentId(), int(GainWizard::TransferPageId));
        QCOMPARE(transfer.count(), 1);
        QVERIFY(!w.currentPage()->isComplete());
        w.transferProgress(3, 13);
        QCOMPARE(w.currentPage()->findChild<QProgressBar*>("transferProgress")->value(), 3);
        w.stepFailed("the signal clipped on stage 2 of CH1");
        QCOMPARE(w.currentId(), int(GainWizard::TransferPageId));
        w.currentPage()->findChild<QPushButton*>("tryAgain")->click();
        QCOMPARE(transfer.count(), 2);
        w.transferred(flatTransfer(cal.stages.size()));

        // The check, on the other coarse gain; empty skips it.
        QCOMPARE(w.currentId(), int(GainWizard::CheckPlusId));
        QCOMPARE(level.count(), 3);
        QCOMPARE(level.last().at(0).toInt(), starosc::check_stage(cal.stages));
        w.levelMeasured(0.30, 0.31);
        QVERIFY(w.currentPage()->isComplete());
        w.next();

        QCOMPARE(w.currentId(), int(GainWizard::ResultPageId));
        const double ch1 = (1.50 + 1.55) / (1.6 + 1.7), ch2 = (1.52 + 1.57) / (1.6 + 1.7);
        const auto& r = w.result().stages;
        QVERIFY(std::abs(r[5].channel[0].gain - ch1) < 1e-9);
        QVERIFY(std::abs(r[5].channel[1].gain - ch2) < 1e-9);
        QVERIFY(std::abs(r[0].channel[0].gain - ch1 * 1.05) < 1e-9);
        QVERIFY(std::abs(r[3].channel[1].gain - ch2 * 0.99) < 1e-9);
        QVERIFY(!w.result().gain_time.empty());
        QVERIFY(w.currentPage()->isComplete());
        auto* table = w.currentPage()->findChild<QTableWidget*>("gainResult");
        QCOMPARE(table->rowCount(), static_cast<int>(r.size()));
        QCOMPARE(table->item(5, 2)->text(), QString::number(ch1, 'f', 4));
        QCOMPARE(table->item(5, 1)->text(), QString("1.0000"));
    }

    void gainWizardChecksAgainstTheMultimeter()
    {
        const starosc::UserCalibration cal = starosc::make_simulator()->factory_calibration();
        GainWizard w(cal);
        const auto meter = [&] { return w.currentPage()->findChild<QLineEdit*>("meterReading"); };
        w.restart();
        w.next();
        w.benchChecked();
        w.levelMeasured(1.50, 1.50);
        meter()->setText("1.6");
        w.next();
        w.levelMeasured(-1.50, -1.50);
        meter()->setText("-1.6");
        w.next();
        w.transferred(flatTransfer(cal.stages.size()));
        QCOMPARE(w.currentId(), int(GainWizard::CheckPlusId));
        w.levelMeasured(0.40, 0.40);
        meter()->setText("0.4");
        w.next();
        QCOMPARE(w.currentId(), int(GainWizard::CheckMinusId));
        QVERIFY(!w.currentPage()->isComplete());  // begun, so not to be left out
        w.levelMeasured(-0.40, -0.40);
        meter()->setText("-0.4");
        w.next();
        QCOMPARE(w.currentId(), int(GainWizard::ResultPageId));

        // The stage reads 1.000 by the multimeter; the result says 0.9375 × 0.99 for CH2, 0.9375 for CH1.
        std::array<double, 2> error{};
        QVERIFY(w.checkError(error));
        QVERIFY(std::abs(error[0] - (0.9375 - 1)) < 1e-9);
        QVERIFY(std::abs(error[1] - (0.9375 * 0.99 - 1)) < 1e-9);
        QVERIFY(w.currentPage()->findChild<QLabel*>("gainNote")->text().contains("more than the usual"));
    }

    void gainWizardWorksWithoutAMultimeter()
    {
        starosc::UserCalibration cal = starosc::make_simulator()->factory_calibration();
        cal.stages[5].channel[1].gain = 0.94;
        GainWizard w(cal);
        QSignalSpy level(&w, &GainWizard::levelRequested);
        w.restart();
        w.currentPage()->findChild<QRadioButton*>("withoutMultimeter")->setChecked(true);
        w.next();
        w.benchChecked();
        QCOMPARE(w.currentId(), int(GainWizard::TransferPageId));
        w.transferred(flatTransfer(cal.stages.size()));
        QCOMPARE(w.currentId(), int(GainWizard::ResultPageId));
        QCOMPARE(level.count(), 0);
        QVERIFY(std::abs(w.result().stages[5].channel[1].gain - 0.94) < 1e-9);
        QVERIFY(std::abs(w.result().stages[5].channel[0].gain - 0.94 * 0.99) < 1e-9);
        QVERIFY(w.currentPage()->findChild<QLabel*>("gainNote")->text().contains("agree with each other"));
    }

    void gainWizardRefusesGainsOutOfRange()
    {
        const starosc::UserCalibration cal = starosc::make_simulator()->factory_calibration();
        GainWizard w(cal);
        w.restart();
        w.currentPage()->findChild<QRadioButton*>("withoutMultimeter")->setChecked(true);
        w.next();
        w.benchChecked();
        starosc::GainTransfer t = flatTransfer(cal.stages.size());
        t.relative[0][1] = 3;
        w.transferred(t);
        QCOMPARE(w.currentId(), int(GainWizard::ResultPageId));
        QVERIFY(!w.currentPage()->isComplete());
    }

    void gainWizardRunsOnTheSimulatorsBench()
    {
        AcquisitionWorker worker;
        worker.open(true);
        const starosc::UserCalibration cal = starosc::make_simulator()->factory_calibration();
        worker.setCalibration(cal);
        worker.run();
        worker.beginGainWizard();

        GainWizard w(cal);
        connect(&w, &GainWizard::checkBenchRequested, &worker, &AcquisitionWorker::checkGainBench, Qt::QueuedConnection);
        connect(&w, &GainWizard::levelRequested, &worker, &AcquisitionWorker::measureGainLevel, Qt::QueuedConnection);
        connect(&w, &GainWizard::transferRequested, &worker, &AcquisitionWorker::transferGains, Qt::QueuedConnection);
        connect(&worker, &AcquisitionWorker::gainBenchOk, &w, &GainWizard::benchChecked);
        connect(&worker, &AcquisitionWorker::gainLevelMeasured, &w, &GainWizard::levelMeasured);
        connect(&worker, &AcquisitionWorker::gainTransferProgress, &w, &GainWizard::transferProgress);
        connect(&worker, &AcquisitionWorker::gainTransferred, &w, &GainWizard::transferred);
        connect(&worker, &AcquisitionWorker::gainStepFailed, &w, [](const QString& message) { QFAIL(qPrintable(message)); });

        const auto meter = [&] { return w.currentPage()->findChild<QLineEdit*>("meterReading"); };
        w.restart();
        w.next();
        w.currentPage()->findChild<QPushButton*>("checkConnection")->click();
        QTRY_COMPARE_WITH_TIMEOUT(w.currentId(), int(GainWizard::ReferencePlusId), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(meter()->isEnabled(), 10000);
        meter()->setText("1.75");
        w.next();
        QTRY_VERIFY_WITH_TIMEOUT(meter()->isEnabled(), 10000);
        meter()->setText("-1.65");
        w.next();
        QTRY_COMPARE_WITH_TIMEOUT(w.currentId(), int(GainWizard::CheckPlusId), 60000);
        QTRY_VERIFY_WITH_TIMEOUT(meter()->isEnabled(), 10000);
        w.next();
        QCOMPARE(w.currentId(), int(GainWizard::ResultPageId));

        // The bench's stages (docs/measurements.md §3), against the reference stage.
        const auto& r = w.transfer().relative;
        QVERIFY(std::abs(r[0][0] - 0.980 / 0.940) < 0.002);
        QVERIFY(std::abs(r[1][3] - 0.940 / 0.944) < 0.002);
        QVERIFY(std::abs(r[0][5] - 1) < 1e-9);

        // Afterwards the source is the plain simulator again, and acquisition goes on.
        QSignalSpy frames(&worker, &AcquisitionWorker::frameReady);
        worker.endGainWizard();
        QTRY_VERIFY_WITH_TIMEOUT(frames.count() > 0, 5000);
    }

    void calibrationDialogTakesTheWizardsGains()
    {
        const starosc::UserCalibration factory = starosc::make_simulator()->factory_calibration();
        starosc::UserCalibration current = factory;
        current.stages[2].channel[0].zero_v = 0.003;
        CalibrationDialog dialog(factory, current);
        QSignalSpy requested(&dialog, &CalibrationDialog::gainWizardRequested);
        dialog.findChild<QPushButton*>("gainWizard")->click();
        QCOMPARE(requested.count(), 1);

        starosc::UserCalibration found = current;
        found.stages[2].channel[0].gain = 0.93;
        found.stages[2].channel[0].zero_v = 0.1;  // not its business
        found.gain_time = "2026-11-07T12:00:00";
        dialog.adoptGains(found);
        QCOMPARE(dialog.calibration().stages[2].channel[0].gain, 0.93);
        QCOMPARE(dialog.calibration().stages[2].channel[0].zero_v, 0.003);
        QCOMPARE(QString::fromStdString(dialog.calibration().gain_time), QString("2026-11-07T12:00:00"));
        QCOMPARE(dialog.table()->item(2, CalibrationDialog::Ch1Gain)->text(), QString("0.9300"));

        dialog.setGainWizardAvailable(false);
        QVERIFY(dialog.findChild<QPushButton*>("gainWizard")->isHidden());
    }

    void classicStyleMenusHaveNoShadow()
    {
        const QString before = QApplication::style()->name();
        theme::apply("Windows", true);
        QMenu classic;
        classic.ensurePolished();
        QVERIFY(classic.windowFlags().testFlag(Qt::NoDropShadowWindowHint));
        theme::apply("Fusion", false);
        QVERIFY(!classic.windowFlags().testFlag(Qt::NoDropShadowWindowHint));  // the menus there already were change too
        QMenu other;
        other.ensurePolished();
        QVERIFY(!other.windowFlags().testFlag(Qt::NoDropShadowWindowHint));
        theme::apply(before, true);
    }

    void measurementsDockCanGrow()
    {
        // No tab may hold the display at its height; the Generator tab's form once did, from behind the others.
        MainWindow window(true);
        window.resize(1180, 760);
        window.show();
        QApplication::processEvents();
        auto* dock = window.findChild<QDockWidget*>("measurementsDock");
        QVERIFY(dock);
        const int before = dock->height();
        window.resizeDocks({dock}, {before + 150}, Qt::Vertical);
        QApplication::processEvents();
        QVERIFY2(dock->height() > before + 100, qPrintable(QString("%1 -> %2").arg(before).arg(dock->height())));
        window.close();
    }

    void manySamplesAColumnDrawTheMeanOverFaintPeaks()
    {
        auto frame = std::make_shared<starosc::Frame>();
        frame->settings = defaults();
        frame->settings.channel[0] = {true, 1.0, 0.0, starosc::Coupling::DC, 1.0};
        frame->settings.channel[1].enabled = false;
        frame->plan.window_samples = 10000;
        frame->plan.sample_rate_hz = 1e6;
        frame->sample_interval_s = 1e-6;
        // Between -1 V and +1 V all the way, so the mean is 0 V, with a spike of one sample to +3 V in the middle.
        for (int i = 0; i <= 10000; ++i)
            frame->volts[0].push_back(i % 2 ? 1.0f : -1.0f);
        frame->volts[0][5000] = 3.0f;

        ScopeView view;
        view.resize(1000, 700);
        view.setSettings(frame->settings);
        view.setFrame(frame);
        const QImage image = view.grab().toImage();
        const QRectF plot = view.plotRect();
        const double perVolt = plot.height() / starosc::kDivisionsY;
        const auto y = [&](double volts) { return static_cast<int>(plot.center().y() - volts * perVolt); };
        const auto strength = [&](int x, int yy) {
            return qRed(image.pixel(x, yy));
        };
        const int red = display_style::channel(0).red();
        const int x = static_cast<int>(plot.left() + plot.width() / 4) + 3;

        QVERIFY2(strength(x, y(0)) > 0.6 * red, "the mean is the bright line");
        const int faint = strength(x, y(0.5));
        QVERIFY2(faint > 0.25 * red && faint < 0.6 * red, qPrintable(QString::number(faint)));  // inside the peaks
        QCOMPARE(strength(x, y(1.5)), display_style::background().red());  // nothing above them

        // The spike reaches the screen although a mean of its column hardly moves.
        bool seen = false;
        const int middle = static_cast<int>(plot.center().x());
        for (int column = middle - 4; column <= middle + 4; ++column)
            seen |= strength(column, y(2.5)) > 0.25 * red;
        QVERIFY(seen);
    }

    void knobWithARangeTurnsAndTakesTypedValues()
    {
        Knob knob("POS", Knob::Size::Small, 6, "div");
        knob.setRange(-4, 4, 0.1, 0.01);
        knob.resize(knob.sizeHint());
        knob.setValue(1);
        QSignalSpy spy(&knob, &Knob::edited);
        QCOMPARE(spy.count(), 0);

        const auto near = [&](double v) { return std::abs(knob.value() - v) < 1e-9; };
        wheel(&knob, {5, 5}, 2);
        QCOMPARE(spy.count(), 1);
        QVERIFY(near(1.2));
        wheel(&knob, {5, 5}, 1, Qt::ControlModifier);  // a tenth of a step
        QVERIFY(near(1.21));

        knob.setValue(3.95);
        wheel(&knob, {5, 5}, 5);
        QVERIFY(near(4));
        const int atEnd = spy.count();
        wheel(&knob, {5, 5}, 1);  // nothing changed, nothing reported
        QCOMPARE(spy.count(), atEnd);

        const auto type = [&](const QString& text, int key) {
            doubleClick(&knob, {5, 5});
            auto* editor = knob.findChild<QLineEdit*>("knobEditor");
            QVERIFY(editor && !editor->isHidden());
            editor->setText(text);
            QTest::keyClick(editor, static_cast<Qt::Key>(key));
            QVERIFY(editor->isHidden());
        };
        type("-1,5 div", Qt::Key_Return);
        QVERIFY(near(-1.5));
        type("abc", Qt::Key_Return);  // no value: stays
        QVERIFY(near(-1.5));
        type("2", Qt::Key_Escape);  // cancelled
        QVERIFY(near(-1.5));
        type("99", Qt::Key_Return);  // held at the end
        QVERIFY(near(4));

        QTest::keyClick(&knob, Qt::Key_Down);
        QVERIFY(near(3.9));
        QTest::keyClick(&knob, Qt::Key_PageDown);
        QVERIFY(near(2.9));
        QTest::keyClick(&knob, Qt::Key_Home);
        QVERIFY(near(-4));
    }

    void knobOfStepsMovesAlongItsList()
    {
        Knob knob("VOLTS/DIV", Knob::Size::Large, 7, "V");
        knob.setSteps({2, 1, 0.5, 0.2}, {"2 V", "1 V", "500 mV", "200 mV"});
        knob.resize(knob.sizeHint());
        knob.setValue(1);
        QCOMPARE(knob.text(), QString("1 V"));
        QSignalSpy spy(&knob, &Knob::edited);

        wheel(&knob, {5, 5}, 1);
        QCOMPARE(knob.value(), 0.5);
        QCOMPARE(knob.text(), QString("500 mV"));
        QCOMPARE(spy.count(), 1);
        wheel(&knob, {5, 5}, 5);  // held at the end
        QCOMPARE(knob.value(), 0.2);

        knob.setValue(0.3);  // closest by ratio
        QCOMPARE(knob.value(), 0.2);

        const auto type = [&](const QString& text) {
            doubleClick(&knob, {5, 5});
            auto* editor = knob.findChild<QLineEdit*>("knobEditor");
            QVERIFY(editor);
            editor->setText(text);
            QTest::keyClick(editor, Qt::Key_Return);
        };
        type("1.1");
        QCOMPARE(knob.value(), 1.0);
        type("500 mV/div");
        QCOMPARE(knob.value(), 0.5);

        // A step that means off goes by its name, and the other values never fall on it.
        Knob zoom("ZOOM", Knob::Size::Small, 7, "s");
        zoom.setSteps({0, 1e-3, 1e-4}, {"Off", "1 ms", "100 µs"});
        zoom.resize(zoom.sizeHint());
        zoom.setValue(0);
        QCOMPARE(zoom.text(), QString("Off"));
        zoom.setValue(5e-3);
        QCOMPARE(zoom.value(), 1e-3);
        doubleClick(&zoom, {5, 5});
        auto* editor = zoom.findChild<QLineEdit*>("knobEditor");
        QVERIFY(editor);
        editor->setText("off");
        QTest::keyClick(editor, Qt::Key_Return);
        QCOMPARE(zoom.value(), 0.0);
    }

    void knobFollowsThePointerAroundIt()
    {
        Knob knob("POS", Knob::Size::Large, 6, "div");
        knob.setRange(-4, 4, 0.1, 0.01);  // 8 divisions in the 270 degrees of the dial
        knob.resize(knob.sizeHint());
        QSignalSpy spy(&knob, &Knob::edited);

        const auto at = [](const Knob& k, double degrees, double radius = 20) {
            const double a = degrees * std::numbers::pi / 180;
            return QPointF(k.width() / 2.0 + radius * std::cos(a), 28 - radius * std::sin(a));
        };
        const auto near = [&](double v) { return std::abs(knob.value() - v) < 0.02; };

        // Around clockwise in small moves: the value only ever grows, also where the pointer goes down and to the
        // left, which is where it turned back when up and right counted as clockwise.
        knob.setValue(-4);
        mouse(&knob, QEvent::MouseButtonPress, at(knob, 90));
        double before = knob.value();
        for (int degrees = 85; degrees >= -175; degrees -= 5) {
            mouse(&knob, QEvent::MouseMove, at(knob, degrees));
            QVERIFY2(knob.value() > before, qPrintable(QString::number(degrees)));
            before = knob.value();
        }
        QVERIFY(near(-4 + 8.0 * 265 / 270));
        mouse(&knob, QEvent::MouseButtonRelease, at(knob, -175), Qt::NoButton);

        // Counter-clockwise turns it back by the same angle, from where it stands rather than to the pointer.
        knob.setValue(0);
        mouse(&knob, QEvent::MouseButtonPress, at(knob, 0));
        QVERIFY(near(0));
        mouse(&knob, QEvent::MouseMove, at(knob, 45));
        QVERIFY(near(-8.0 * 45 / 270));
        mouse(&knob, QEvent::MouseButtonRelease, at(knob, 45), Qt::NoButton);

        // Past an end it waits, and comes back with the first move the other way.
        knob.setValue(-3.9);
        mouse(&knob, QEvent::MouseButtonPress, at(knob, 0));
        mouse(&knob, QEvent::MouseMove, at(knob, 90));
        QVERIFY(near(-4));
        mouse(&knob, QEvent::MouseMove, at(knob, 60));
        QVERIFY(near(-4 + 8.0 * 30 / 270));
        mouse(&knob, QEvent::MouseButtonRelease, at(knob, 60), Qt::NoButton);

        // Across the centre, where the direction means nothing, the knob holds still.
        knob.setValue(1);
        mouse(&knob, QEvent::MouseButtonPress, at(knob, 0));
        mouse(&knob, QEvent::MouseMove, at(knob, 0, 2));
        QVERIFY(near(1));
        mouse(&knob, QEvent::MouseMove, at(knob, 180, 2));
        QVERIFY(near(1));
        mouse(&knob, QEvent::MouseMove, at(knob, 180));
        QVERIFY(near(1));
        mouse(&knob, QEvent::MouseButtonRelease, at(knob, 180), Qt::NoButton);

        // The box under the dial is for typing; a drag from there turns nothing.
        const int count = spy.count();
        mouse(&knob, QEvent::MouseButtonPress, QPointF(knob.width() / 2.0, knob.height() - 3));
        mouse(&knob, QEvent::MouseMove, at(knob, 0));
        mouse(&knob, QEvent::MouseButtonRelease, at(knob, 0), Qt::NoButton);
        QCOMPARE(spy.count(), count);

        // Steps: one for every 90 degrees here, and small moves add up.
        Knob steps("SCALE", Knob::Size::Large, 7, "V");
        steps.setSteps({4, 3, 2, 1}, {"4", "3", "2", "1"});
        steps.resize(steps.sizeHint());
        steps.setValue(4);
        mouse(&steps, QEvent::MouseButtonPress, at(steps, 90));
        for (int degrees = 85; degrees >= 0; degrees -= 5)
            mouse(&steps, QEvent::MouseMove, at(steps, degrees));
        QCOMPARE(steps.value(), 3.0);
        mouse(&steps, QEvent::MouseButtonRelease, at(steps, 0), Qt::NoButton);
    }

    void frontPanelReachesEverySetting()
    {
        FrontPanel panel;
        starosc::ScopeSettings start = defaults();
        start.timebase.seconds_per_div = 1e-3;
        panel.setSettings(start);
        panel.setSampleRate(1e6);
        QSignalSpy spy(&panel, &ScopePanel::settingsChanged);
        const auto key = [&](const char* name) { return panel.findChild<QToolButton*>(name); };
        const auto knob = [&](const char* name) { return panel.findChild<Knob*>(name); };
        const auto turn = [&](const char* name, int notches = 1) { wheel(knob(name), {5, 5}, notches); };
        const auto& s = panel.settings();
        const auto near = [](double a, double b) { return std::abs(a - b) < 1e-9; };
        int expected = 0;
        const auto changed = [&] { return spy.count() == ++expected; };

        // Channels
        key("ch1Key")->click();
        QVERIFY(changed());
        QVERIFY(!s.channel[0].enabled);
        QVERIFY(!knob("ch1Scale")->isEnabled());
        key("ch1Key")->click();
        QVERIFY(changed());
        QVERIFY(s.channel[0].enabled);
        QVERIFY(knob("ch1Scale")->isEnabled());
        turn("ch2Scale");  // clockwise is more sensitive, as a wheel notch up on the display
        QVERIFY(changed());
        QCOMPARE(s.channel[1].volts_per_div, 0.2);
        turn("ch1Position");
        QVERIFY(changed());
        QVERIFY(near(s.channel[0].position_div, 1.1));
        key("ch1Ac")->click();
        QVERIFY(changed());
        QVERIFY(s.channel[0].coupling == starosc::Coupling::AC);
        key("ch1Ac")->click();  // already down: nothing changes
        QCOMPARE(spy.count(), expected);
        auto* probe = panel.findChild<QComboBox*>("frontProbe2");
        QVERIFY(probe);
        probe->setEditText("10.15");
        emit probe->lineEdit()->editingFinished();
        QVERIFY(changed());
        QCOMPARE(s.channel[1].probe, 10.15);
        QCOMPARE(s.channel[1].volts_per_div, 2.0);  // the same input sensitivity, on a round step
        QCOMPARE(knob("ch2Scale")->text(), QString("2 V"));

        // Horizontal
        turn("timeScale");
        QVERIFY(changed());
        QCOMPARE(s.timebase.seconds_per_div, 500e-6);
        QVERIFY(!knob("zoomPosition")->isEnabled());
        QCOMPARE(knob("zoomScale")->text(), QString("Off"));
        turn("zoomScale");
        QVERIFY(changed());
        QCOMPARE(s.zoom.seconds_per_div, 200e-6);
        QVERIFY(knob("zoomPosition")->isEnabled());
        turn("zoomPosition");
        QVERIFY(changed());
        QVERIFY(s.zoom.center > 0.5);
        key("memory2")->click();
        QVERIFY(changed());
        QVERIFY(s.timebase.memory == starosc::MemoryDepth::Large);
        key("display1")->click();
        QVERIFY(changed());
        QVERIFY(s.display == starosc::DisplayMode::XY);
        QVERIFY(!knob("zoomScale")->isEnabled());
        key("display0")->click();
        QVERIFY(changed());

        // Trigger
        key("triggerSource1")->click();
        QVERIFY(changed());
        QCOMPARE(s.trigger.source, 1);
        key("triggerSource0")->click();
        QVERIFY(changed());
        key("triggerEdge2")->click();
        QVERIFY(changed());
        QVERIFY(s.trigger.edge == starosc::TriggerEdge::Either);
        key("triggerMode1")->click();
        QVERIFY(changed());
        QVERIFY(s.trigger.mode == starosc::TriggerMode::Normal);
        key("triggerMode2")->click();
        QVERIFY(changed());
        QVERIFY(s.trigger.mode == starosc::TriggerMode::Single);
        turn("triggerLevel");
        QVERIFY(changed());
        QVERIFY(near(s.trigger.level_v, 1.1));  // a tenth of a division of CH1
        turn("triggerSensitivity");
        QVERIFY(changed());
        QVERIFY(near(s.trigger.hysteresis_div, 0.5));
        turn("preTrigger");
        QVERIFY(changed());
        QVERIFY(near(s.trigger.pre_trigger, 0.55));

        // Math
        QVERIFY(!knob("mathScale")->isEnabled());
        key("mathKey")->click();
        QVERIFY(changed());
        QVERIFY(s.math.enabled);
        QVERIFY(knob("mathScale")->isEnabled());
        auto* operation = panel.findChild<QComboBox*>("mathOperation");
        QVERIFY(operation);
        emit operation->activated(2);
        QVERIFY(changed());
        QVERIFY(s.math.operation == starosc::MathOperation::Multiply);
        QCOMPARE(s.math.units_per_div, 1.0);  // the scale stays when the unit changes
        emit panel.findChild<QComboBox*>("mathA")->activated(1);
        QVERIFY(changed());
        QCOMPARE(s.math.a, 1);
        emit panel.findChild<QComboBox*>("mathB")->activated(0);
        QVERIFY(changed());
        QCOMPARE(s.math.b, 0);
        turn("mathScale");
        QVERIFY(changed());
        QCOMPARE(s.math.units_per_div, 0.5);
        turn("mathPosition");
        QVERIFY(changed());
        QVERIFY(near(s.math.position_div, 0.1));

        // What the tool bar offers stays on the tool bar.
        for (const QToolButton* button : panel.findChildren<QToolButton*>())
            for (const char* text : {"RUN", "FORCE", "AUTOSET", "FFT"})
                QVERIFY2(button->text() != text, text);
    }

    void frontPanelShowsSettingsWithoutReportingThem()
    {
        FrontPanel panel;
        QSignalSpy spy(&panel, &ScopePanel::settingsChanged);
        const auto key = [&](const char* name) { return panel.findChild<QToolButton*>(name); };
        const auto knob = [&](const char* name) { return panel.findChild<Knob*>(name); };

        starosc::ScopeSettings s = defaults();
        s.channel[0].enabled = false;
        s.channel[0].coupling = starosc::Coupling::AC;
        s.channel[1].probe = 10;
        s.channel[1].volts_per_div = 5;
        s.channel[1].position_div = -1.5;
        s.timebase = {2e-3, starosc::MemoryDepth::Medium};
        s.zoom = {100e-6, 0.25};
        s.trigger = {1, starosc::TriggerEdge::Falling, starosc::TriggerMode::Normal, -2.5, 0.2, 0.7};
        s.math = {true, starosc::MathOperation::Add, 1, 0, 0.2, 1.5};
        panel.setSettings(s);

        QVERIFY(!key("ch1Key")->isChecked());
        QVERIFY(key("ch1Ac")->isChecked());
        QVERIFY(key("ch2Key")->isChecked());
        QCOMPARE(knob("ch2Scale")->text(), QString("5 V"));
        QCOMPARE(knob("ch2Position")->text(), QString("-1.50"));
        QCOMPARE(panel.findChild<QComboBox*>("frontProbe2")->currentText(), QString("×10"));
        QCOMPARE(knob("timeScale")->text(), QString("2 ms"));
        QVERIFY(key("memory1")->isChecked());
        QCOMPARE(knob("zoomScale")->text(), QString("100 µs"));
        QCOMPARE(knob("zoomPosition")->value(), 25.0);
        QVERIFY(key("triggerSource1")->isChecked());
        QVERIFY(key("triggerEdge1")->isChecked());
        QVERIFY(key("triggerMode1")->isChecked());
        QCOMPARE(knob("triggerLevel")->value(), -2.5);
        QCOMPARE(knob("triggerSensitivity")->text(), QString("0.7 div"));
        QCOMPARE(knob("preTrigger")->text(), QString("20 %"));
        QVERIFY(key("mathKey")->isChecked());
        QCOMPARE(panel.findChild<QComboBox*>("mathA")->currentIndex(), 1);
        QCOMPARE(panel.findChild<QComboBox*>("mathB")->currentIndex(), 0);
        QCOMPARE(knob("mathScale")->text(), QString("200 mV"));
        QCOMPARE(knob("mathPosition")->text(), QString("+1.50"));
        QCOMPARE(panel.settings(), s);
        QCOMPARE(spy.count(), 0);
    }

    void frontPanelHandlesReferencesAndLights()
    {
        FrontPanel panel;
        QSignalSpy store(&panel, &ScopePanel::referenceStoreRequested);
        QSignalSpy shown(&panel, &ScopePanel::referenceShown);
        QSignalSpy save(&panel, &ScopePanel::referenceSaveRequested);
        QSignalSpy load(&panel, &ScopePanel::referenceLoadRequested);
        QSignalSpy clear(&panel, &ScopePanel::referenceClearRequested);

        panel.findChild<QComboBox*>("ref2Source")->setCurrentIndex(2);
        panel.findChild<QToolButton*>("ref2Store")->click();
        QCOMPARE(store.count(), 1);
        QCOMPARE(store.last().at(0).toInt(), 1);
        QCOMPARE(store.last().at(1).toInt(), 2);  // math

        auto* key = panel.findChild<QToolButton*>("ref1Key");
        QVERIFY(!key->isEnabled());  // empty
        panel.setReferenceState(0, "CH1 · 1 V/div · 5 ms", true);
        QVERIFY(key->isEnabled());
        QVERIFY(key->isChecked());
        QCOMPARE(shown.count(), 0);
        key->click();
        QCOMPARE(shown.count(), 1);
        QCOMPARE(shown.last().at(1).toBool(), false);
        bool described = false;
        for (const QLabel* label : panel.findChildren<QLabel*>())
            described |= label->text() == "CH1 · 1 V/div · 5 ms";
        QVERIFY(described);

        const QList<QAction*> actions = panel.findChild<QToolButton*>("ref1More")->menu()->actions();
        QCOMPARE(actions.size(), 3);
        for (QAction* action : actions)
            action->trigger();
        QCOMPARE(save.count(), 1);
        QCOMPARE(load.count(), 1);
        QCOMPARE(clear.count(), 1);

        auto* light = panel.findChild<QWidget*>("triggeredLight");
        QVERIFY(light && !light->property("lit").toBool());
        panel.setTriggered(true);
        QVERIFY(light->property("lit").toBool());

        panel.setAcquisitionInfo("1 MSa/s · 5000 pts");
        QCOMPARE(panel.findChild<QLabel*>("frontAcquisitionInfo")->text(), QString("1 MSa/s\n5000 pts"));  // a line each
    }

    void viewMenuChoosesTheControlPanelAndKeepsItChosen()
    {
        QSettings().remove("window/controlPanel");
        {
            MainWindow window(true);
            auto* form = window.findChild<ControlPanel*>();
            auto* front = window.findChild<FrontPanel*>();
            auto* spectrum = window.findChild<SpectrumPanel*>();
            auto* formAction = window.findChild<QAction*>("formPanelAction");
            auto* frontAction = window.findChild<QAction*>("frontPanelAction");
            QVERIFY(form && front && spectrum && formAction && frontAction);

            // The Form by default.
            QVERIFY(formAction->isChecked());
            QVERIFY(form->isVisibleTo(&window));
            QVERIFY(!front->isVisibleTo(&window));

            frontAction->trigger();
            QVERIFY(!formAction->isChecked());
            QVERIFY(!form->isVisibleTo(&window));
            QVERIFY(front->isVisibleTo(&window));

            // The Spectrum tab has its own panel, and the choice comes back after it.
            QVERIFY(window.showTab("spectrum"));
            QVERIFY(spectrum->isVisibleTo(&window));
            QVERIFY(!front->isVisibleTo(&window));
            QVERIFY(window.showTab("oscilloscope"));
            QVERIFY(front->isVisibleTo(&window));

            // Both panels always show the same settings, whichever one changed them.
            wheel(front->findChild<Knob*>("timeScale"), {5, 5}, 1);
            QCOMPARE(form->settings().timebase.seconds_per_div, front->settings().timebase.seconds_per_div);
            starosc::ScopeSettings s = form->settings();
            s.channel[0].position_div = 2.5;
            form->setSettings(s);
            emit form->settingsChanged(s);
            QCOMPARE(front->settings().channel[0].position_div, 2.5);
            QCOMPARE(front->findChild<Knob*>("ch1Position")->text(), QString("+2.50"));

            window.close();
        }
        {
            MainWindow window(true);
            QVERIFY(window.findChild<QAction*>("frontPanelAction")->isChecked());
            QVERIFY(window.findChild<FrontPanel*>()->isVisibleTo(&window));
            window.findChild<QAction*>("formPanelAction")->trigger();
            QVERIFY(window.findChild<ControlPanel*>()->isVisibleTo(&window));
            window.close();
        }
        QCOMPARE(QSettings().value("window/controlPanel").toString(), QString("form"));
    }
};

QTEST_MAIN(GuiTest)
#include "test_gui.moc"
