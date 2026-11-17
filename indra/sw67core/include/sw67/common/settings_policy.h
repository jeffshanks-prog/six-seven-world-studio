/**
 * @file settings_policy.h
 * @brief Which viewer settings 6|7 World Studio may copy into a file it
 *        writes (a workspace profile, a settings backup) or change when it
 *        applies one. Shared by every module that does either, so the rule
 *        lives in one place.
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

#include <string>
#include <string_view>

namespace sw67::common
{

enum class SettingClass
{
    Portable,      ///< May be saved in and applied from a profile or backup.
    Secret,        ///< Passwords, tokens, keys, stream credentials: never written to a file.
    Accessibility, ///< Kept as the user set it whatever profile is applied.
    Consent,       ///< Opt-ins that need the user's own action (RLV, transcription...).
    Invalid,       ///< Not a well-formed setting name.
};

/// Classifies a setting by name. Names are matched case-insensitively
/// against the lists in settings_policy.cpp; a name that looks like it holds
/// a secret is treated as one even if it is not on a list.
SettingClass classifySetting(std::string_view name);

inline bool isPortableSetting(std::string_view name)
{
    return classifySetting(name) == SettingClass::Portable;
}

/// Why a setting was left out, for import and export reports.
std::string_view settingClassReason(SettingClass c);

} // namespace sw67::common
