/**
 * @file photo_looks.cpp
 * @brief Photo Studio looks. See looks.h.
 *
 * Copyright (c) 2026, 6|7 World Studio contributors.
 *
 * The source code in this file is provided to you under the terms of the
 * GNU Lesser General Public License, version 2.1, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE. Terms of the LGPL can be found in doc/LGPL-license.txt
 * in this distribution, or online at http://www.gnu.org/licenses/lgpl-2.1.txt
 */

#include "sw67/photo/looks.h"

#include "sw67/io/json.h"
#include "sw67/photo/product.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace sw67::photo
{
namespace
{
using io::Json;

std::string num(double v)
{
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.4g", v);
    return buf;
}

std::optional<double> parseNum(const std::string& s)
{
    if (s.empty()) return std::nullopt;
    char* end = nullptr;
    const double v = std::strtod(s.c_str(), &end);
    if (!end || *end != '\0' || !std::isfinite(v)) return std::nullopt;
    return v;
}

bool sameValue(const std::string& a, const std::string& b)
{
    if (a == b) return true;
    const auto x = parseNum(a), y = parseNum(b);
    return x && y && std::fabs(*x - *y) < 1e-4;
}

Look make(std::string key, std::string name, std::string description, Values settings, Sky sky)
{
    Look l;
    l.key = std::move(key);
    l.name = std::move(name);
    l.description = std::move(description);
    l.settings = std::move(settings);
    l.sky = sky;
    l.builtin = true;
    return l;
}
} // namespace

std::string_view skyKey(Sky sky)
{
    switch (sky)
    {
    case Sky::Keep: return "keep";
    case Sky::Sunrise: return "sunrise";
    case Sky::Midday: return "midday";
    case Sky::Sunset: return "sunset";
    case Sky::Midnight: return "midnight";
    }
    return "keep";
}

std::optional<Sky> parseSky(std::string_view key)
{
    for (Sky s : {Sky::Keep, Sky::Sunrise, Sky::Midday, Sky::Sunset, Sky::Midnight})
        if (skyKey(s) == key) return s;
    return std::nullopt;
}

const std::map<std::string, SettingRange>& lookSettings()
{
    static const std::map<std::string, SettingRange> settings = {
        {"RenderColorGrade", {0, 1, true}},
        {"RenderColorGradeWhiteBalanceCCT", {-5000, 5000, false}},
        {"RenderColorGradeBrightness", {-0.5, 0.5, false}},
        {"RenderColorGradeContrast", {0, 2, false}},
        {"RenderColorGradeSaturation", {0, 2, false}},
        {"RenderColorGradeVibrance", {-1, 1, false}},
        {"RenderColorGradeHighlights", {-1, 1, false}},
        {"RenderColorGradeShadows", {-1, 1, false}},
        {"RenderVignetteAmount", {0, 1, false}},
        {"RenderFilmGrainAmount", {0, 1, false}},
        {"RenderBloomStrength", {0, 2, false}},
        {"RenderExposure", {0.25, 4, false}},
    };
    return settings;
}

std::vector<std::string> sanitise(Look& look)
{
    std::vector<std::string> notes;
    const auto& ranges = lookSettings();
    for (auto it = look.settings.begin(); it != look.settings.end();)
    {
        const auto r = ranges.find(it->first);
        const auto v = parseNum(it->second);
        if (r == ranges.end() || !v)
        {
            notes.push_back("Left out " + it->first + ": a look can only set colour, lens and exposure settings.");
            it = look.settings.erase(it);
            continue;
        }
        double x = *v;
        if (r->second.boolean) x = x != 0.0 ? 1.0 : 0.0;
        const double clamped = std::clamp(x, r->second.min, r->second.max);
        if (clamped != x) notes.push_back(it->first + " was outside its range and was clamped.");
        it->second = num(clamped);
        ++it;
    }
    return notes;
}

std::vector<Look> builtinLooks()
{
    return {
        make("product_neutral", "Product, neutral",
             "True colours for store listings: no vignette, grain or tint, gentle bloom.",
             {{"RenderColorGrade", "1"}, {"RenderColorGradeWhiteBalanceCCT", "0"},
              {"RenderColorGradeContrast", "1.05"}, {"RenderColorGradeSaturation", "1"},
              {"RenderColorGradeVibrance", "0"}, {"RenderVignetteAmount", "0"}, {"RenderFilmGrainAmount", "0"},
              {"RenderBloomStrength", "0.15"}},
             Sky::Midday),
        make("product_bright", "Product, bright studio",
             "A brighter, slightly lifted look for clothing and decor on light backdrops.",
             {{"RenderColorGrade", "1"}, {"RenderColorGradeBrightness", "0.06"}, {"RenderColorGradeShadows", "0.15"},
              {"RenderColorGradeContrast", "0.95"}, {"RenderVignetteAmount", "0"}, {"RenderFilmGrainAmount", "0"}},
             Sky::Midday),
        make("social_warm", "Social, golden hour",
             "Warm and vivid for social posts, with a light vignette.",
             {{"RenderColorGrade", "1"}, {"RenderColorGradeWhiteBalanceCCT", "-1000"},
              {"RenderColorGradeVibrance", "0.2"}, {"RenderColorGradeContrast", "1.1"},
              {"RenderVignetteAmount", "0.25"}},
             Sky::Sunset),
        make("social_cool", "Social, cool night",
             "Cool, high-contrast night look for club and event photos.",
             {{"RenderColorGrade", "1"}, {"RenderColorGradeWhiteBalanceCCT", "1500"},
              {"RenderColorGradeContrast", "1.2"}, {"RenderColorGradeSaturation", "0.9"},
              {"RenderVignetteAmount", "0.35"}, {"RenderBloomStrength", "0.5"}},
             Sky::Midnight),
        make("portrait_soft", "Portrait, soft",
             "Soft contrast and a little warmth for avatar portraits.",
             {{"RenderColorGrade", "1"}, {"RenderColorGradeWhiteBalanceCCT", "-400"},
              {"RenderColorGradeContrast", "0.92"}, {"RenderColorGradeHighlights", "-0.1"},
              {"RenderVignetteAmount", "0.15"}, {"RenderFilmGrainAmount", "0.05"}},
             Sky::Sunrise),
    };
}

Look captureLook(std::string name, const Values& current, Sky sky)
{
    Look l;
    l.name = name.substr(0, 80);
    l.key = "custom_" + safeFileStem(l.name.empty() ? std::string("look") : l.name);
    for (const auto& [setting, range] : lookSettings())
    {
        const auto it = current.find(setting);
        if (it != current.end() && !it->second.empty()) l.settings[setting] = it->second;
    }
    l.sky = sky;
    sanitise(l);
    return l;
}

perf::ChangePlan planLook(const Look& look, const Values& current)
{
    perf::ChangePlan plan;
    plan.title = "Photo look: " + look.name;
    plan.disclaimer = "Only your own screen and snapshots change. Other residents see the region's sky and none of "
                      "these effects. Turning the look off puts these settings back.";
    for (const auto& [setting, to] : look.settings)
    {
        const auto it = current.find(setting);
        const std::string from = it == current.end() ? std::string() : it->second;
        if (sameValue(from, to)) continue;
        plan.changes.push_back({setting, from, to, look.name});
    }
    if (look.sky != Sky::Keep)
    {
        plan.changes.push_back({"Local sky", "", std::string(skyKey(look.sky)), "fixed local sky for the shoot"});
    }
    return plan;
}

LookState stateFor(const Look& look, const Values& current)
{
    LookState s;
    s.key = look.key;
    s.name = look.name;
    s.sky = look.sky;
    for (const auto& [setting, to] : look.settings)
    {
        const auto it = current.find(setting);
        if (it == current.end()) continue;
        s.before[setting] = it->second;
        s.set[setting] = to;
    }
    return s;
}

RevertPlan planRevert(const LookState& state, const Values& current)
{
    RevertPlan r;
    r.plan.title = "Turn off the look \"" + state.name + "\"";
    r.plan.disclaimer = "Puts back the settings the look changed.";
    for (const auto& [setting, set] : state.set)
    {
        const auto before = state.before.find(setting);
        const auto now = current.find(setting);
        if (before == state.before.end() || now == current.end()) continue;
        if (!sameValue(now->second, set))
        {
            r.kept.push_back(setting);
            continue;
        }
        if (!sameValue(now->second, before->second))
        {
            r.plan.changes.push_back({setting, now->second, before->second, "as it was before the look"});
        }
    }
    r.restoreSky = state.sky != Sky::Keep;
    return r;
}

namespace
{
Json valuesJson(const Values& v)
{
    Json o = Json::object();
    for (const auto& [k, val] : v) o.set(k, Json(val));
    return o;
}

Values valuesFrom(const Json& j)
{
    Values v;
    const auto& ranges = lookSettings();
    for (const auto& [k, val] : j.members())
    {
        if (ranges.count(k) && val.isString()) v[k] = val.asString().substr(0, 32);
    }
    return v;
}
} // namespace

std::string writeLooks(const std::vector<Look>& custom, const std::optional<LookState>& active)
{
    Json root = Json::object();
    root.set("format", Json(kLooksFormat));
    root.set("version", Json(kLooksFormatVersion));
    Json looks = Json::array();
    for (const Look& l : custom)
    {
        if (l.builtin) continue;
        Json e = Json::object();
        e.set("key", Json(l.key));
        e.set("name", Json(l.name));
        e.set("description", Json(l.description));
        e.set("sky", Json(skyKey(l.sky)));
        e.set("settings", valuesJson(l.settings));
        looks.push(std::move(e));
    }
    root.set("looks", std::move(looks));
    if (active)
    {
        Json a = Json::object();
        a.set("key", Json(active->key));
        a.set("name", Json(active->name));
        a.set("sky", Json(skyKey(active->sky)));
        a.set("before", valuesJson(active->before));
        a.set("set", valuesJson(active->set));
        root.set("active", std::move(a));
    }
    return io::writeJson(root);
}

LooksFile readLooks(std::string_view text)
{
    LooksFile f;
    io::JsonError err;
    const auto root = io::parseJson(text, &err);
    if (!root || !root->isObject() || (*root)["format"].asString("") != kLooksFormat)
    {
        f.notes.push_back("This is not a 6|7 photo looks file.");
        return f;
    }
    const int version = (*root)["version"].asInt(0);
    if (version < 1 || version > kLooksFormatVersion)
    {
        f.notes.push_back("This looks file was saved by a newer 6|7 World Studio.");
        return f;
    }
    for (const Json& e : (*root)["looks"].items())
    {
        if (f.custom.size() >= kMaxCustomLooks) break;
        Look l;
        l.name = e["name"].asString("").substr(0, 80);
        l.key = "custom_" + safeFileStem(l.name.empty() ? std::string("look") : l.name);
        l.description = e["description"].asString("").substr(0, 400);
        l.sky = parseSky(e["sky"].asString("keep")).value_or(Sky::Keep);
        for (const auto& [k, val] : e["settings"].members())
        {
            if (val.isString()) l.settings[k] = val.asString().substr(0, 32);
        }
        for (const std::string& n : sanitise(l)) f.notes.push_back("\"" + l.name + "\": " + n);
        f.custom.push_back(std::move(l));
    }
    const Json& a = (*root)["active"];
    if (a.isObject())
    {
        LookState s;
        s.key = a["key"].asString("").substr(0, 120);
        s.name = a["name"].asString("").substr(0, 80);
        s.sky = parseSky(a["sky"].asString("keep")).value_or(Sky::Keep);
        s.before = valuesFrom(a["before"]);
        s.set = valuesFrom(a["set"]);
        f.active = std::move(s);
    }
    return f;
}

} // namespace sw67::photo
