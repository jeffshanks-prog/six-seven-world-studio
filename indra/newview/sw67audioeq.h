/**
 * @file sw67audioeq.h
 * @brief The Sound Equalizer's and Sound Check's settings in the viewer,
 *        and their hand-off to the audio engine's per-type equalizers and
 *        meters. Only what this listener hears changes.
 *
 * Copyright (c) 2026, 6|7 World Studio contributors.
 *
 * The source code in this file is provided to you under the terms of the
 * GNU Lesser General Public License, version 2.1, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE. Terms of the LGPL can be found in doc/LGPL-license.txt
 * in this distribution, or online at http://www.gnu.org/licenses/lgpl-2.1.txt
 */

#pragma once

#include "sw67/audio/equalizer.h"

namespace sw67viewer::audio
{
/// Loaded on first use. Main thread only.
sw67::audio::Settings& settings();
/// Writes the settings file and applies them to the engine.
bool saveAndApply();
/// Sends the current settings to the engine (after the engine starts).
void apply();
/// What the running engine can equalize and meter.
sw67::audio::EngineSupport support();
/// The source's level now; false when it can't be metered.
bool level(sw67::audio::Source source, sw67::audio::Level& out);
} // namespace sw67viewer::audio
