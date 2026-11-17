/**
 * @file backup_test.cpp
 * @brief Settings Backup and Restore: secrets never written or read,
 *        consent never switched on, accessibility only when chosen, restore
 *        previews exactly and can be undone, never deletes.
 *
 * Copyright (c) 2026, 6|7 World Studio contributors. LGPL-2.1.
 */

#include "sw67/backup/backup.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>

using namespace sw67::backup;
using sw67::workspace::PrefValue;

namespace
{
SettingEntry s(const char* name, PrefValue::Type t, const char* v, Scope scope = Scope::Global)
{
    return {scope, name, PrefValue{t, v}};
}

bool hasSetting(const Backup& b, const std::string& name)
{
    return std::any_of(b.settings.begin(), b.settings.end(), [&](const SettingEntry& e) { return e.name == name; });
}

std::vector<SettingEntry> typical()
{
    return {s("RenderFarClip", PrefValue::Type::Float, "256"),
            s("ShowHoverTips", PrefValue::Type::Bool, "0"),
            s("UIScaleFactor", PrefValue::Type::Float, "1.25"),
            s("RenderCVDMode", PrefValue::Type::Int, "2"),
            s("RememberPassword", PrefValue::Type::Bool, "1"),
            s("SW67StreamKey", PrefValue::Type::String, "live_abc"),
            s("KingJahmirApiToken", PrefValue::Type::String, "sk-123"),
            s("RestrainedLove", PrefValue::Type::Bool, "1"),
            s("SW67CaptioningEnabled", PrefValue::Type::Bool, "1"),
            s("CacheLocation", PrefValue::Type::String, "C:/cache"),
            s("WindowWidth", PrefValue::Type::Int, "1920"),
            s("MediaURL", PrefValue::Type::String, "https://dj:hunter2@stream.example/live"),
            s("NotificationNearbyChatOptions", PrefValue::Type::String, "toast"),
            s("AvatarNameTagMode", PrefValue::Type::Int, "1", Scope::Global),
            s("LastSnapshotFolder", PrefValue::Type::String, "pics", Scope::Account)};
}

CurrentSettings currentOf(const std::vector<SettingEntry>& entries)
{
    CurrentSettings c;
    for (const SettingEntry& e : entries) c[{e.scope, e.name}] = e.value;
    return c;
}
} // namespace

TEST_CASE("A backup never holds a password, key, token or stream credential", "[backup]")
{
    const BuildResult r = makeBackup(typical(), {}, {});
    for (const char* name : {"RememberPassword", "SW67StreamKey", "KingJahmirApiToken", "MediaURL"})
    {
        CHECK_FALSE(hasSetting(r.backup, name));
    }
    const std::string text = writeBackup(r.backup);
    for (const char* secret : {"live_abc", "sk-123", "hunter2"})
    {
        CHECK(text.find(secret) == std::string::npos);
    }
    CHECK(r.excluded.size() >= 4);
}

TEST_CASE("Consent opt-ins and this computer's own settings stay out", "[backup]")
{
    const BuildResult r = makeBackup(typical(), {}, {});
    CHECK_FALSE(hasSetting(r.backup, "RestrainedLove"));
    CHECK_FALSE(hasSetting(r.backup, "SW67CaptioningEnabled"));
    CHECK_FALSE(hasSetting(r.backup, "CacheLocation"));
    CHECK_FALSE(hasSetting(r.backup, "WindowWidth"));
    CHECK(hasSetting(r.backup, "RenderFarClip"));
    CHECK(hasSetting(r.backup, "LastSnapshotFolder"));
}

TEST_CASE("Accessibility settings go in only when chosen", "[backup]")
{
    BackupOptions with;
    CHECK(hasSetting(makeBackup(typical(), {}, with).backup, "UIScaleFactor"));
    BackupOptions without;
    without.includeAccessibility = false;
    const Backup b = makeBackup(typical(), {}, without).backup;
    CHECK_FALSE(hasSetting(b, "UIScaleFactor"));
    CHECK_FALSE(hasSetting(b, "RenderCVDMode"));
    CHECK_FALSE(b.hasAccessibility);
}

TEST_CASE("A backup round-trips through its file", "[backup]")
{
    BackupOptions o;
    o.created = "2026-11-17 20:30";
    o.note = "Before reinstall";
    const Backup b = makeBackup(typical(), {{"workspaces/Desk.sw67workspace.json", "{\"a\":1}"},
                                             {"account/toolbars.xml", "<toolbars/>"}},
                                o)
                         .backup;
    const ReadResult back = readBackup(writeBackup(b));
    REQUIRE(back.backup);
    CHECK(*back.backup == b);
    CHECK(back.notes.empty());
}

TEST_CASE("Only our own files go in a backup, within the size limits", "[backup]")
{
    const std::string big(kMaxFileBytes + 1, 'x');
    const BuildResult r = makeBackup({}, {{"workspaces/a.sw67workspace.json", "{}"},
                                          {"../settings.xml", "<x/>"},
                                          {"workspaces/../../etc/passwd.json", "{}"},
                                          {"/abs/file.json", "{}"},
                                          {"workspaces/notes.txt", "hi"},
                                          {"outfits/big.sw67outfit.json", big}},
                                     {});
    REQUIRE(r.backup.files.size() == 1);
    CHECK(r.backup.files[0].path == "workspaces/a.sw67workspace.json");
    CHECK(r.excluded.size() == 5);
}

