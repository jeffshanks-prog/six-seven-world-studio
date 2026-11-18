/**
 * @file looks_test.cpp
 * @brief Photo Studio looks: only colour, lens and exposure settings,
 *        exact previews, exact revert that keeps the user's own changes.
 *
 * Copyright (c) 2026, 6|7 World Studio contributors. LGPL-2.1.
 */

#include "sw67/photo/looks.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>

using namespace sw67::photo;

namespace
{
Values defaults()
{
    return {{"RenderColorGrade", "0"},          {"RenderColorGradeWhiteBalanceCCT", "0"},
            {"RenderColorGradeBrightness", "0"}, {"RenderColorGradeContrast", "1"},
            {"RenderColorGradeSaturation", "1"}, {"RenderColorGradeVibrance", "0"},
            {"RenderColorGradeHighlights", "0"}, {"RenderColorGradeShadows", "0"},
            {"RenderVignetteAmount", "0"},       {"RenderFilmGrainAmount", "0"},
            {"RenderBloomStrength", "0.325"},    {"RenderExposure", "1"}};
}
} // namespace

TEST_CASE("Built-in looks only use look settings and stay in range", "[looks]")
{
    for (Look l : builtinLooks())
    {
        const Look before = l;
        REQUIRE(sanitise(l).empty());
        REQUIRE(l.settings.size() == before.settings.size());
        REQUIRE(l.builtin);
    }
    REQUIRE(builtinLooks().size() == 5);
}

TEST_CASE("A look's preview lists exactly what changes", "[looks]")
{
    const Look l = builtinLooks()[2]; // social_warm
    const auto plan = planLook(l, defaults());
    // ColorGrade 0->1, CCT 0->-1000, vibrance, contrast, vignette, plus the sky.
    REQUIRE(plan.changes.size() == 6);
    REQUIRE(std::any_of(plan.changes.begin(), plan.changes.end(),
                        [](const auto& c) { return c.setting == "Local sky" && c.to == "sunset"; }));
    REQUIRE(plan.disclaimer.find("Other residents") != std::string::npos);
    // Already applied: only the sky line is left.
    Values now = defaults();
    for (const auto& [k, v] : l.settings) now[k] = v;
    REQUIRE(planLook(l, now).changes.size() == 1);
}

TEST_CASE("Turning a look off restores exactly and keeps the user's own changes", "[looks]")
{
    const Look l = builtinLooks()[3]; // social_cool
    const Values before = defaults();
    const LookState state = stateFor(l, before);
    Values now = before;
    for (const auto& [k, v] : l.settings) now[k] = v;
    now["RenderVignetteAmount"] = "0.6"; // the user moved this slider
    const RevertPlan r = planRevert(state, now);
    REQUIRE(r.kept == std::vector<std::string>{"RenderVignetteAmount"});
    REQUIRE(r.restoreSky);
    for (const auto& c : r.plan.changes)
    {
        now[c.setting] = c.to;
    }
    for (const auto& [k, v] : before)
    {
        if (k == "RenderVignetteAmount") continue;
        REQUIRE(now[k] == v);
    }
    REQUIRE(now["RenderVignetteAmount"] == "0.6");
}

TEST_CASE("Custom looks capture, save and refuse other settings", "[looks]")
{
    Values cur = defaults();
    cur["RenderColorGradeSaturation"] = "1.4";
    cur["RenderShadowResolution"] = "4"; // not a look setting
    const Look mine = captureLook("My shop", cur, Sky::Midday);
    REQUIRE(mine.key == "custom_My_shop");
    REQUIRE(mine.settings.count("RenderShadowResolution") == 0);
    REQUIRE(mine.settings.at("RenderColorGradeSaturation") == "1.4");

    const LookState active = stateFor(mine, defaults());
    const LooksFile f = readLooks(writeLooks({mine}, active));
    REQUIRE(f.notes.empty());
    REQUIRE(f.custom.size() == 1);
    REQUIRE(f.custom[0].sky == Sky::Midday);
    REQUIRE(f.active);
    REQUIRE(f.active->set.at("RenderColorGradeSaturation") == "1.4");

    // A hand-edited file cannot slip in another setting or an absurd value.
    const LooksFile bad = readLooks(R"({"format":"sw67.photolooks","version":1,"looks":[{"name":"x","settings":
        {"RenderExposure":"99","DebugPassword":"1","RenderVignetteAmount":"abc"}}],
        "active":{"set":{"UseChatBubbles":"0"}}})");
    REQUIRE(bad.custom.size() == 1);
    REQUIRE(bad.custom[0].settings.size() == 1);
    REQUIRE(bad.custom[0].settings.at("RenderExposure") == "4");
    REQUIRE(bad.active);
    REQUIRE(bad.active->set.empty());
    REQUIRE_FALSE(readLooks("{}").custom.size());
}
