// SPDX-License-Identifier: GPL-3.0-or-later
#include "calibrationstore.hpp"

#include <cmath>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSettings>

namespace calibration_store {
namespace {

constexpr auto kFormat = "star-oscilloscope-calibration";
constexpr int kVersion = 1;

QJsonObject channelJson(const starosc::ChannelCorrection& c)
{
    return {{"zero_mv", c.zero_v * 1000}, {"gain", c.gain}};
}

bool readChannel(const QJsonValue& value, starosc::ChannelCorrection& c)
{
    const QJsonObject o = value.toObject();
    const double zero_mv = o.value("zero_mv").toDouble(NAN);
    const double gain = o.value("gain").toDouble(NAN);
    if (!std::isfinite(zero_mv) || !std::isfinite(gain) || gain <= 0)
        return false;
    c = {zero_mv / 1000, gain};
    return true;
}

}  // namespace

QString pathFor(const QString& deviceId)
{
    const QString dir = QFileInfo(QSettings().fileName()).absolutePath();
    return dir + "/calibration/" + deviceId + ".json";
}

QByteArray toJson(const starosc::UserCalibration& calibration)
{
    QJsonArray stages;
    for (const starosc::StageCalibration& s : calibration.stages) {
        QJsonObject o{{"gain_code", s.gain_code},
                      {"level_code", QJsonArray{s.level_code[0], s.level_code[1]}},
                      {"factory_span_mv_ch1", QJsonArray{std::round(s.factory_span[0].min_v * 1e4) / 10,
                                                         std::round(s.factory_span[0].max_v * 1e4) / 10}}};
        for (int ch = 0; ch < starosc::kChannels; ++ch)
            o.insert(QString("ch%1").arg(ch + 1), channelJson(s.channel[ch]));
        stages.append(o);
    }
    const QJsonObject root{{"format", kFormat},
                           {"version", kVersion},
                           {"model", QString::fromStdString(calibration.model)},
                           {"device_id", QString::fromStdString(calibration.device_id)},
                           {"zero_time", QString::fromStdString(calibration.zero_time)},
                           {"gain_time", QString::fromStdString(calibration.gain_time)},
                           {"stages", stages}};
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

std::optional<starosc::UserCalibration> fromJson(const QByteArray& json, QString* error)
{
    QJsonParseError parse{};
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parse);
    const QJsonObject root = doc.object();
    if (doc.isNull() || root.value("format").toString() != kFormat) {
        *error = QObject::tr("not a Star Oscilloscope calibration file");
        return std::nullopt;
    }
    if (root.value("version").toInt() != kVersion) {
        *error = QObject::tr("unsupported calibration file version %1").arg(root.value("version").toInt());
        return std::nullopt;
    }
    starosc::UserCalibration cal;
    cal.model = root.value("model").toString().toStdString();
    cal.device_id = root.value("device_id").toString().toStdString();
    cal.zero_time = root.value("zero_time").toString().toStdString();
    cal.gain_time = root.value("gain_time").toString().toStdString();
    if (cal.device_id.empty()) {
        *error = QObject::tr("the file names no device");
        return std::nullopt;
    }
    for (const QJsonValue& v : root.value("stages").toArray()) {
        const QJsonObject o = v.toObject();
        starosc::StageCalibration s;
        s.gain_code = static_cast<std::uint16_t>(o.value("gain_code").toInt());
        const QJsonArray level = o.value("level_code").toArray();
        if (level.size() != starosc::kChannels) {
            *error = QObject::tr("a stage has no level codes");
            return std::nullopt;
        }
        for (int ch = 0; ch < starosc::kChannels; ++ch) {
            s.level_code[ch] = static_cast<std::uint16_t>(level[ch].toInt());
            if (!readChannel(o.value(QString("ch%1").arg(ch + 1)), s.channel[ch])) {
                *error = QObject::tr("invalid correction for CH%1 of stage G=%2").arg(ch + 1).arg(s.gain_code, 0, 16);
                return std::nullopt;
            }
        }
        cal.stages.push_back(s);
    }
    return cal;
}

std::optional<starosc::UserCalibration> load(const QString& path, QString* error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        *error = file.errorString();
        return std::nullopt;
    }
    return fromJson(file.readAll(), error);
}

bool save(const QString& path, const starosc::UserCalibration& calibration, QString* error)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(toJson(calibration)) < 0 || !file.commit()) {
        *error = file.errorString();
        return false;
    }
    return true;
}

bool store(const starosc::UserCalibration& calibration, QString* error)
{
    const QString path = pathFor(QString::fromStdString(calibration.device_id));
    if (calibration.is_factory()) {
        if (QFile::exists(path) && !QFile::remove(path)) {
            *error = QObject::tr("cannot remove %1").arg(path);
            return false;
        }
        return true;
    }
    return save(path, calibration, error);
}

starosc::UserCalibration loadFor(const starosc::UserCalibration& factory, QString* error)
{
    starosc::UserCalibration out = factory;
    const QString path = pathFor(QString::fromStdString(factory.device_id));
    if (!QFile::exists(path))
        return out;
    const auto saved = load(path, error);
    if (!saved)
        return out;
    if (saved->device_id != factory.device_id) {
        *error = QObject::tr("%1 belongs to device %2").arg(path, QString::fromStdString(saved->device_id));
        return out;
    }
    out.adopt(*saved);
    out.zero_time = saved->zero_time;
    out.gain_time = saved->gain_time;
    return out;
}

}  // namespace calibration_store
