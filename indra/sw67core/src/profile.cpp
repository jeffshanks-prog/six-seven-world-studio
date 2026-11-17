/**
 * @file profile.cpp
 * @brief Accessibility presets, validation and settings round trip.
 *
 * Copyright (c) 2026, 6|7 World Studio contributors.
 *
 * The source code in this file is provided to you under the terms of the
 * GNU Lesser General Public License, version 2.1, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE. Terms of the LGPL can be found in doc/LGPL-license.txt
 * in this distribution, or online at http://www.gnu.org/licenses/lgpl-2.1.txt
 */

#include "sw67/a11y/profile.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <set>

namespace sw67::a11y
{
namespace
{
Rgb8 hex(std::string_view s)
{
    // Built-in colours are literals; a typo is a programming error caught by
    // the tests, so fall back to magenta to make it obvious.
    return parseHex(s).value_or(Rgb8{255, 0, 255});
}

void setStatus(InterfacePalette& p, StatusRole role, std::string_view colour)
{
    p.status[static_cast<std::size_t>(role)] = hex(colour);
}

InterfacePalette darkBase()
{
    InterfacePalette p;
    p.background = hex("#16181D");
    p.surface = hex("#22252C");
    p.text = hex("#ECEFF4");
    p.mutedText = hex("#AEB6C2");
    p.focusRing = hex("#FFD866");
    setStatus(p, StatusRole::Neutral, "#9AA4B2");
    return p;
}

// One icon, shape and label per role, the same in every preset, so that a
// user switching presets keeps recognising statuses by shape.
struct RoleGlyph
{
    std::string_view icon;
    StatusShape shape;
    std::string_view label;
};

constexpr std::array<RoleGlyph, kStatusRoleCount> kGlyphs{{
    {"SW67_Status_Success", StatusShape::Circle, "OK"},
    {"SW67_Status_Warning", StatusShape::Triangle, "Warning"},
    {"SW67_Status_Error", StatusShape::Cross, "Error"},
    {"SW67_Status_Info", StatusShape::Square, "Info"},
    {"SW67_Status_Neutral", StatusShape::Ring, "Idle"},
    {"SW67_Status_Restricted", StatusShape::DiamondOutline, "Restricted"},
    {"SW67_Status_Live", StatusShape::Diamond, "Live"},
}};

constexpr std::array<std::string_view, kStatusRoleCount> kRoleKeys{
    "success", "warning", "error", "info", "neutral", "restricted", "live"};

std::optional<double> parseDouble(const std::string& s)
{
    double v = 0.0;
    auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
    if (ec != std::errc() || ptr != s.data() + s.size() || !std::isfinite(v))
    {
        return std::nullopt;
    }
    return v;
}

std::optional<int> parseInt(const std::string& s)
{
    int v = 0;
    auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
    if (ec != std::errc() || ptr != s.data() + s.size())
    {
        return std::nullopt;
    }
    return v;
}

std::optional<bool> parseBool(const std::string& s)
{
    if (s == "1" || s == "true") return true;
    if (s == "0" || s == "false") return false;
    return std::nullopt;
}

std::string formatDouble(double v)
{
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.3f", v);
    return buf;
}

bool applyOverride(InterfacePalette& p, const std::string& key, const Rgb8& c)
{
    if (key == "background") { p.background = c; return true; }
    if (key == "surface") { p.surface = c; return true; }
    if (key == "text") { p.text = c; return true; }
    if (key == "mutedText") { p.mutedText = c; return true; }
    if (key == "focusRing") { p.focusRing = c; return true; }
    for (std::size_t i = 0; i < kStatusRoleCount; ++i)
    {
        if (key == std::string("status.") + std::string(kRoleKeys[i]))
        {
            p.status[i] = c;
            return true;
        }
    }
    return false;
}

constexpr std::array<PalettePreset, 6> kPresets{PalettePreset::Standard,   PalettePreset::Protanopia,
                                                PalettePreset::Deuteranopia, PalettePreset::Tritanopia,
                                                PalettePreset::HighContrast, PalettePreset::Custom};
} // namespace

InterfacePalette presetPalette(PalettePreset preset)
{
    InterfacePalette p = darkBase();
    switch (preset)
    {
    case PalettePreset::Standard:
    case PalettePreset::Custom:
        setStatus(p, StatusRole::Success, "#3FB950");
        setStatus(p, StatusRole::Warning, "#E3B341");
        setStatus(p, StatusRole::Error, "#FF6B61");
        setStatus(p, StatusRole::Info, "#58A6FF");
        setStatus(p, StatusRole::Restricted, "#C297FF");
        setStatus(p, StatusRole::Live, "#FF7BC0");
        break;
    case PalettePreset::Protanopia:
    case PalettePreset::Deuteranopia:
        // Okabe-Ito hues on the blue/orange axis, which red-green
        // dichromats keep.
        p.focusRing = hex("#F0E442");
        setStatus(p, StatusRole::Success, "#56B4E9");
        setStatus(p, StatusRole::Warning, "#F0E442");
        setStatus(p, StatusRole::Error, "#E69F00");
        setStatus(p, StatusRole::Info, "#E8E8FF");
        setStatus(p, StatusRole::Restricted, "#CC79A7");
        setStatus(p, StatusRole::Live, "#FF9E4A");
        break;
    case PalettePreset::Tritanopia:
        // The red/cyan axis, which blue-yellow dichromats keep.
        p.focusRing = hex("#00C7BE");
        setStatus(p, StatusRole::Success, "#00C7BE");
        setStatus(p, StatusRole::Warning, "#FFB0C8");
        setStatus(p, StatusRole::Error, "#FF4D6A");
        setStatus(p, StatusRole::Info, "#E8E8FF");
        setStatus(p, StatusRole::Restricted, "#B08CFF");
        setStatus(p, StatusRole::Live, "#FF4D6A");
        break;
    case PalettePreset::HighContrast:
        p.background = hex("#000000");
        p.surface = hex("#000000");
        p.text = hex("#FFFFFF");
        p.mutedText = hex("#E0E0E0");
        p.focusRing = hex("#FFFF00");
        setStatus(p, StatusRole::Neutral, "#D0D0D0");
        setStatus(p, StatusRole::Success, "#5CC8FF");
        setStatus(p, StatusRole::Warning, "#FFE500");
        setStatus(p, StatusRole::Error, "#FF9F1C");
        setStatus(p, StatusRole::Info, "#FFFFFF");
        setStatus(p, StatusRole::Restricted, "#FF85D0");
        setStatus(p, StatusRole::Live, "#FF9F1C");
        break;
    }
    return p;
}

InterfacePalette resolvePalette(const AccessibilityProfile& profile)
{
    InterfacePalette p = presetPalette(profile.preset);
    if (profile.preset == PalettePreset::Custom)
    {
        for (const auto& [key, colour] : profile.overrides)
        {
            applyOverride(p, key, colour);
        }
    }
    return p;
}

StatusIndicator indicator(const InterfacePalette& palette, StatusRole role)
{
    const auto& g = kGlyphs[static_cast<std::size_t>(role)];
    return {role, palette.statusColour(role), g.icon, g.shape, g.label};
}

std::string_view shapeGlyph(StatusShape shape)
{
    switch (shape)
    {
    case StatusShape::Circle: return "\xE2\x97\x8F";
    case StatusShape::Triangle: return "\xE2\x96\xB2";
    case StatusShape::Cross: return "\xE2\x9C\x96";
    case StatusShape::Square: return "\xE2\x96\xA0";
    case StatusShape::Diamond: return "\xE2\x97\x86";
    case StatusShape::DiamondOutline: return "\xE2\x97\x87";
    case StatusShape::Ring: return "\xE2\x97\x8B";
    }
    return "";
}

int scaledFontPx(const AccessibilityProfile& profile, int basePx)
{
    const double scale = std::clamp(profile.textScale, kMinTextScale, kMaxTextScale);
    return std::max(8, static_cast<int>(std::lround(basePx * scale)));
}

VisionType presetVision(PalettePreset preset)
{
    switch (preset)
    {
    case PalettePreset::Protanopia: return VisionType::Protanopia;
    case PalettePreset::Deuteranopia: return VisionType::Deuteranopia;
    case PalettePreset::Tritanopia: return VisionType::Tritanopia;
    default: return VisionType::Typical;
    }
}

std::string_view presetKey(PalettePreset preset)
{
    switch (preset)
    {
    case PalettePreset::Standard: return "standard";
    case PalettePreset::Protanopia: return "protanopia";
    case PalettePreset::Deuteranopia: return "deuteranopia";
    case PalettePreset::Tritanopia: return "tritanopia";
    case PalettePreset::HighContrast: return "high_contrast";
    case PalettePreset::Custom: return "custom";
    }
    return "standard";
}

std::string_view statusRoleKey(StatusRole role)
{
    return role == StatusRole::Count ? std::string_view{} : kRoleKeys[static_cast<std::size_t>(role)];
}

std::vector<PaletteIssue> validatePalette(const InterfacePalette& palette, VisionType vision, bool strict,
                                          double minDistance)
{
    std::vector<PaletteIssue> issues;
    auto need = [&](double ratio, double min, const std::string& what) {
        if (ratio + 1e-9 < min)
        {
            char buf[160];
            std::snprintf(buf, sizeof(buf), "%s contrast %.2f below %.1f", what.c_str(), ratio, min);
            issues.push_back({buf});
        }
    };

    const double textMin = strict ? 7.0 : 4.5;
    const double statusMin = strict ? 4.5 : 3.0;
    need(contrastRatio(palette.text, palette.background), textMin, "text on background");
    need(contrastRatio(palette.text, palette.surface), textMin, "text on surface");
    need(contrastRatio(palette.mutedText, palette.surface), 4.5, "muted text on surface");
    need(contrastRatio(palette.focusRing, palette.background), 3.0, "focus ring");
    for (std::size_t i = 0; i < kStatusRoleCount; ++i)
    {
        need(contrastRatio(palette.status[i], palette.background), statusMin,
             "status '" + std::string(kRoleKeys[i]) + "'");
    }

    const std::array<StatusRole, 5> distinct{StatusRole::Success, StatusRole::Warning, StatusRole::Error,
                                             StatusRole::Info, StatusRole::Restricted};
    std::vector<VisionType> visions{VisionType::Typical};
    if (vision != VisionType::Typical)
    {
        visions.push_back(vision);
    }
    for (VisionType v : visions)
    {
        for (std::size_t a = 0; a < distinct.size(); ++a)
        {
            for (std::size_t b = a + 1; b < distinct.size(); ++b)
            {
                const double d = perceptualDistance(simulate(palette.statusColour(distinct[a]), v),
                                                    simulate(palette.statusColour(distinct[b]), v));
                if (d < minDistance)
                {
                    char buf[200];
                    std::snprintf(buf, sizeof(buf), "'%s' and '%s' too similar for %s (%.3f)",
                                  kRoleKeys[static_cast<std::size_t>(distinct[a])].data(),
                                  kRoleKeys[static_cast<std::size_t>(distinct[b])].data(),
                                  std::string(visionTypeName(v)).c_str(), d);
                    issues.push_back({buf});
                }
            }
        }
    }

    // Never colour alone: every role needs its own shape, icon and label.
    std::set<StatusShape> shapes;
    std::set<std::string_view> icons, labels;
    for (const auto& g : kGlyphs)
    {
        if (g.icon.empty() || g.label.empty())
        {
            issues.push_back({"a status role has no icon or label"});
        }
        shapes.insert(g.shape);
        icons.insert(g.icon);
        labels.insert(g.label);
    }
    if (shapes.size() != kStatusRoleCount || icons.size() != kStatusRoleCount || labels.size() != kStatusRoleCount)
    {
        issues.push_back({"status roles share a shape, icon or label"});
    }
    return issues;
}

std::map<std::string, std::string> serialize(const AccessibilityProfile& profile)
{
    std::map<std::string, std::string> out;
    out["preset"] = std::string(presetKey(profile.preset));
    out["text_scale"] = formatDouble(std::clamp(profile.textScale, kMinTextScale, kMaxTextScale));
    out["focus_highlight"] = profile.keyboardFocusHighlight ? "1" : "0";
    out["focus_ring_width"] = std::to_string(std::clamp(profile.focusRingWidthPx, 1, 6));
    out["reduce_motion"] = profile.reduceMotion ? "1" : "0";
    for (const auto& [key, colour] : profile.overrides)
    {
        out["override." + key] = toHex(colour);
    }
    const char* mode = "off";
    if (profile.sceneFilter.mode == SceneColorFilter::Mode::Simulate) mode = "simulate";
    if (profile.sceneFilter.mode == SceneColorFilter::Mode::Compensate) mode = "compensate";
    out["scene_filter.mode"] = mode;
    out["scene_filter.vision"] = std::string(visionTypeKey(profile.sceneFilter.vision));
    out["scene_filter.strength"] = formatDouble(std::clamp(profile.sceneFilter.strength, 0.0, 1.0));
    out["scene_filter.in_snapshots"] = profile.sceneFilter.includeInSnapshots ? "1" : "0";
    return out;
}

AccessibilityProfile deserialize(const std::map<std::string, std::string>& values)
{
    AccessibilityProfile p;
    auto get = [&](const char* key) -> const std::string* {
        auto it = values.find(key);
        return it == values.end() ? nullptr : &it->second;
    };

    if (auto* v = get("preset"))
    {
        for (PalettePreset candidate : kPresets)
        {
            if (presetKey(candidate) == *v)
            {
                p.preset = candidate;
            }
        }
    }
    if (auto* v = get("text_scale"))
    {
        if (auto d = parseDouble(*v))
        {
            p.textScale = std::clamp(*d, kMinTextScale, kMaxTextScale);
        }
    }
    if (auto* v = get("focus_highlight"))
    {
        p.keyboardFocusHighlight = parseBool(*v).value_or(true);
    }
    if (auto* v = get("focus_ring_width"))
    {
        if (auto i = parseInt(*v))
        {
            p.focusRingWidthPx = std::clamp(*i, 1, 6);
        }
    }
    if (auto* v = get("reduce_motion"))
    {
        p.reduceMotion = parseBool(*v).value_or(false);
    }
    InterfacePalette scratch;
    for (const auto& [key, value] : values)
    {
        static const std::string prefix = "override.";
        if (key.rfind(prefix, 0) == 0)
        {
            const std::string name = key.substr(prefix.size());
            auto colour = parseHex(value);
            if (colour && applyOverride(scratch, name, *colour))
            {
                p.overrides[name] = *colour;
            }
        }
    }
    if (auto* v = get("scene_filter.mode"))
    {
        if (*v == "simulate") p.sceneFilter.mode = SceneColorFilter::Mode::Simulate;
        else if (*v == "compensate") p.sceneFilter.mode = SceneColorFilter::Mode::Compensate;
    }
    if (auto* v = get("scene_filter.vision"))
    {
        p.sceneFilter.vision = parseVisionType(*v).value_or(VisionType::Typical);
    }
    if (auto* v = get("scene_filter.strength"))
    {
        if (auto d = parseDouble(*v))
        {
            p.sceneFilter.strength = std::clamp(*d, 0.0, 1.0);
        }
    }
    if (auto* v = get("scene_filter.in_snapshots"))
    {
        p.sceneFilter.includeInSnapshots = parseBool(*v).value_or(false);
    }
    // A filter with no vision type to work for does nothing; keep it off.
    if (p.sceneFilter.vision == VisionType::Typical)
    {
        p.sceneFilter.mode = SceneColorFilter::Mode::Off;
    }
    return p;
}

} // namespace sw67::a11y
