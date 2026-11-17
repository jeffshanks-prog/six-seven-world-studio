/**
 * @file settings_policy_test.cpp
 * @brief Secrets, accessibility and consent settings never travel in files.
 *
 * Copyright (c) 2026, 6|7 World Studio contributors. LGPL-2.1.
 */

#include "sw67/common/settings_policy.h"

#include <catch2/catch_test_macros.hpp>

using namespace sw67::common;

TEST_CASE("Credential-like settings are secrets whatever their exact name", "[policy]")
{
    for (const char* name : {"RememberPassword", "SW67StreamKey", "AIProviderApiKey", "OpenAI_API_KEY", "MyServiceToken",
                             "DiscordSecret", "SW67IcecastCredentials", "LastName", "LoginLocation"})
    {
        CAPTURE(name);
        CHECK(classifySetting(name) == SettingClass::Secret);
    }
}

TEST_CASE("Accessibility and consent settings are held back", "[policy]")
{
    CHECK(classifySetting("SW67Accessibility") == SettingClass::Accessibility);
    CHECK(classifySetting("UIScaleFactor") == SettingClass::Accessibility);
    CHECK(classifySetting("RenderCVDMode") == SettingClass::Accessibility);
    CHECK(classifySetting("RestrainedLove") == SettingClass::Consent);
    CHECK(classifySetting("restrainedlove") == SettingClass::Consent);
}

TEST_CASE("Ordinary preferences are portable and junk is invalid", "[policy]")
{
    CHECK(isPortableSetting("RenderFarClip"));
    CHECK(isPortableSetting("ShowChatMiniIcons"));
    CHECK(classifySetting("") == SettingClass::Invalid);
    CHECK(classifySetting("../etc/passwd") == SettingClass::Invalid);
    CHECK(classifySetting("Render Far Clip") == SettingClass::Invalid);
    CHECK_FALSE(settingClassReason(SettingClass::Secret).empty());
}
