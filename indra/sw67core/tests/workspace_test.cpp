/**
 * @file workspace_test.cpp
 * @brief Workspace profiles: built-ins, export and import, apply preview and
 *        undo, and accessibility kept out of every profile.
 *
 * Copyright (c) 2026, 6|7 World Studio contributors. LGPL-2.1.
 */

#include "sw67/workspace/profile.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>

using namespace sw67::workspace;

namespace
{
CurrentState viewerWith(std::initializer_list<std::string> open, std::initializer_list<std::string> closed)
{
    CurrentState s;
    for (const std::string& n : open) s.windows[n] = {n, true, Rect{10, 600, 300, 400}};
    for (const std::string& n : closed) s.windows[n] = {n, false, Rect{20, 500, 320, 420}};
    return s;
}

bool contains(const std::vector<Change>& plan, Change::What what, const std::string& target)
{
    return std::any_of(plan.begin(), plan.end(), [&](const Change& c) { return c.what == what && c.target == target; });
}
} // namespace

TEST_CASE("The five built-in profiles exist and are clean", "[workspace]")
{
    const auto all = builtinProfiles();
    REQUIRE(all.size() == 5);
    for (const WorkspaceProfile& p : all)
    {
        WorkspaceProfile copy = p;
        CHECK(sanitize(copy).empty());
        CHECK_FALSE(p.windows.empty());
    }
    CHECK(builtinProfile(ProfileKind::Photographer).windows.front().name == "snapshot");
}

TEST_CASE("A profile exports and imports unchanged", "[workspace]")
{
    WorkspaceProfile p = builtinProfile(ProfileKind::Creator);
    p.name = "My build desk";
    p.closeOthers = true;
    p.windows[0].rect = Rect{40, 700, 640, 480};
    p.windows.push_back({"inventory_extra", false, std::nullopt});
    p.toolbarsXml = "<?xml version=\"1.0\"?><toolbars></toolbars>";
    p.preferences["ShowAxes"] = {PrefValue::Type::Bool, "1"};
    p.preferences["RenderVolumeLODFactor"] = {PrefValue::Type::Float, "2.5"};

    const ImportResult back = importProfile(exportProfile(p));
    REQUIRE(back.profile);
    CHECK(*back.profile == p);
}

TEST_CASE("Accessibility, consent and secret settings never enter a profile", "[workspace][a11y]")
{
    WorkspaceProfile p = builtinProfile(ProfileKind::DJ);
    p.preferences["UIScaleFactor"] = {PrefValue::Type::Float, "0.8"};
    p.preferences["SW67Accessibility"] = {PrefValue::Type::String, "x"};
    p.preferences["RestrainedLove"] = {PrefValue::Type::Bool, "1"};
    p.preferences["SW67StreamKey"] = {PrefValue::Type::String, "live_abc123"};
    p.preferences["ShowAxes"] = {PrefValue::Type::Bool, "1"};

    std::vector<Note> notes;
    const std::string text = exportProfile(p, &notes);
    CHECK(text.find("UIScaleFactor") == std::string::npos);
    CHECK(text.find("SW67Accessibility") == std::string::npos);
    CHECK(text.find("RestrainedLove") == std::string::npos);
    CHECK(text.find("live_abc123") == std::string::npos);
    CHECK(text.find("ShowAxes") != std::string::npos);
    CHECK(notes.size() == 4);

    // A hand-edited file that adds them back is cleaned on import, and the
    // apply plan never touches them.
    const std::string tampered = R"({"format":"sw67.workspace","version":1,"name":"x","kind":"custom","windows":[],
        "preferences":{"UIScaleFactor":{"type":"float","value":"0.5"},
                       "RenderCVDMode":{"type":"int","value":"0"},
                       "RestrainedLove":{"type":"bool","value":"1"}}})";
    const ImportResult in = importProfile(tampered);
    REQUIRE(in.profile);
    CHECK(in.profile->preferences.empty());

    CurrentState now;
    now.preferences["UIScaleFactor"] = {PrefValue::Type::Float, "1.5"};
    WorkspaceProfile direct;
    direct.name = "direct";
    direct.preferences["UIScaleFactor"] = {PrefValue::Type::Float, "0.5"};
    CHECK(planApply(direct, now).empty());
}

