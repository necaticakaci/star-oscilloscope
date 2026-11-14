// SPDX-License-Identifier: GPL-3.0-or-later
#include "scopepanel.hpp"

#include "probebox.hpp"
#include "starosc/scale.hpp"

void ScopePanel::setSettings(const starosc::ScopeSettings& settings)
{
    settings_ = settings;
    showSettings();
}

void ScopePanel::setSampleRate(double hz)
{
    if (hz == sampleRate_)
        return;
    sampleRate_ = hz;
    showSettings();
}

void ScopePanel::changeProbe(int channel, double probe)
{
    auto& cs = settings_.channel[channel];
    if (probe == cs.probe)
        return;
    const double factor = probe / cs.probe;
    cs.volts_per_div = starosc::step_value(starosc::volts_per_div_steps(probe), cs.volts_per_div * factor, 0);
    if (settings_.trigger.source == channel)
        settings_.trigger.level_v *= factor;
    cs.probe = probe;
    if (!ProbeBox::isPreset(probe))
        cs.custom_probe = probe;
    showSettings();
    emit settingsChanged(settings_);
}
