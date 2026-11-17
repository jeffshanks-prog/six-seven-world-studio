/**
 * @file perf_assistant.cpp
 * @brief Performance findings, presets, previews and restore points.
 *
 * Copyright (c) 2026, 6|7 World Studio contributors.
 *
 * The source code in this file is provided to you under the terms of the
 * GNU Lesser General Public License, version 2.1, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE. Terms of the LGPL can be found in doc/LGPL-license.txt
 * in this distribution, or online at http://www.gnu.org/licenses/lgpl-2.1.txt
 */

#include "sw67/perf/assistant.h"

#include "sw67/io/json.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace sw67::perf
{
namespace
{
std::string num(double v, int decimals = 0)
{
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.*f", decimals, v);
    return buf;
}

constexpr std::string_view kDisclaimer =
    "These are changes to your own viewer's graphics settings. They can lower the work your computer does, "
    "but how much depends on the scene, your hardware and the region; no frame rate is promised. "
    "Nothing here changes other residents' objects or settings. You can restore the previous values at any time.";
} // namespace

std::optional<double> GraphicsSettings::number(std::string_view name) const
{
    const auto it = values.find(std::string(name));
    if (it == values.end() || it->second.empty())
    {
        return std::nullopt;
    }
    char* end = nullptr;
    const double v = std::strtod(it->second.c_str(), &end);
    if (!end || *end != '\0' || !std::isfinite(v))
    {
        return std::nullopt;
    }
    return v;
}

std::string_view basisLabel(Basis b)
{
    return b == Basis::Observed ? "Measured" : "Estimate";
}

std::vector<Finding> explain(const Sample& s, const GraphicsSettings& g)
{
    std::vector<Finding> observed;
    std::vector<Finding> estimated;

    const bool lowFps = s.fps && *s.fps < 30.0;

    if (s.fps)
    {
        if (*s.fps < 20.0)
        {
            observed.push_back({"low_fps", Basis::Observed, "Frame rate is low: " + num(*s.fps) + " fps",
                                "Measured over the last few seconds" +
                                    (s.frameTimeMs ? std::string(", about ") + num(*s.frameTimeMs, 1) + " ms a frame" : std::string()) +
                                    ". Below about 20 fps movement and camera turns look uneven.",
                                {}});
        }
        else if (*s.fps < 30.0)
        {
            observed.push_back({"modest_fps", Basis::Observed, "Frame rate is modest: " + num(*s.fps) + " fps",
                                "Measured over the last few seconds. Usable, but busy scenes may feel slow.", {}});
        }
    }

    if (s.textureMemoryUsedMB && s.textureMemoryBudgetMB && *s.textureMemoryBudgetMB > 0.0)
    {
        const double ratio = *s.textureMemoryUsedMB / *s.textureMemoryBudgetMB;
        if (ratio >= 0.9)
        {
            observed.push_back({"texture_memory", Basis::Observed,
                                "Texture memory is nearly full: " + num(*s.textureMemoryUsedMB) + " of " +
                                    num(*s.textureMemoryBudgetMB) + " MB",
                                "When texture memory is full the viewer lowers texture detail and fetches textures again, "
                                "which shows as blurry surfaces and slow loading.",
                                {"RenderMaxTextureResolution"}});
        }
    }

    if (s.texturesPending && *s.texturesPending >= 100)
    {
        observed.push_back({"textures_loading", Basis::Observed,
                            std::to_string(*s.texturesPending) + " textures are still loading",
                            "Common just after arriving somewhere busy. It usually settles; a slow or lossy "
                            "connection makes it take longer.",
                            {}});
    }

    if ((s.packetLossPercent && *s.packetLossPercent >= 2.0) || (s.pingMs && *s.pingMs >= 300.0))
    {
        std::string what;
        if (s.packetLossPercent && *s.packetLossPercent >= 2.0) what += "packet loss " + num(*s.packetLossPercent, 1) + "%";
        if (s.pingMs && *s.pingMs >= 300.0) what += std::string(what.empty() ? "" : ", ") + "ping " + num(*s.pingMs) + " ms";
        observed.push_back({"network", Basis::Observed, "The connection to the region is struggling (" + what + ")",
                            "This slows loading and makes movement and chat lag. Graphics settings do not change it; "
                            "a wired connection or another time of day may.",
                            {}});
    }

    if (lowFps && s.avatarsFullyDrawn && *s.avatarsFullyDrawn >= 8)
    {
        estimated.push_back({"crowd", Basis::Estimated,
                             "Many avatars are drawn in full (" + std::to_string(*s.avatarsFullyDrawn) + ")",
                             "Detailed avatars are often the largest cost in a crowd. Drawing fewer in full, or setting a "
                             "complexity limit, may raise the frame rate. Their owners are not affected.",
                             {"RenderAvatarMaxNonImpostors", "RenderAvatarMaxComplexity"}});
    }
    if (lowFps)
    {
        if (const auto shadows = g.number("RenderShadowDetail"); shadows && *shadows >= 1.0)
        {
            estimated.push_back({"shadows", Basis::Estimated, "Shadows are on",
                                 "Shadows draw much of the scene again from the sun's or a light's point of view. "
                                 "Turning them down may help on many computers.",
                                 {"RenderShadowDetail"}});
        }
        if (const auto far = g.number("RenderFarClip"); far && *far > 128.0)
        {
            estimated.push_back({"draw_distance", Basis::Estimated, "Draw distance is " + num(*far) + " m",
                                 "A longer draw distance brings in more objects and avatars to draw. A shorter one may help "
                                 "in busy areas.",
                                 {"RenderFarClip"}});
        }
        if (s.attachmentsHUDs && *s.attachmentsHUDs >= 4)
        {
            estimated.push_back({"huds", Basis::Estimated, "You are wearing " + std::to_string(*s.attachmentsHUDs) + " HUDs",
                                 "HUDs are drawn every frame. Detailed or many HUDs can cost frame time; taking off ones you "
                                 "are not using may help.",
                                 {}});
        }
    }

    if (observed.empty() && estimated.empty() && s.fps)
    {
        observed.push_back({"ok", Basis::Observed, "No obvious problem: " + num(*s.fps) + " fps",
                            "Nothing in the current measurements points to a slowdown.", {}});
    }

    observed.insert(observed.end(), estimated.begin(), estimated.end());
    return observed;
}

std::string_view presetName(Preset p)
{
    switch (p)
    {
    case Preset::Everyday: return "Everyday";
    case Preset::Shopping: return "Shopping";
    case Preset::Building: return "Building";
    case Preset::Photography: return "Photography";
    case Preset::Event: return "Event";
    }
    return "";
}

std::string_view presetKey(Preset p)
{
    switch (p)
    {
    case Preset::Everyday: return "everyday";
    case Preset::Shopping: return "shopping";
    case Preset::Building: return "building";
    case Preset::Photography: return "photography";
    case Preset::Event: return "event";
    }
    return "";
}

std::optional<Preset> parsePreset(std::string_view key)
{
    for (Preset p : {Preset::Everyday, Preset::Shopping, Preset::Building, Preset::Photography, Preset::Event})
    {
        if (presetKey(p) == key)
        {
            return p;
        }
    }
    return std::nullopt;
}

std::string_view presetPurpose(Preset p)
{
    switch (p)
    {
    case Preset::Everyday: return "A balance of looks and smoothness for exploring and socialising.";
    case Preset::Shopping: return "Close-up detail on products, fewer distant avatars and effects.";
    case Preset::Building: return "Full object detail near you, lighter avatars, shadows from the sun only.";
    case Preset::Photography: return "The best-looking settings, for still shots. Expect a lower frame rate.";
    case Preset::Event: return "For crowded clubs and events: short draw distance, avatars kept light.";
    }
    return "";
}

const std::vector<std::string>& managedSettings()
{
    static const std::vector<std::string> kManaged{
        "RenderAvatarMaxComplexity", "RenderAvatarMaxNonImpostors", "RenderDeferredSSAO", "RenderDepthOfField",
        "RenderFarClip",             "RenderGlow",                  "RenderMaxPartCount", "RenderMaxTextureResolution",
        "RenderScreenSpaceReflections", "RenderShadowDetail",       "RenderVolumeLODFactor",
    };
    return kManaged;
}

bool isManagedSetting(std::string_view name)
{
    const auto& m = managedSettings();
    return std::find(m.begin(), m.end(), name) != m.end();
}

std::map<std::string, PresetValue> presetValues(Preset p)
{
    // Values are the viewer's own units: metres, counts, Boolean 0/1,
    // RenderShadowDetail 0 none / 1 sun and moon / 2 also local lights,
    // RenderAvatarMaxComplexity 0 meaning no limit.
    switch (p)
    {
    case Preset::Everyday:
        return {{"RenderFarClip", {"128", "Enough to see across a parcel"}},
                {"RenderShadowDetail", {"1", "Sun and moon shadows only"}},
                {"RenderAvatarMaxNonImpostors", {"12", "Up to 12 avatars drawn in full"}},
                {"RenderAvatarMaxComplexity", {"250000", "Very complex avatars are drawn simply"}},
                {"RenderDeferredSSAO", {"1", "Ambient occlusion on"}},
                {"RenderDepthOfField", {"0", "Depth of field off"}},
                {"RenderMaxPartCount", {"4096", "The viewer's default particle count"}},
                {"RenderVolumeLODFactor", {"1.25", "Objects keep detail a little further away"}},
                {"RenderScreenSpaceReflections", {"0", "Screen-space reflections off"}},
                {"RenderGlow", {"1", "Glow on"}}};
    case Preset::Shopping:
        return {{"RenderFarClip", {"96", "Stores are close by"}},
                {"RenderShadowDetail", {"0", "Shadows off"}},
                {"RenderAvatarMaxNonImpostors", {"6", "Other shoppers drawn simply"}},
                {"RenderAvatarMaxComplexity", {"150000", "Very complex avatars are drawn simply"}},
                {"RenderDeferredSSAO", {"0", "Ambient occlusion off"}},
                {"RenderDepthOfField", {"0", "Depth of field off"}},
                {"RenderMaxPartCount", {"2048", "Fewer particles"}},
                {"RenderVolumeLODFactor", {"2.0", "Products keep their detail"}},
                {"RenderMaxTextureResolution", {"2048", "Full texture detail on products"}},
                {"RenderScreenSpaceReflections", {"0", "Screen-space reflections off"}}};
    case Preset::Building:
        return {{"RenderFarClip", {"128", "Your build and its surroundings"}},
                {"RenderShadowDetail", {"1", "Sun shadows, to judge placement"}},
                {"RenderAvatarMaxNonImpostors", {"4", "Visitors drawn simply"}},
                {"RenderAvatarMaxComplexity", {"150000", "Very complex avatars are drawn simply"}},
                {"RenderDeferredSSAO", {"1", "Ambient occlusion on, to read shapes"}},
                {"RenderDepthOfField", {"0", "Depth of field off"}},
                {"RenderMaxPartCount", {"1024", "Fewer particles"}},
                {"RenderVolumeLODFactor", {"2.0", "Objects keep detail at a distance"}},
                {"RenderScreenSpaceReflections", {"0", "Screen-space reflections off"}}};
    case Preset::Photography:
        return {{"RenderFarClip", {"256", "Long views"}},
                {"RenderShadowDetail", {"2", "Shadows from the sun and local lights"}},
                {"RenderAvatarMaxNonImpostors", {"12", "Subjects drawn in full"}},
                {"RenderAvatarMaxComplexity", {"0", "No complexity limit"}},
                {"RenderDeferredSSAO", {"1", "Ambient occlusion on"}},
                {"RenderDepthOfField", {"1", "Depth of field on"}},
                {"RenderMaxPartCount", {"8192", "All particles"}},
                {"RenderVolumeLODFactor", {"4.0", "Highest object detail"}},
                {"RenderMaxTextureResolution", {"2048", "Full texture detail"}},
                {"RenderScreenSpaceReflections", {"1", "Screen-space reflections on"}},
                {"RenderGlow", {"1", "Glow on"}}};
    case Preset::Event:
        return {{"RenderFarClip", {"64", "The venue only"}},
                {"RenderShadowDetail", {"0", "Shadows off"}},
                {"RenderAvatarMaxNonImpostors", {"3", "Most of the crowd drawn simply"}},
                {"RenderAvatarMaxComplexity", {"100000", "Very complex avatars are drawn simply"}},
                {"RenderDeferredSSAO", {"0", "Ambient occlusion off"}},
                {"RenderDepthOfField", {"0", "Depth of field off"}},
                {"RenderMaxPartCount", {"1024", "Fewer particles"}},
                {"RenderVolumeLODFactor", {"1.0", "Default object detail"}},
                {"RenderScreenSpaceReflections", {"0", "Screen-space reflections off"}},
                {"RenderGlow", {"1", "Glow on, for stage lights"}}};
    }
    return {};
}

ChangePlan planPreset(Preset preset, const GraphicsSettings& current)
{
    ChangePlan plan;
    plan.title = std::string(presetName(preset)) + " preset";
    plan.disclaimer = std::string(kDisclaimer);
    for (const auto& [name, v] : presetValues(preset))
    {
        if (!isManagedSetting(name))
        {
            continue; // Belt and braces: presets name only managed settings (tested).
        }
        const auto it = current.values.find(name);
        const std::string from = it == current.values.end() ? std::string() : it->second;
        // Compare as numbers where both are, so "1.0" and "1" are the same.
        const auto a = current.number(name);
        char* end = nullptr;
        const double b = std::strtod(v.value.c_str(), &end);
        if (a && end && *end == '\0' && std::fabs(*a - b) < 1e-6)
        {
            continue;
        }
        if (!a && from == v.value)
        {
            continue;
        }
        plan.changes.push_back({name, from, v.value, v.why});
    }
    return plan;
}

RestorePoint makeRestorePoint(const ChangePlan& plan, const GraphicsSettings& current, std::string label)
{
    RestorePoint point;
    point.label = std::move(label);
    for (const SettingChange& c : plan.changes)
    {
        const auto it = current.values.find(c.setting);
        if (it != current.values.end() && isManagedSetting(c.setting))
        {
            point.values[c.setting] = it->second;
        }
    }
    return point;
}

ChangePlan planRestore(const RestorePoint& point, const GraphicsSettings& current)
{
    ChangePlan plan;
    plan.title = "Restore " + point.label;
    plan.disclaimer = std::string(kDisclaimer);
    for (const auto& [name, value] : point.values)
    {
        if (!isManagedSetting(name))
        {
            continue;
        }
        const auto it = current.values.find(name);
        const std::string from = it == current.values.end() ? std::string() : it->second;
        if (from != value)
        {
            plan.changes.push_back({name, from, value, "The value before the preset"});
        }
    }
    return plan;
}

std::string writeRestorePoint(const RestorePoint& point)
{
    io::Json doc = io::Json::object();
    doc.set("format", "sw67.perf.restore");
    doc.set("version", 1);
    doc.set("label", point.label);
    io::Json values = io::Json::object();
    for (const auto& [name, value] : point.values)
    {
        values.set(name, value);
    }
    doc.set("values", values);
    return io::writeJson(doc);
}

std::optional<RestorePoint> readRestorePoint(std::string_view text)
{
    const auto doc = io::parseJson(text);
    if (!doc || (*doc)["format"].asString() != "sw67.perf.restore" || (*doc)["version"].asInt(0) != 1)
    {
        return std::nullopt;
    }
    RestorePoint point;
    point.label = (*doc)["label"].asString("previous settings").substr(0, 120);
    for (const auto& [name, value] : (*doc)["values"].members())
    {
        if (isManagedSetting(name) && value.isString() && value.asString().size() <= 32)
        {
            point.values[name] = value.asString();
        }
    }
    return point;
}

} // namespace sw67::perf
