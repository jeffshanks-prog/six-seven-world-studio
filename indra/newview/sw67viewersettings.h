/**
 * @file sw67viewersettings.h
 * @brief Reads and writes viewer settings as 6|7's typed text values
 *        (sw67::workspace::PrefValue), for Settings Backup and Recording
 *        Privacy Mode. Only boolean, integer, decimal and text settings are
 *        handled; colours, vectors and rectangles are left alone.
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

#include "sw67/workspace/profile.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

class LLControlGroup;
class LLControlVariable;

namespace sw67viewer
{
/// The setting's value, or nullopt when it does not exist or has a type we
/// do not handle.
std::optional<sw67::workspace::PrefValue> readSetting(LLControlGroup& group, const std::string& name);
std::optional<sw67::workspace::PrefValue> valueOf(LLControlVariable& control);

/// Sets the setting from `value`. False when it does not exist, has another
/// type, or the text does not parse.
bool writeSetting(LLControlGroup& group, const std::string& name, const sw67::workspace::PrefValue& value);

/// Every saved setting of the group that differs from its default.
std::vector<std::pair<std::string, sw67::workspace::PrefValue>> changedSettings(LLControlGroup& group);

/// True once the per-account settings are loaded (the user is logged in).
bool accountSettingsReady();
} // namespace sw67viewer