TEST_CASE("Import refuses newer formats and other files, and notes unknown fields", "[workspace]")
{
    CHECK_FALSE(importProfile("not json").profile);
    CHECK_FALSE(importProfile(R"({"format":"something.else","version":1})").profile);
    const ImportResult newer = importProfile(R"({"format":"sw67.workspace","version":99,"name":"future"})");
    CHECK_FALSE(newer.profile);
    REQUIRE_FALSE(newer.notes.empty());
    CHECK(newer.notes.front().text.find("newer version") != std::string::npos);

    const ImportResult extra = importProfile(R"({"format":"sw67.workspace","version":1,"name":"ok","futureThing":3})");
    REQUIRE(extra.profile);
    CHECK(std::any_of(extra.notes.begin(), extra.notes.end(),
                      [](const Note& n) { return n.text.find("futureThing") != std::string::npos; }));
}

TEST_CASE("Unsafe toolbar XML, bad window names and wild rectangles are removed", "[workspace]")
{
    WorkspaceProfile p;
    p.name = "x";
    p.toolbarsXml = "<!DOCTYPE x [<!ENTITY a \"aaaa\">]><toolbars>&a;</toolbars>";
    p.keyBindingsXml = "not xml at all";
    p.windows = {{"../evil", true, std::nullopt}, {"inventory", true, Rect{0, 0, 99999, 10}}, {"inventory", true, std::nullopt}};
    p.preferences["RenderFarClip"] = {PrefValue::Type::Float, "far"};
    const auto notes = sanitize(p);
    CHECK(p.toolbarsXml.empty());
    CHECK(p.keyBindingsXml.empty());
    REQUIRE(p.windows.size() == 1);
    CHECK(p.windows[0].name == "inventory");
    CHECK_FALSE(p.windows[0].rect);
    CHECK(p.preferences.empty());
    CHECK(notes.size() == 6);
}

TEST_CASE("Apply preview lists exactly what changes, and undo restores it", "[workspace]")
{
    CurrentState now = viewerWith({"people", "world_map", "sw67_workspaces"}, {"snapshot", "camera", "lightbox"});
    now.preferences["ShowAxes"] = {PrefValue::Type::Bool, "0"};

    WorkspaceProfile photo = builtinProfile(ProfileKind::Photographer);
    photo.closeOthers = true;
    photo.windows[0].rect = Rect{100, 800, 500, 600};
    photo.preferences["ShowAxes"] = {PrefValue::Type::Bool, "1"};
    photo.preferences["NoSuchSetting"] = {PrefValue::Type::Bool, "1"};

    const auto plan = planApply(photo, now);
    CHECK(contains(plan, Change::What::OpenWindow, "snapshot"));
    CHECK(contains(plan, Change::What::OpenWindow, "camera"));
    CHECK(contains(plan, Change::What::SkipUnknownWindow, "my_environments"));
    CHECK(contains(plan, Change::What::CloseWindow, "people"));
    CHECK(contains(plan, Change::What::CloseWindow, "world_map"));
    CHECK_FALSE(contains(plan, Change::What::CloseWindow, "sw67_workspaces")); // never closes itself
    CHECK(contains(plan, Change::What::SetPreference, "ShowAxes"));
    CHECK(contains(plan, Change::What::SkipPreference, "NoSuchSetting"));
    for (const Change& c : plan) CHECK_FALSE(c.describe().empty());

    // Simulate applying the plan, then undo with the restore point.
    const WorkspaceProfile undo = restorePoint(photo, now);
    CurrentState after = now;
    for (const Change& c : plan)
    {
        if (c.what == Change::What::OpenWindow) after.windows[c.target].open = true;
        if (c.what == Change::What::CloseWindow) after.windows[c.target].open = false;
        if (c.what == Change::What::SetPreference) after.preferences[c.target].value = c.to;
    }
    for (const auto& c : planApply(undo, after))
    {
        if (c.what == Change::What::OpenWindow) after.windows[c.target].open = true;
        if (c.what == Change::What::CloseWindow) after.windows[c.target].open = false;
        if (c.what == Change::What::SetPreference) after.preferences[c.target].value = c.to;
    }
    for (const auto& [name, w] : now.windows) CHECK(after.windows[name].open == w.open);
    CHECK(after.preferences["ShowAxes"].value == "0");
}

TEST_CASE("Applying the same profile twice changes nothing the second time", "[workspace]")
{
    CurrentState now = viewerWith({"snapshot", "camera", "lightbox", "my_environments"}, {});
    CHECK(planApply(builtinProfile(ProfileKind::Photographer), now).empty());
}
