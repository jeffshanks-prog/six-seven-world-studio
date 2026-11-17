/**
 * @file privacy_recording.cpp
 * @brief Recording Privacy Mode.
 *
 * Copyright (c) 2026, 6|7 World Studio contributors.
 *
 * The source code in this file is provided to you under the terms of the
 * GNU Lesser General Public License, version 2.1, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE. Terms of the LGPL can be found in doc/LGPL-license.txt
 * in this distribution, or online at http://www.gnu.org/licenses/lgpl-2.1.txt
 */

#include "sw67/privacy/recording.h"

#include "sw67/io/json.h"

#include <algorithm>
#include <set>

namespace sw67::privacy
{
namespace
{
using io::Json;

struct HideInfo
{
    Hide hide;
    std::string_view key;
    std::string_view label;
};

constexpr HideInfo kHides[] = {
    {Hide::MyName, "my_name", "Your own name tag"},
    {Hide::OtherNames, "other_names", "Other people's name tags"},
    {Hide::GroupTitles, "group_titles", "Group titles"},
    {Hide::ChatPopups, "chat_popups", "Nearby chat pop-ups"},
    {Hide::IMPopups, "im_popups", "IM, group and conference pop-ups"},
    {Hide::ChatBubbles, "chat_bubbles", "Chat bubbles"},
    {Hide::Location, "location", "Your location in the navigation bar"},
    {Hide::Balance, "balance", "Your L$ balance (status bar and snapshots)"},
    {Hide::HoverTips, "hover_tips", "Hover tips with object and land names"},
};

// The viewer's settings, by option. "noaction" is the IM preference that
// shows no toast, flashes nothing and opens nothing; messages still arrive
// in the conversation windows.
const std::vector<Managed>& table()
{
    static const std::vector<Managed> t = {
        {"RenderNameShowSelf", "0", Hide::MyName},
        {"AvatarNameTagMode", "0", Hide::OtherNames},
        {"NameTagShowGroupTitles", "0", Hide::GroupTitles},
        {"NotificationNearbyChatOptions", "noaction", Hide::ChatPopups},
        {"NotificationFriendIMOptions", "noaction", Hide::IMPopups},
        {"NotificationNonFriendIMOptions", "noaction", Hide::IMPopups},
        {"NotificationConferenceIMOptions", "noaction", Hide::IMPopups},
        {"NotificationGroupChatOptions", "noaction", Hide::IMPopups},
        {"NotificationObjectIMOptions", "noaction", Hide::IMPopups},
        {"UseChatBubbles", "0", Hide::ChatBubbles},
        {"ShowNavbarNavigationPanel", "0", Hide::Location},
        {"ShowMiniLocationPanel", "0", Hide::Location},
        {"ObscureBalanceInStatusBar", "1", Hide::Balance},
        {"RenderBalanceInSnapshot", "0", Hide::Balance},
        {"ShowHoverTips", "0", Hide::HoverTips},
    };
    return t;
}

Json valuesJson(const Values& v)
{
    Json o = Json::object();
    for (const auto& [k, val] : v) o.set(k, Json(val));
    return o;
}

Values valuesFrom(const Json& j)
{
    Values v;
    for (const auto& [k, val] : j.members())
    {
        if (isManagedSetting(k) && val.isString() && val.asString().size() <= 64)
        {
            v[k] = val.asString();
        }
    }
    return v;
}
} // namespace

std::string_view hideKey(Hide h)
{
    for (const HideInfo& i : kHides)
        if (i.hide == h) return i.key;
    return "";
}

std::string_view hideLabel(Hide h)
{
    for (const HideInfo& i : kHides)
        if (i.hide == h) return i.label;
    return "";
}

std::optional<Hide> parseHide(std::string_view key)
{
    for (const HideInfo& i : kHides)
        if (i.key == key) return i.hide;
    return std::nullopt;
}

bool Options::hides(Hide h) const
{
    const auto it = hide.find(h);
    return it == hide.end() || it->second;
}

std::vector<Managed> settingsFor(Hide h)
{
    std::vector<Managed> out;
    for (const Managed& m : table())
        if (m.because == h) out.push_back(m);
    return out;
}

const std::vector<std::string>& managedSettings()
{
    static const std::vector<std::string> names = [] {
        std::vector<std::string> n;
        for (const Managed& m : table()) n.push_back(m.setting);
        return n;
    }();
    return names;
}

bool isManagedSetting(std::string_view name)
{
    const auto& n = managedSettings();
    return std::find(n.begin(), n.end(), name) != n.end();
}

perf::ChangePlan planEnable(const Options& options, const Values& current)
{
    perf::ChangePlan plan;
    plan.title = "Recording privacy mode";
    plan.disclaimer = "Only this screen changes: nothing is sent and nobody else's view changes. Messages still "
                      "arrive in your conversation windows. Turning privacy mode off puts these back.";
    for (const Managed& m : table())
    {
        if (!options.hides(m.because)) continue;
        const auto it = current.find(m.setting);
        if (it == current.end())
        {
            continue; // This viewer lacks the setting: nothing to change.
        }
        if (it->second == m.value) continue;
        plan.changes.push_back({m.setting, it->second, m.value, std::string("hides ") + std::string(hideLabel(m.because))});
    }
    return plan;
}

State stateFor(const Options& options, const perf::ChangePlan& plan)
{
    State s;
    s.active = true;
    s.options = options;
    for (const perf::SettingChange& c : plan.changes)
    {
        if (!isManagedSetting(c.setting)) continue;
        s.before[c.setting] = c.from;
        s.set[c.setting] = c.to;
    }
    return s;
}

DisablePlan planDisable(const State& state, const Values& current)
{
    DisablePlan d;
    d.plan.title = "Turn off recording privacy mode";
    d.plan.disclaimer = "Puts back the display settings privacy mode changed.";
    for (const auto& [name, before] : state.before)
    {
        if (!isManagedSetting(name)) continue;
        const auto now = current.find(name);
        const auto set = state.set.find(name);
        if (now == current.end()) continue;
        if (set != state.set.end() && now->second != set->second)
        {
            d.kept.push_back(name); // The user changed it themselves while privacy mode was on.
            continue;
        }
        if (now->second == before) continue;
        d.plan.changes.push_back({name, now->second, before, "as it was before privacy mode"});
    }
    return d;
}

std::string writeState(const State& s)
{
    Json root = Json::object();
    root.set("format", Json(kFormat));
    root.set("version", Json(kFormatVersion));
    root.set("active", Json(s.active));
    Json opts = Json::object();
    for (const HideInfo& i : kHides) opts.set(std::string(i.key), Json(s.options.hides(i.hide)));
    root.set("hide", std::move(opts));
    root.set("before", valuesJson(s.before));
    root.set("set", valuesJson(s.set));
    return io::writeJson(root);
}

std::optional<State> readState(std::string_view text)
{
    io::JsonLimits limits;
    limits.maxBytes = 64 * 1024;
    const auto root = io::parseJson(text, nullptr, limits);
    if (!root || (*root)["format"].asString("") != kFormat)
    {
        return std::nullopt;
    }
    const int version = (*root)["version"].asInt(0);
    if (version < 1 || version > kFormatVersion)
    {
        return std::nullopt;
    }
    State s;
    s.active = (*root)["active"].asBool(false);
    for (const auto& [k, v] : (*root)["hide"].members())
    {
        if (const auto h = parseHide(k)) s.options.hide[*h] = v.asBool(true);
    }
    s.before = valuesFrom((*root)["before"]);
    s.set = valuesFrom((*root)["set"]);
    return s;
}

std::string statusLine(bool active, std::size_t hiddenCount)
{
    if (!active)
    {
        return "Recording privacy mode is off.";
    }
    return "Recording privacy mode is on: " + std::to_string(hiddenCount) +
           (hiddenCount == 1 ? " thing is" : " things are") + " hidden on this screen.";
}

} // namespace sw67::privacy
