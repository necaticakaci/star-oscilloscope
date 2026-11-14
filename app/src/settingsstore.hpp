// SPDX-License-Identifier: GPL-3.0-or-later
// Saves and restores the scope and cursor settings between sessions.
#pragma once

#include "measurementselection.hpp"
#include "starosc/generator.hpp"
#include "starosc/settings.hpp"
#include "starosc/spectrum.hpp"

class QSettings;
class QString;

namespace settings_store {

// Settings in a file on every system, before the first QSettings. Windows would keep them in the registry, but the
// calibration and the references are files beside the settings file: on Windows %APPDATA%\StarOscilloscope\, on Linux
// ~/.config/StarOscilloscope/.
void useFiles();
// Settings, calibration and references in `folder` instead (the --settings option): for a portable copy, and for runs
// that must leave the user's own settings alone.
void useFolder(const QString& folder);

void save(QSettings& store, const starosc::ScopeSettings& scope, const starosc::CursorSettings& cursors);

// Values missing from `store` or out of range keep what `scope` and `cursors` already hold.
void load(const QSettings& store, starosc::ScopeSettings& scope, starosc::CursorSettings& cursors);

// `park` is the "park at 0 V when connecting and closing" choice.
void saveGenerator(QSettings& store, const starosc::GeneratorSettings& generator, bool park);
void loadGenerator(const QSettings& store, starosc::GeneratorSettings& generator, bool& park);

void saveMeasurements(QSettings& store, const MeasurementSelection& selection);
void loadMeasurements(const QSettings& store, MeasurementSelection& selection);

void saveSpectrum(QSettings& store, const starosc::SpectrumSettings& spectrum);
void loadSpectrum(const QSettings& store, starosc::SpectrumSettings& spectrum);

}  // namespace settings_store
