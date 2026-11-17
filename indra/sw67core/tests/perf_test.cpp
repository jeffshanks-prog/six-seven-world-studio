/**
 * @file perf_test.cpp
 * @brief Performance Assistant: measured findings are kept apart from
 *        estimates, nothing promises a frame rate, presets touch only the
 *        viewer's own managed settings, and restore puts them back.
 *
 * Copyright (c) 2026, 6|7 World Studio contributors. LGPL-2.1.
 */

#include "sw67/perf/assistant.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cctype>
#include <string>

using namespace sw67::perf;

namespace
{
std::string lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

GraphicsSettings defaults()
{
    GraphicsSettings g;
    g.values = {{"RenderFarClip", "256.0"},        {"RenderShadowDetail", "2"},     {"RenderAvatarMaxNonImpostors", "12"},
                {"RenderAvatarMaxComplexity", "0"}, {"RenderDeferredSSAO", "1"},     {"RenderDepthOfField", "0"},
                {"RenderMaxPartCount", "4096"},    {"RenderVolumeLODFactor", "1.0"}, {"RenderMaxTextureResolution", "2048"},
                {"RenderScreenSpaceReflections", "0"}, {"RenderGlow", "1"}};
    return g;
}

const Finding* find(const std::vector<Finding>& f, const std::string& id)
{
    for (const Finding& x : f) if (x.id == id) return &x;
    return nullptr;
}

const std::vector<Preset> kAll{Preset::Everyday, Preset::Shopping, Preset::Building, Preset::Photography, Preset::Event};
} // namespace

TEST_CASE("A slow, crowded scene gets measured facts first, then estimates", "[perf]")
{
    Sample s;
    s.fps = 14.0;
    s.frameTimeMs = 71.4;
    s.avatarsNearby = 30;
    s.avatarsFullyDrawn = 12;
    s.texturesPending = 250;
    s.packetLossPercent = 3.5;
    const auto findings = explain(s, defaults());

    REQUIRE(find(findings, "low_fps"));
    CHECK(find(findings, "low_fps")->basis == Basis::Observed);
    CHECK(find(findings, "textures_loading")->basis == Basis::Observed);
    CHECK(find(findings, "network")->basis == Basis::Observed);
    CHECK(find(findings, "crowd")->basis == Basis::Estimated);
    CHECK(find(findings, "shadows")->basis == Basis::Estimated);
    CHECK(find(findings, "draw_distance")->basis == Basis::Estimated);

    // Measured findings all come before any estimate.
    bool seenEstimate = false;
    for (const Finding& f : findings)
    {
        if (f.basis == Basis::Estimated) seenEstimate = true;
        else CHECK_FALSE(seenEstimate);
    }
}

TEST_CASE("Nothing is inferred from measurements the viewer could not read", "[perf]")
{
    CHECK(explain(Sample{}, defaults()).empty());
    Sample s;
    s.fps = 60.0;
    const auto f = explain(s, defaults());
    REQUIRE(f.size() == 1);
    CHECK(f[0].id == "ok");
}

TEST_CASE("No finding, plan or disclaimer promises a frame rate", "[perf]")
{
    Sample s;
    s.fps = 9.0;
    s.avatarsFullyDrawn = 20;
    s.attachmentsHUDs = 6;
    s.textureMemoryUsedMB = 1900;
    s.textureMemoryBudgetMB = 2000;
    s.pingMs = 450;
    std::vector<std::string> texts;
    for (const Finding& f : explain(s, defaults())) texts.push_back(f.title + " " + f.explanation);
    for (Preset p : kAll)
    {
        const ChangePlan plan = planPreset(p, defaults());
        texts.push_back(plan.disclaimer);
        for (const SettingChange& c : plan.changes) texts.push_back(c.why);
    }
    for (const std::string& t : texts)
    {
        const std::string l = lower(t);
        CHECK(l.find("guarantee") == std::string::npos);
        CHECK(l.find("will improve") == std::string::npos);
        CHECK(l.find("will raise") == std::string::npos);
        CHECK(l.find("will fix") == std::string::npos);
    }
    CHECK(lower(planPreset(Preset::Event, defaults()).disclaimer).find("no frame rate is promised") != std::string::npos);
}

TEST_CASE("Presets change only the viewer's own managed graphics settings", "[perf]")
{
    for (Preset p : kAll)
    {
        CAPTURE(presetName(p));
        CHECK_FALSE(presetPurpose(p).empty());
        CHECK(parsePreset(presetKey(p)) == p);
        for (const auto& [name, v] : presetValues(p))
        {
            CHECK(isManagedSetting(name));
            CHECK(name.rfind("Render", 0) == 0);
            CHECK_FALSE(v.why.empty());
        }
    }
}

TEST_CASE("Preview lists real changes only, and restore returns every value", "[perf]")
{
    GraphicsSettings now = defaults();
    const ChangePlan plan = planPreset(Preset::Event, now);
    REQUIRE_FALSE(plan.empty());
    for (const SettingChange& c : plan.changes) CHECK(c.from != c.to);
    // RenderGlow is already 1 and RenderVolumeLODFactor "1.0" equals "1.0": not listed.
    CHECK(std::none_of(plan.changes.begin(), plan.changes.end(), [](const SettingChange& c) {
        return c.setting == "RenderGlow" || c.setting == "RenderVolumeLODFactor";
    }));

    const RestorePoint point = makeRestorePoint(plan, now, "before Event");
    GraphicsSettings after = now;
    for (const SettingChange& c : plan.changes) after.values[c.setting] = c.to;
    CHECK(planPreset(Preset::Event, after).empty()); // applying twice does nothing

    const ChangePlan undo = planRestore(point, after);
    for (const SettingChange& c : undo.changes) after.values[c.setting] = c.to;
    CHECK(after.values == now.values);

    // The restore point survives a restart, and a tampered one cannot add
    // settings outside the managed list.
    const auto back = readRestorePoint(writeRestorePoint(point));
    REQUIRE(back);
    CHECK(back->values == point.values);
    const auto tampered = readRestorePoint(
        R"({"format":"sw67.perf.restore","version":1,"label":"x","values":{"RestrainedLove":"1","RenderFarClip":"64"}})");
    REQUIRE(tampered);
    CHECK(tampered->values.size() == 1);
    CHECK(tampered->values.count("RenderFarClip") == 1);
}
