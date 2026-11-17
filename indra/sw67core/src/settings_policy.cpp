/**
 * @file settings_policy.cpp
 * @brief Which settings a profile or backup may carry.
 *
 * Copyright (c) 2026, 6|7 World Studio contributors.
 *
 * The source code in this file is provided to you under the terms of the
 * GNU Lesser General Public License, version 2.1, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE. Terms of the LGPL can be found in doc/LGPL-license.txt
 * in this distribution, or online at http://www.gnu.org/licenses/lgpl-2.1.txt
 */

#include "sw67/common/settings_policy.h"

#include <algorithm>
#include <array>
#include <cctype>

namespace sw67::common
{
namespace
{
std::string lower(std::string_view s)
{
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

// Fragments that mark a setting as holding a credential. Matched anywhere
// in the lower-cased name, so "SW67StreamKey" and "AIProviderApiKey" are
// both caught without being listed.
constexpr std::array<std::string_view, 12> kSecretFragments{
    "password", "passwd", "secret", "token", "apikey", "api_key",
    "credential", "streamkey", "stream_key", "privatekey", "authkey", "sessionid",
};

// Exact names (lower case) of the accessibility settings: the 6|7 profile,
// the scene colour filter, and the viewer's own UI and font scale.
constexpr std::array<std::string_view, 9> kAccessibility{
    "sw67accessibility", "sw67scenefilterinsnapshots", "rendercvdmode", "rendercvdamount",
    "uiscalefactor", "fontscreendpi", "chatfontsize", "uiautoscale", "fontsizeadjustment",
};

// Opt-ins a file must never turn on behind the user's back.
constexpr std::array<std::string_view, 6> kConsent{
    "restrainedlove", "restrainedlovedebug", "sw67captioningenabled", "sw67transcriptsaving",
    "sw67audioleavesdevice", "voiceenabledefault",
};

// Login and account state: not secrets as such, but not a layout either.
constexpr std::array<std::string_view, 5> kAccountPrefixes{
    "rememberpassword", "lastname", "firstname", "loginlocation", "cmdlineloginuri",
};
} // namespace

SettingClass classifySetting(std::string_view name)
{
    if (name.empty() || name.size() > 96)
    {
        return SettingClass::Invalid;
    }
    for (const char c : name)
    {
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.'))
        {
            return SettingClass::Invalid;
        }
    }
    const std::string l = lower(name);
    for (std::string_view f : kSecretFragments)
    {
        if (l.find(f) != std::string::npos)
        {
            return SettingClass::Secret;
        }
    }
    for (std::string_view a : kAccountPrefixes)
    {
        if (l.rfind(a, 0) == 0)
        {
            return SettingClass::Secret;
        }
    }
    if (std::find(kAccessibility.begin(), kAccessibility.end(), l) != kAccessibility.end())
    {
        return SettingClass::Accessibility;
    }
    if (std::find(kConsent.begin(), kConsent.end(), l) != kConsent.end())
    {
        return SettingClass::Consent;
    }
    return SettingClass::Portable;
}

std::string_view settingClassReason(SettingClass c)
{
    switch (c)
    {
    case SettingClass::Portable: return "";
    case SettingClass::Secret: return "may hold a password, key or credential, so it is never written to or read from a file";
    case SettingClass::Accessibility: return "is an accessibility setting, which stays as you set it whatever profile is applied";
    case SettingClass::Consent: return "is an opt-in that only you can turn on, so a file cannot change it";
    case SettingClass::Invalid: return "is not a valid setting name";
    }
    return "";
}

} // namespace sw67::common