TEST_CASE("A hand-edited backup cannot carry a secret or switch RLV on", "[backup]")
{
    const std::string text = R"({"format":"sw67.backup","version":1,"hasAccessibility":false,
      "settings":[{"scope":"global","name":"RestrainedLove","type":"bool","value":"1"},
                  {"scope":"global","name":"MyPassword","type":"string","value":"x"},
                  {"scope":"global","name":"UIScaleFactor","type":"float","value":"2"},
                  {"scope":"global","name":"RenderFarClip","type":"float","value":"128"},
                  {"scope":"global","name":"ShowHoverTips","type":"bool","value":"maybe"}],
      "files":[{"path":"../../x.json","content":"{}"}],
      "surprise":true})";
    const ReadResult r = readBackup(text);
    REQUIRE(r.backup);
    REQUIRE(r.backup->settings.size() == 1);
    CHECK(r.backup->settings[0].name == "RenderFarClip");
    CHECK(r.backup->files.empty());
    CHECK(r.notes.size() == 6);
}

TEST_CASE("Newer and foreign files are refused", "[backup]")
{
    CHECK_FALSE(readBackup(R"({"format":"sw67.backup","version":2})").backup);
    CHECK_FALSE(readBackup(R"({"format":"sw67.workspace","version":1})").backup);
    CHECK_FALSE(readBackup("not json").backup);
    CHECK_FALSE(readBackup("not json").notes.empty());
}

TEST_CASE("The restore preview lists exactly what changes, and nothing is deleted", "[backup]")
{
    const Backup b = makeBackup(typical(), {{"workspaces/new.sw67workspace.json", "{\"n\":1}"},
                                             {"workspaces/same.sw67workspace.json", "{\"s\":1}"},
                                             {"workspaces/mine.sw67workspace.json", "{\"backup\":1}"}},
                                {})
                         .backup;
    CurrentSettings now = currentOf(typical());
    now[{Scope::Global, "RenderFarClip"}] = PrefValue{PrefValue::Type::Float, "512"};
    now[{Scope::Global, "UIScaleFactor"}] = PrefValue{PrefValue::Type::Float, "1.0"};
    now.erase({Scope::Account, "LastSnapshotFolder"});
    now[{Scope::Global, "ShowHoverTips"}] = PrefValue{PrefValue::Type::Int, "0"};
    const std::map<std::string, std::string> files = {{"workspaces/same.sw67workspace.json", "{\"s\":1}"},
                                                      {"workspaces/mine.sw67workspace.json", "{\"mine\":1}"},
                                                      {"workspaces/other.sw67workspace.json", "{}"}};
    const RestorePlan plan = planRestore(b, now, files, {});
    std::map<std::string, RestoreStep::What> what;
    for (const RestoreStep& s : plan.steps) what[s.target] = s.what;
    CHECK(what["RenderFarClip"] == RestoreStep::What::SetSetting);
    CHECK(what["UIScaleFactor"] == RestoreStep::What::SetSetting);
    CHECK(what["LastSnapshotFolder"] == RestoreStep::What::SkipUnknown);
    CHECK(what["ShowHoverTips"] == RestoreStep::What::SkipType);
    CHECK(what["AvatarNameTagMode"] == RestoreStep::What::SkipSame);
    CHECK(what["workspaces/new.sw67workspace.json"] == RestoreStep::What::WriteFile);
    CHECK(what["workspaces/same.sw67workspace.json"] == RestoreStep::What::SkipSame);
    CHECK(what["workspaces/mine.sw67workspace.json"] == RestoreStep::What::KeepFile);
    CHECK_FALSE(what.count("workspaces/other.sw67workspace.json")); // never touched, never deleted
    CHECK(plan.changeCount() == 3);
    CHECK(plan.describe().find("RenderFarClip: 512 -> 256") != std::string::npos);

    RestoreOptions replace;
    replace.replaceDifferentFiles = true;
    replace.accessibility = false;
    const RestorePlan p2 = planRestore(b, now, files, replace);
    for (const RestoreStep& s : p2.steps)
    {
        if (s.target == "workspaces/mine.sw67workspace.json") CHECK(s.what == RestoreStep::What::WriteFile);
        if (s.target == "UIScaleFactor") CHECK(s.what == RestoreStep::What::SkipPolicy);
    }
}

TEST_CASE("Restoring twice changes nothing the second time, and the undo point puts it back", "[backup]")
{
    const Backup b = makeBackup(typical(), {}, {}).backup;
    CurrentSettings now = currentOf(typical());
    now[{Scope::Global, "RenderFarClip"}] = PrefValue{PrefValue::Type::Float, "64"};
    const RestorePlan plan = planRestore(b, now, {}, {});
    REQUIRE(plan.changeCount() == 1);
    const Backup undo = undoPointFor(plan, now, "now");
    REQUIRE(undo.settings.size() == 1);
    CHECK(undo.settings[0].value.value == "64");

    CurrentSettings after = now;
    for (const SettingEntry& e : b.settings)
        if (after.count({e.scope, e.name})) after[{e.scope, e.name}] = e.value;
    CHECK(planRestore(b, after, {}, {}).changeCount() == 0);
    const RestorePlan back = planRestore(*readBackup(writeBackup(undo)).backup, after, {}, {});
    REQUIRE(back.changeCount() == 1);
    CHECK(back.steps[0].target == "RenderFarClip");
}

TEST_CASE("Credential-looking values are recognised", "[backup]")
{
    CHECK(valueLooksSecret("https://u:p@host/x"));
    CHECK(valueLooksSecret("rtmp://a.b/live?streamkey=abc"));
    CHECK(valueLooksSecret("-----BEGIN PRIVATE KEY-----"));
    CHECK(valueLooksSecret("Bearer abc"));
    CHECK_FALSE(valueLooksSecret("https://user@host/x"));
    CHECK_FALSE(valueLooksSecret("https://example.com/a:b@c"));
    CHECK_FALSE(valueLooksSecret("toast"));
}
