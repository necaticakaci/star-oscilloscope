// SPDX-License-Identifier: GPL-3.0-or-later
// User calibration files: one JSON file per device ID next to the settings file.
#pragma once

#include <optional>

#include <QByteArray>
#include <QString>

#include "starosc/calibration.hpp"

namespace calibration_store {

QString pathFor(const QString& deviceId);

QByteArray toJson(const starosc::UserCalibration& calibration);
std::optional<starosc::UserCalibration> fromJson(const QByteArray& json, QString* error);

std::optional<starosc::UserCalibration> load(const QString& path, QString* error);
bool save(const QString& path, const starosc::UserCalibration& calibration, QString* error);

// Saves the calibration of its device, or removes the file when it holds only factory values.
bool store(const starosc::UserCalibration& calibration, QString* error);

// Loads the saved calibration for the source's stages. Without a file, returns `factory` unchanged.
starosc::UserCalibration loadFor(const starosc::UserCalibration& factory, QString* error);

}  // namespace calibration_store
