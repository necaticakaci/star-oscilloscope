// SPDX-License-Identifier: GPL-3.0-or-later
// Types passed between the acquisition thread and the GUI.
#pragma once

#include <memory>
#include <vector>

#include <QMetaType>

#include "starosc/calibration.hpp"
#include "starosc/engine.hpp"
#include "starosc/gain_transfer.hpp"
#include "starosc/generator.hpp"
#include "starosc/spectrum.hpp"

using FramePtr = std::shared_ptr<const starosc::Frame>;
using SpectrumFramePtr = std::shared_ptr<const starosc::SpectrumFrame>;
using StageZeros = std::vector<starosc::StageZero>;

Q_DECLARE_METATYPE(FramePtr)
Q_DECLARE_METATYPE(SpectrumFramePtr)
Q_DECLARE_METATYPE(starosc::SpectrumSettings)
Q_DECLARE_METATYPE(starosc::ScopeSettings)
Q_DECLARE_METATYPE(starosc::CursorSettings)
Q_DECLARE_METATYPE(starosc::UserCalibration)
Q_DECLARE_METATYPE(StageZeros)
Q_DECLARE_METATYPE(starosc::GeneratorSettings)
Q_DECLARE_METATYPE(starosc::GainTransfer)
