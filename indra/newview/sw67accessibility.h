/**
 * @file sw67accessibility.h
 * @brief The viewer side of 6|7 World Studio's accessibility framework:
 *        loads and saves the profile in settings, and hands every 6|7
 *        module its colours and status indicators.
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

#include "sw67/a11y/profile.h"

#include "v4color.h"

#include <string>

namespace sw67viewer
{
/// The saved profile (setting SW67Accessibility), malformed values replaced
/// by defaults.
sw67::a11y::AccessibilityProfile loadAccessibility();

/// Saves the profile, and applies its scene colour filter to the renderer's
/// own CVD settings (RenderCVDMode, RenderCVDAmount) and to
/// SW67SceneFilterInSnapshots. The interface palette never touches those.
void saveAccessibility(const sw67::a11y::AccessibilityProfile& profile);

LLColor4 toColor(const sw67::a11y::Rgb8& colour);

/// The colour a status draws in, for the saved profile.
LLColor4 statusColor(sw67::a11y::StatusRole role);

/// "<shape glyph> <label>", so the status reads without colour.
std::string statusText(sw67::a11y::StatusRole role);
} // namespace sw67viewer
