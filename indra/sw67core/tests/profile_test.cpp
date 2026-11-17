/**
 * @file profile_test.cpp
 * @brief Tests that every built-in preset meets the framework's contrast
 *        and distinguishability rules, and that profiles survive a bad
 *        settings file.
 *
 * Copyright (c) 2026, 6|7 World Studio contributors. LGPL-2.1.
 */

#include "sw67/a11y/profile.h"

#include <catch2/catch_test_macros.hpp>

#include <set>

using namespace sw67::a11y;

namespace
{
std::string join(const std::vector<PaletteIssue>& issues)
{
    std::string s;
    for (const auto& i : issues) s += i.what + "\n";
    return s;
}
} // namespace

TEST_CASE("Each colour-vision preset passes validation for its own vision type", "[a11y]")
{
    for (PalettePreset p : {PalettePreset::Standard, PalettePreset::Protanopia, PalettePreset::Deuteranopia,
                            PalettePreset::Tritanopia})
    {
        const auto issues = validatePalette(presetPalette(p), presetVision(p));
        INFO("preset " << presetKey(p) << "\n" << join(issues));
        CHECK(issues.empty());
    }
}

TEST_CASE("High contrast passes the strict (AAA text) rules", "[a11y]")
{
    const auto issues = validatePalette(presetPalette(PalettePreset::HighContrast), VisionType::Typical, true);
    INFO(join(issues));
    CHECK(issues.empty());
}

TEST_CASE("The standard palette is not relied on for red-green dichromats", "[a11y]")
{
    // A guard on the validator itself: the standard green/red pair should be
    // flagged under protanopia, which is why the protanopia preset exists.
    const auto issues = validatePalette(presetPalette(PalettePreset::Standard), VisionType::Protanopia);
    CHECK_FALSE(issues.empty());
}

TEST_CASE("Every status has a distinct icon, shape and label in every preset", "[a11y]")
{
    for (PalettePreset p : {PalettePreset::Standard, PalettePreset::Protanopia, PalettePreset::Deuteranopia,
                            PalettePreset::Tritanopia, PalettePreset::HighContrast})
    {
        const InterfacePalette palette = presetPalette(p);
        std::set<int> shapes;
        std::set<std::string_view> icons, labels;
        for (std::size_t i = 0; i < kStatusRoleCount; ++i)
        {
            const StatusIndicator s = indicator(palette, static_cast<StatusRole>(i));
            CHECK_FALSE(s.icon.empty());
            CHECK_FALSE(s.label.empty());
            shapes.insert(static_cast<int>(s.shape));
            CHECK_FALSE(shapeGlyph(s.shape).empty());
            icons.insert(s.icon);
            labels.insert(s.label);
        }
        CHECK(shapes.size() == kStatusRoleCount);
        CHECK(icons.size() == kStatusRoleCount);
        CHECK(labels.size() == kStatusRoleCount);
    }
}

TEST_CASE("Custom overrides apply only to the Custom preset", "[a11y]")
{
    AccessibilityProfile prof;
    prof.overrides["status.error"] = Rgb8{255, 0, 0};
    CHECK(resolvePalette(prof).statusColour(StatusRole::Error) == presetPalette(PalettePreset::Standard).statusColour(StatusRole::Error));
    prof.preset = PalettePreset::Custom;
    CHECK(resolvePalette(prof).statusColour(StatusRole::Error) == Rgb8{255, 0, 0});
}

TEST_CASE("Text scale is clamped and never yields unreadable sizes", "[a11y]")
{
    AccessibilityProfile prof;
    prof.textScale = 1.5;
    CHECK(scaledFontPx(prof, 12) == 18);
    prof.textScale = 100.0;
    CHECK(scaledFontPx(prof, 12) == 36);
    prof.textScale = 0.01;
    CHECK(scaledFontPx(prof, 10) == 8);
}

TEST_CASE("Profiles round trip through settings", "[a11y]")
{
    AccessibilityProfile prof;
    prof.preset = PalettePreset::Custom;
    prof.textScale = 1.25;
    prof.keyboardFocusHighlight = false;
    prof.focusRingWidthPx = 4;
    prof.reduceMotion = true;
    prof.overrides["text"] = Rgb8{250, 250, 240};
    prof.overrides["status.warning"] = Rgb8{255, 200, 0};
    prof.sceneFilter.mode = SceneColorFilter::Mode::Compensate;
    prof.sceneFilter.vision = VisionType::Deuteranopia;
    prof.sceneFilter.strength = 0.5;
    CHECK(deserialize(serialize(prof)) == prof);
}

TEST_CASE("A malformed settings file falls back to safe defaults", "[a11y]")
{
    const AccessibilityProfile prof = deserialize({
        {"preset", "rainbow"},
        {"text_scale", "huge"},
        {"focus_ring_width", "99"},
        {"override.text", "not-a-colour"},
        {"override.nonsense", "#FFFFFF"},
        {"scene_filter.mode", "simulate"},
        {"scene_filter.vision", "typical"},
        {"scene_filter.strength", "7"},
    });
    CHECK(prof.preset == PalettePreset::Standard);
    CHECK(prof.textScale == 1.0);
    CHECK(prof.focusRingWidthPx == 6);
    CHECK(prof.overrides.empty());
    CHECK(prof.sceneFilter.mode == SceneColorFilter::Mode::Off);
    CHECK(prof.sceneFilter.strength == 1.0);
}

TEST_CASE("Choosing an interface preset never turns on the scene filter or puts it in snapshots", "[a11y]")
{
    for (PalettePreset p : {PalettePreset::Protanopia, PalettePreset::Deuteranopia, PalettePreset::Tritanopia,
                            PalettePreset::HighContrast})
    {
        auto values = serialize(AccessibilityProfile{});
        values["preset"] = std::string(presetKey(p));
        const AccessibilityProfile prof = deserialize(values);
        CHECK(prof.sceneFilter.mode == SceneColorFilter::Mode::Off);
        CHECK_FALSE(prof.sceneFilter.includeInSnapshots);
    }
}
