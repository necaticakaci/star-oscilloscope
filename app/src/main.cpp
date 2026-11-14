// SPDX-License-Identifier: GPL-3.0-or-later
#include <QApplication>
#include <QCommandLineParser>
#include <QLocale>

#include "acquisitionworker.hpp"
#include "appicon.hpp"
#include "language.hpp"
#include "mainwindow.hpp"
#include "settingsstore.hpp"
#include "theme.hpp"
#include "types.hpp"

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName("StarOscilloscope");
    QApplication::setApplicationDisplayName("Star Oscilloscope");
    QApplication::setOrganizationName("StarOscilloscope");
    QApplication::setApplicationVersion(STAROSC_VERSION);
    // The desktop entry the Linux packages install (packaging/linux): Wayland desktops take the window's icon and
    // name from it.
    QApplication::setDesktopFileName("star-oscilloscope");
    QApplication::setWindowIcon(app_icon::icon());
    settings_store::useFiles();
    // Numbers with a decimal point in every language of the interface, also on a Turkish system, as the readouts on
    // the screen write them.
    QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));

    qRegisterMetaType<FramePtr>();
    qRegisterMetaType<SpectrumFramePtr>();
    qRegisterMetaType<starosc::SpectrumSettings>();
    qRegisterMetaType<starosc::ScopeSettings>();
    qRegisterMetaType<AcquisitionWorker::State>();
    qRegisterMetaType<starosc::UserCalibration>();
    qRegisterMetaType<StageZeros>();
    qRegisterMetaType<starosc::GeneratorSettings>();
    qRegisterMetaType<starosc::GainTransfer>();

    QCommandLineParser parser;
    parser.setApplicationDescription("Oscilloscope software for the Instrustar ISDS205 series");
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption simulate("simulate", "Use the built-in signal simulator instead of the device.");
    const QCommandLineOption screenshot("screenshot", "Save a picture of the main window and quit.", "file");
    const QCommandLineOption frames("frames", "Frames to show before --screenshot (default 5).", "count", "5");
    const QCommandLineOption tab("tab", "Open on a tab: oscilloscope, spectrum or generator.", "name");
    const QCommandLineOption settings("settings", "Keep the settings, calibration and references in this folder.", "folder");
    const QCommandLineOption model("model",
                                   "The model the simulator acts as, for example ISDS205B or ISDS205 (default ISDS205X(1.1)).",
                                   "name");
    parser.addOptions({simulate, model, screenshot, frames, tab, settings});
    parser.process(app);
    if (parser.isSet(settings))
        settings_store::useFolder(parser.value(settings));
    language::use(language::startup());

    theme::applySaved();
    MainWindow window(parser.isSet(simulate), parser.value(model));
    if (parser.isSet(tab) && !window.showTab(parser.value(tab)))
        qWarning("unknown tab: %s", qPrintable(parser.value(tab)));
    window.show();
    if (parser.isSet(screenshot))
        window.screenshotAfterFrames(parser.value(screenshot), parser.value(frames).toInt());
    return QApplication::exec();
}
