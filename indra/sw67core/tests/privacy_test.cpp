/**
 * @file privacy_test.cpp
 * @brief Recording Privacy Mode: hides what it says, touches nothing else,
 *        turns off exactly, keeps the user's own changes, survives a restart.
 *
 * Copyright (c) 2026, 6|7 World Studio contributors. LGPL-2.1.
 */

#include "sw67/privacy/recording.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>

using namespace sw67::privacy;

namespace
{
Values viewerDefaults()
{
    return {{"RenderNameShowSelf", "1"},
            {"AvatarNameTagMode", "1"},
            {"NameTagShowGroupTitles", "1"},
            {"NotificationNearbyChatOptions", "toast"},
            {"NotificationFriendIMOptions", "toast"},
            {"NotificationNonFriendIMOptions", "toast"},
            {"NotificationConferenceIMOptions", "toast"},
            {"NotificationGroupChatOptions", "toast"},
            {"NotificationObjectIMOptions", "toast"},
            {"UseChatBubbles", "0"},
            {"ShowNavbarNavigationPanel", "1"},
            {"ShowMiniLocationPanel", "0"},
            {"ObscureBalanceInStatusBar", "0"},
            {"RenderBalanceInSnapshot", "1"},
            {"ShowHoverTips", "1"},
            {"RenderFarClip", "256"}};
}

Values applied(Values v, const sw67::perf::ChangePlan& plan)
{
    for (const auto& c : plan.changes) v[c.setting] = c.to;
    return v;
}

bool changes(const sw67::perf::ChangePlan& plan, const std::string& name)
{
    return std::any_of(plan.changes.begin(), plan.changes.end(), [&](const auto& c) { return c.setting == name; });
}
} // namespace

TEST_CASE("Every option has a label, a key and at least one setting", "[privacy]")
{
    for (int i = 0; i < static_cast<int>(Hide::Count); ++i)
    {
        const Hide h = static_cast<Hide>(i);
        CHECK_FALSE(hideLabel(h).empty());
        CHECK(parseHide(hideKey(h)) == h);
        CHECK_FALSE(settingsFor(h).empty());
    }
}

TEST_CASE("Turning privacy mode on hides names, pop-ups, location and balance, and nothing else", "[privacy]")
{
    const Values now = viewerDefaults();
    const auto plan = planEnable({}, now);
    CHECK(changes(plan, "RenderNameShowSelf"));
    CHECK(changes(plan, "AvatarNameTagMode"));
    CHECK(changes(plan, "NotificationFriendIMOptions"));
    CHECK(changes(plan, "ObscureBalanceInStatusBar"));
    CHECK(changes(plan, "ShowNavbarNavigationPanel"));
    // Already hidden: not listed.
    CHECK_FALSE(changes(plan, "UseChatBubbles"));
    CHECK_FALSE(changes(plan, "ShowMiniLocationPanel"));
    for (const auto& c : plan.changes) CHECK(isManagedSetting(c.setting));
    CHECK_FALSE(changes(plan, "RenderFarClip"));
    CHECK(plan.disclaimer.find("nobody else") != std::string::npos);
}

TEST_CASE("Options can leave things showing", "[privacy]")
{
    Options o;
    o.hide[Hide::OtherNames] = false;
    o.hide[Hide::Balance] = false;
    const auto plan = planEnable(o, viewerDefaults());
    CHECK_FALSE(changes(plan, "AvatarNameTagMode"));
    CHECK_FALSE(changes(plan, "ObscureBalanceInStatusBar"));
    CHECK(changes(plan, "RenderNameShowSelf"));
}

TEST_CASE("Turning it off puts back exactly what it changed", "[privacy]")
{
    const Values before = viewerDefaults();
    const auto plan = planEnable({}, before);
    const State state = stateFor({}, plan);
    const Values during = applied(before, plan);
    CHECK(planEnable({}, during).empty());

    const DisablePlan off = planDisable(state, during);
    CHECK(off.kept.empty());
    CHECK(applied(during, off.plan) == before);
}

TEST_CASE("A setting the user changed while privacy mode was on is left as they set it", "[privacy]")
{
    const Values before = viewerDefaults();
    const auto plan = planEnable({}, before);
    const State state = stateFor({}, plan);
    Values during = applied(before, plan);
    during["AvatarNameTagMode"] = "2"; // the user chose another mode themselves
    const DisablePlan off = planDisable(state, during);
    REQUIRE(off.kept.size() == 1);
    CHECK(off.kept[0] == "AvatarNameTagMode");
    CHECK(applied(during, off.plan).at("AvatarNameTagMode") == "2");
    CHECK(applied(during, off.plan).at("RenderNameShowSelf") == "1");
}

TEST_CASE("The state survives a restart and cannot be tampered into other settings", "[privacy]")
{
    const auto plan = planEnable({}, viewerDefaults());
    Options o;
    o.hide[Hide::HoverTips] = false;
    const State state = stateFor(o, plan);
    const auto back = readState(writeState(state));
    REQUIRE(back);
    CHECK(back->active);
    CHECK(back->before == state.before);
    CHECK(back->set == state.set);
    CHECK_FALSE(back->options.hides(Hide::HoverTips));

    const auto tampered = readState(
        R"({"format":"sw67.privacy","version":1,"active":true,"before":{"RestrainedLove":"1","ShowHoverTips":"1"},"set":{}})");
    REQUIRE(tampered);
    CHECK(tampered->before.size() == 1);
    CHECK(tampered->before.count("ShowHoverTips"));
    CHECK_FALSE(readState(R"({"format":"sw67.privacy","version":9})"));
}

TEST_CASE("The status line says on or off in words", "[privacy]")
{
    CHECK(statusLine(false, 0) == "Recording privacy mode is off.");
    CHECK(statusLine(true, 9).find("on: 9 things") != std::string::npos);
}
