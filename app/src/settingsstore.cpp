// SPDX-License-Identifier: GPL-3.0-or-later
#include "settingsstore.hpp"

#include <algorithm>
#include <cmath>

#include <QSettings>

namespace settings_store {

void useFiles()
{
#ifdef Q_OS_WIN
    QSettings::setDefaultFormat(QSettings::IniFormat);
#endif
}

void useFolder(const QString& folder)
{
    for (const auto format : {QSettings::NativeFormat, QSettings::IniFormat})
        QSettings::setPath(format, QSettings::UserScope, folder);
}

namespace {

void read(const QSettings& s, const QString& key, double& value, double lo, double hi)
{
    bool ok = false;
    const double v = s.value(key).toDouble(&ok);
    if (ok && std::isfinite(v) && v >= lo && v <= hi)
        value = v;
}

void read(const QSettings& s, const QString& key, bool& value)
{
    if (s.contains(key))
        value = s.value(key).toBool();
}

template <typename Enum>
void readEnum(const QSettings& s, const QString& key, Enum& value, int count)
{
    bool ok = false;
    const int v = s.value(key).toInt(&ok);
    if (ok && v >= 0 && v < count)
        value = static_cast<Enum>(v);
}

void readIndex(const QSettings& s, const QString& key, int& value, int count)
{
    bool ok = false;
    const int v = s.value(key).toInt(&ok);
    if (ok && v >= 0 && v < count)
        value = v;
}

QString channelKey(int ch, const char* name) { return QString("scope/ch%1/%2").arg(ch + 1).arg(name); }

}  // namespace

void save(QSettings& s, const starosc::ScopeSettings& scope, const starosc::CursorSettings& cursors)
{
    for (int ch = 0; ch < starosc::kChannels; ++ch) {
        const auto& c = scope.channel[ch];
        s.setValue(channelKey(ch, "enabled"), c.enabled);
        s.setValue(channelKey(ch, "voltsPerDiv"), c.volts_per_div);
        s.setValue(channelKey(ch, "positionDiv"), c.position_div);
        s.setValue(channelKey(ch, "coupling"), static_cast<int>(c.coupling));
        s.setValue(channelKey(ch, "probe"), c.probe);
        s.setValue(channelKey(ch, "customProbe"), c.custom_probe);
    }
    s.setValue("scope/timebase/secondsPerDiv", scope.timebase.seconds_per_div);
    s.setValue("scope/timebase/memory", static_cast<int>(scope.timebase.memory));
    s.setValue("scope/zoom/secondsPerDiv", scope.zoom.seconds_per_div);
    s.setValue("scope/zoom/center", scope.zoom.center);
    s.setValue("scope/measure/low", scope.thresholds.low);
    s.setValue("scope/measure/mid", scope.thresholds.mid);
    s.setValue("scope/measure/high", scope.thresholds.high);
    s.setValue("scope/trigger/source", scope.trigger.source);
    s.setValue("scope/trigger/edge", static_cast<int>(scope.trigger.edge));
    s.setValue("scope/trigger/mode", static_cast<int>(scope.trigger.mode));
    s.setValue("scope/trigger/level", scope.trigger.level_v);
    s.setValue("scope/trigger/preTrigger", scope.trigger.pre_trigger);
    s.setValue("scope/trigger/hysteresisDiv", scope.trigger.hysteresis_div);

    s.setValue("scope/display", static_cast<int>(scope.display));
    for (int ch = 0; ch < starosc::kChannels; ++ch)
        s.setValue(channelKey(ch, "otherPositionDiv"), scope.other_position_div[ch]);
    s.setValue("scope/math/enabled", scope.math.enabled);
    s.setValue("scope/math/operation", static_cast<int>(scope.math.operation));
    s.setValue("scope/math/a", scope.math.a);
    s.setValue("scope/math/b", scope.math.b);
    s.setValue("scope/math/unitsPerDiv", scope.math.units_per_div);
    s.setValue("scope/math/positionDiv", scope.math.position_div);

    s.setValue("cursors/mode", static_cast<int>(cursors.mode));
    s.setValue("cursors/source", cursors.source);
    for (int i = 0; i < 2; ++i) {
        s.setValue(QString("cursors/x%1").arg(i + 1), cursors.x[i]);
        s.setValue(QString("cursors/y%1").arg(i + 1), cursors.y_div[i]);
    }
}

void load(const QSettings& s, starosc::ScopeSettings& scope, starosc::CursorSettings& cursors)
{
    const double half = starosc::kDivisionsY / 2.0;
    for (int ch = 0; ch < starosc::kChannels; ++ch) {
        auto& c = scope.channel[ch];
        read(s, channelKey(ch, "enabled"), c.enabled);
        read(s, channelKey(ch, "voltsPerDiv"), c.volts_per_div, 1e-4, 1e3);
        read(s, channelKey(ch, "positionDiv"), c.position_div, -half, half);
        readEnum(s, channelKey(ch, "coupling"), c.coupling, 2);
        read(s, channelKey(ch, "probe"), c.probe, starosc::kMinProbe, starosc::kMaxProbe);
        read(s, channelKey(ch, "customProbe"), c.custom_probe, 0, starosc::kMaxProbe);
    }
    read(s, "scope/timebase/secondsPerDiv", scope.timebase.seconds_per_div, 1e-9, 10);
    readEnum(s, "scope/timebase/memory", scope.timebase.memory, 3);
    read(s, "scope/zoom/secondsPerDiv", scope.zoom.seconds_per_div, 0, 10);
    read(s, "scope/zoom/center", scope.zoom.center, 0, 1);
    read(s, "scope/measure/low", scope.thresholds.low, 1, 99);
    read(s, "scope/measure/mid", scope.thresholds.mid, 1, 99);
    read(s, "scope/measure/high", scope.thresholds.high, 1, 99);
    scope.thresholds = starosc::constrained(scope.thresholds);
    readIndex(s, "scope/trigger/source", scope.trigger.source, starosc::kChannels);
    readEnum(s, "scope/trigger/edge", scope.trigger.edge, 3);
    readEnum(s, "scope/trigger/mode", scope.trigger.mode, 3);
    read(s, "scope/trigger/level", scope.trigger.level_v, -1e4, 1e4);
    read(s, "scope/trigger/preTrigger", scope.trigger.pre_trigger, 0, 1);
    read(s, "scope/trigger/hysteresisDiv", scope.trigger.hysteresis_div, 0, 2);

    readEnum(s, "scope/display", scope.display, 2);
    for (int ch = 0; ch < starosc::kChannels; ++ch)
        read(s, channelKey(ch, "otherPositionDiv"), scope.other_position_div[ch], -half, half);
    read(s, "scope/math/enabled", scope.math.enabled);
    readEnum(s, "scope/math/operation", scope.math.operation, starosc::kMathOperations);
    readIndex(s, "scope/math/a", scope.math.a, starosc::kChannels);
    readIndex(s, "scope/math/b", scope.math.b, starosc::kChannels);
    read(s, "scope/math/unitsPerDiv", scope.math.units_per_div, 1e-6, 1e4);
    read(s, "scope/math/positionDiv", scope.math.position_div, -half, half);

    readEnum(s, "cursors/mode", cursors.mode, 3);
    readIndex(s, "cursors/source", cursors.source, starosc::kChannels);
    for (int i = 0; i < 2; ++i) {
        read(s, QString("cursors/x%1").arg(i + 1), cursors.x[i], 0, 1);
        read(s, QString("cursors/y%1").arg(i + 1), cursors.y_div[i], -half, half);
    }
}

namespace {

QString measurementKey(int trace)
{
    return trace == starosc::kChannels ? QStringLiteral("measure/math") : QString("measure/ch%1").arg(trace + 1);
}

QStringList parameterNames(const std::vector<starosc::Parameter>& list)
{
    QStringList out;
    for (starosc::Parameter p : list)
        out << QString::fromUtf8(starosc::parameter_name(p).data());
    return out;
}

// The parameters among `known` that `names` holds, in the order of `known`; other names are ignored.
template <typename Known>
std::vector<starosc::Parameter> parametersNamed(const QStringList& names, const Known& known)
{
    std::vector<starosc::Parameter> out;
    for (starosc::Parameter p : known)
        if (names.contains(QString::fromUtf8(starosc::parameter_name(p).data())))
            out.push_back(p);
    return out;
}

}  // namespace

void saveMeasurements(QSettings& s, const MeasurementSelection& selection)
{
    for (int t = 0; t < MeasurementSelection::kTraces; ++t)
        s.setValue(measurementKey(t), parameterNames(selection.trace[static_cast<std::size_t>(t)]));
    s.setValue("measure/pair", parameterNames(selection.pair));
}

void loadMeasurements(const QSettings& s, MeasurementSelection& selection)
{
    for (int t = 0; t < MeasurementSelection::kTraces; ++t)
        if (s.contains(measurementKey(t)))
            selection.trace[static_cast<std::size_t>(t)] =
                parametersNamed(s.value(measurementKey(t)).toStringList(), starosc::kParameters);
    if (s.contains("measure/pair"))
        selection.pair = parametersNamed(s.value("measure/pair").toStringList(), starosc::kPairParameters);
}

void saveGenerator(QSettings& s, const starosc::GeneratorSettings& g, bool park)
{
    s.setValue("generator/park", park);
    s.setValue("generator/output", g.output);
    s.setValue("generator/waveform", static_cast<int>(g.waveform));
    s.setValue("generator/frequency", g.frequency_hz);
    s.setValue("generator/amplitude", g.amplitude_vpp);
    s.setValue("generator/offset", g.offset_v);
    s.setValue("generator/duty", g.duty_percent);
    s.setValue("generator/sweep/enabled", g.sweep.enabled);
    s.setValue("generator/sweep/start", g.sweep.start_hz);
    s.setValue("generator/sweep/stop", g.sweep.stop_hz);
    s.setValue("generator/sweep/seconds", g.sweep.seconds);
    s.setValue("generator/sweep/logarithmic", g.sweep.logarithmic);
}

void loadGenerator(const QSettings& s, starosc::GeneratorSettings& g, bool& park)
{
    read(s, "generator/park", park);
    read(s, "generator/output", g.output);
    readEnum(s, "generator/waveform", g.waveform, starosc::kWaveforms);
    read(s, "generator/frequency", g.frequency_hz, 1, 1e8);
    read(s, "generator/amplitude", g.amplitude_vpp, 0, 100);
    read(s, "generator/offset", g.offset_v, -100, 100);
    double duty = g.duty_percent;
    read(s, "generator/duty", duty, 1, 99);
    g.duty_percent = static_cast<int>(duty);
    read(s, "generator/sweep/enabled", g.sweep.enabled);
    read(s, "generator/sweep/start", g.sweep.start_hz, 1, 1e8);
    read(s, "generator/sweep/stop", g.sweep.stop_hz, 1, 1e8);
    read(s, "generator/sweep/seconds", g.sweep.seconds, 0.01, 1000);
    read(s, "generator/sweep/logarithmic", g.sweep.logarithmic);
    g = starosc::constrained(g);
}

void saveSpectrum(QSettings& s, const starosc::SpectrumSettings& sp)
{
    s.setValue("spectrum/sampleRate", sp.sample_rate_hz);
    s.setValue("spectrum/points", static_cast<qulonglong>(sp.points));
    s.setValue("spectrum/window", static_cast<int>(sp.window));
    s.setValue("spectrum/unit", static_cast<int>(sp.unit));
    s.setValue("spectrum/averaging", static_cast<int>(sp.averaging));
    s.setValue("spectrum/averages", sp.averages);
    s.setValue("spectrum/ch1", sp.shown[0]);
    s.setValue("spectrum/ch2", sp.shown[1]);
    s.setValue("spectrum/math", sp.shown[2]);
    s.setValue("spectrum/harmonics", sp.harmonics);
    s.setValue("spectrum/markers", sp.marker_source);
    s.setValue("spectrum/logFrequency", sp.log_frequency);
    s.setValue("spectrum/start", sp.start_hz);
    s.setValue("spectrum/stop", sp.stop_hz);
    s.setValue("spectrum/topDbv", sp.top_dbv);
    s.setValue("spectrum/dbPerDiv", sp.db_per_div);
}

void loadSpectrum(const QSettings& s, starosc::SpectrumSettings& sp)
{
    double rate = sp.sample_rate_hz;
    read(s, "spectrum/sampleRate", rate, 1, 1e10);
    sp.sample_rate_hz = static_cast<std::uint32_t>(rate);
    double points = static_cast<double>(sp.points);
    read(s, "spectrum/points", points, 1, 1e8);
    sp.points = static_cast<std::size_t>(points);
    readEnum(s, "spectrum/window", sp.window, starosc::kWindows);
    readEnum(s, "spectrum/unit", sp.unit, starosc::kSpectrumUnits);
    readEnum(s, "spectrum/averaging", sp.averaging, 3);
    readIndex(s, "spectrum/averages", sp.averages, starosc::kMaxSpectrumAverages + 1);
    read(s, "spectrum/ch1", sp.shown[0]);
    read(s, "spectrum/ch2", sp.shown[1]);
    read(s, "spectrum/math", sp.shown[2]);
    readIndex(s, "spectrum/harmonics", sp.harmonics, starosc::kMaxHarmonics + 1);
    int markers = sp.marker_source + 1;
    if (s.contains("spectrum/markers")) {
        markers = s.value("spectrum/markers").toInt() + 1;
        if (markers < 0 || markers > starosc::kSpectrumSources)
            markers = sp.marker_source + 1;
    }
    sp.marker_source = markers - 1;
    read(s, "spectrum/logFrequency", sp.log_frequency);
    read(s, "spectrum/start", sp.start_hz, 0, 1e10);
    read(s, "spectrum/stop", sp.stop_hz, 0, 1e10);
    read(s, "spectrum/topDbv", sp.top_dbv, -200, 100);
    read(s, "spectrum/dbPerDiv", sp.db_per_div, 0.1, 50);
    sp = starosc::constrained(sp);
}

}  // namespace settings_store
