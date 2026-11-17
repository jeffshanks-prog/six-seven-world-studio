/**
 * @file workspace_profile.cpp
 * @brief Workspace profiles: built-ins, sanitising, export, import, apply plan.
 *
 * Copyright (c) 2026, 6|7 World Studio contributors.
 *
 * The source code in this file is provided to you under the terms of the
 * GNU Lesser General Public License, version 2.1, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE. Terms of the LGPL can be found in doc/LGPL-license.txt
 * in this distribution, or online at http://www.gnu.org/licenses/lgpl-2.1.txt
 */

#include "sw67/workspace/profile.h"

#include "sw67/common/settings_policy.h"
#include "sw67/io/json.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <set>

namespace sw67::workspace
{
namespace
{
using io::Json;

constexpr std::size_t kMaxXmlBytes = 256u * 1024u;
constexpr std::size_t kMaxWindows = 200;
constexpr std::size_t kMaxPreferences = 500;
constexpr std::size_t kMaxNameBytes = 80;
constexpr std::size_t kMaxDescriptionBytes = 2000;

// Windows a profile never closes: the one applying it.
const std::set<std::string, std::less<>> kNeverClose{"sw67_workspaces"};

bool validWindowName(std::string_view name)
{
    if (name.empty() || name.size() > 64)
    {
        return false;
    }
    return std::all_of(name.begin(), name.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
    });
}

bool validRect(const Rect& r)
{
    return r.width >= 32 && r.width <= 8192 && r.height >= 32 && r.height <= 8192 &&
           r.left >= -8192 && r.left <= 16384 && r.top >= -8192 && r.top <= 16384;
}

std::string lower(std::string_view s)
{
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

/// Toolbar and shortcut files are the viewer's own XML. Refuse anything that
/// is not plainly XML or that declares a DTD or entities, which the viewer's
/// parser has no need of.
std::optional<std::string> xmlProblem(std::string_view xml)
{
    if (xml.size() > kMaxXmlBytes)
    {
        return std::string("is larger than 256 KB");
    }
    const auto first = xml.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos || xml[first] != '<')
    {
        return std::string("is not XML");
    }
    const std::string l = lower(xml);
    if (l.find("<!doctype") != std::string::npos || l.find("<!entity") != std::string::npos)
    {
        return std::string("declares a DTD or entities");
    }
    return std::nullopt;
}

bool validPrefValue(const PrefValue& v)
{
    switch (v.type)
    {
    case PrefValue::Type::Bool:
        return v.value == "0" || v.value == "1";
    case PrefValue::Type::Int:
    {
        if (v.value.empty() || v.value.size() > 12) return false;
        char* end = nullptr;
        (void)std::strtol(v.value.c_str(), &end, 10);
        return end && *end == '\0';
    }
    case PrefValue::Type::Float:
    {
        if (v.value.empty() || v.value.size() > 32) return false;
        char* end = nullptr;
        (void)std::strtod(v.value.c_str(), &end);
        return end && *end == '\0';
    }
    case PrefValue::Type::String:
        return v.value.size() <= 4096;
    }
    return false;
}

std::string_view typeKey(PrefValue::Type t)
{
    switch (t)
    {
    case PrefValue::Type::Bool: return "bool";
    case PrefValue::Type::Int: return "int";
    case PrefValue::Type::Float: return "float";
    case PrefValue::Type::String: return "string";
    }
    return "string";
}

std::optional<PrefValue::Type> parseType(std::string_view key)
{
    if (key == "bool") return PrefValue::Type::Bool;
    if (key == "int") return PrefValue::Type::Int;
    if (key == "float") return PrefValue::Type::Float;
    if (key == "string") return PrefValue::Type::String;
    return std::nullopt;
}

Note warn(std::string text)
{
    return {Note::Level::Warning, std::move(text)};
}

WindowLayout win(std::string name)
{
    WindowLayout w;
    w.name = std::move(name);
    return w;
}

std::string rectText(const std::optional<Rect>& r)
{
    if (!r)
    {
        return "its last position";
    }
    return std::to_string(r->width) + "x" + std::to_string(r->height) + " at " + std::to_string(r->left) + "," +
           std::to_string(r->top);
}
} // namespace

std::string_view kindKey(ProfileKind kind)
{
    switch (kind)
    {
    case ProfileKind::Everyday: return "everyday";
    case ProfileKind::Creator: return "creator";
    case ProfileKind::Photographer: return "photographer";
    case ProfileKind::DJ: return "dj";
    case ProfileKind::Host: return "host";
    case ProfileKind::Custom: return "custom";
    }
    return "custom";
}

std::string_view kindName(ProfileKind kind)
{
    switch (kind)
    {
    case ProfileKind::Everyday: return "Everyday";
    case ProfileKind::Creator: return "Creator";
    case ProfileKind::Photographer: return "Photographer";
    case ProfileKind::DJ: return "DJ";
    case ProfileKind::Host: return "Host";
    case ProfileKind::Custom: return "Custom";
    }
    return "Custom";
}

std::optional<ProfileKind> parseKind(std::string_view key)
{
    for (ProfileKind k : {ProfileKind::Everyday, ProfileKind::Creator, ProfileKind::Photographer, ProfileKind::DJ,
                          ProfileKind::Host, ProfileKind::Custom})
    {
        if (kindKey(k) == key)
        {
            return k;
        }
    }
    return std::nullopt;
}

WorkspaceProfile builtinProfile(ProfileKind kind)
{
    WorkspaceProfile p;
    p.kind = kind;
    p.name = std::string(kindName(kind));
    switch (kind)
    {
    case ProfileKind::Everyday:
        p.description = "Chat, people and inventory to hand.";
        p.windows = {win("people"), win("inventory"), win("nearby_chat")};
        break;
    case ProfileKind::Creator:
        p.description = "Build tools, inventory, Script Studio and the scene explorer.";
        p.windows = {win("build"), win("inventory"), win("script_studio"), win("scene_explorer")};
        break;
    case ProfileKind::Photographer:
        p.description = "Snapshot, camera, Lightbox and environments.";
        p.windows = {win("snapshot"), win("camera"), win("lightbox"), win("my_environments")};
        break;
    case ProfileKind::DJ:
        p.description = "Music ticker, chat and conversations. Stream controls arrive with the DJ Workspace (planned).";
        p.windows = {win("music_ticker"), win("nearby_chat"), win("im_container")};
        break;
    case ProfileKind::Host:
        p.description = "Chat, conversations, people, land and events.";
        p.windows = {win("nearby_chat"), win("im_container"), win("people"), win("about_land")};
        break;
    case ProfileKind::Custom:
        p.description = "Your own layout.";
        break;
    }
    return p;
}

std::vector<WorkspaceProfile> builtinProfiles()
{
    return {builtinProfile(ProfileKind::Everyday), builtinProfile(ProfileKind::Creator),
            builtinProfile(ProfileKind::Photographer), builtinProfile(ProfileKind::DJ),
            builtinProfile(ProfileKind::Host)};
}

std::vector<Note> sanitize(WorkspaceProfile& p)
{
    std::vector<Note> notes;
    if (p.name.size() > kMaxNameBytes)
    {
        p.name.resize(kMaxNameBytes);
        notes.push_back(warn("The profile name was shortened to 80 characters."));
    }
    if (p.name.empty())
    {
        p.name = "Untitled";
        notes.push_back(warn("The profile had no name; it is called \"Untitled\"."));
    }
    if (p.description.size() > kMaxDescriptionBytes)
    {
        p.description.resize(kMaxDescriptionBytes);
        notes.push_back(warn("The description was shortened."));
    }

    std::vector<WindowLayout> windows;
    std::set<std::string, std::less<>> seen;
    for (WindowLayout& w : p.windows)
    {
        if (!validWindowName(w.name))
        {
            notes.push_back(warn("Left out a window with an invalid name."));
            continue;
        }
        if (!seen.insert(w.name).second)
        {
            notes.push_back(warn("Left out a second entry for window \"" + w.name + "\"."));
            continue;
        }
        if (w.rect && !validRect(*w.rect))
        {
            w.rect.reset();
            notes.push_back(warn("Window \"" + w.name + "\" had an out-of-range position; it opens where it last was."));
        }
        if (windows.size() >= kMaxWindows)
        {
            notes.push_back(warn("Left out windows past the first 200."));
            break;
        }
        windows.push_back(std::move(w));
    }
    p.windows = std::move(windows);

    if (!p.toolbarsXml.empty())
    {
        if (auto problem = xmlProblem(p.toolbarsXml))
        {
            p.toolbarsXml.clear();
            notes.push_back(warn("Left out the toolbar layout: it " + *problem + "."));
        }
    }
    if (!p.keyBindingsXml.empty())
    {
        if (auto problem = xmlProblem(p.keyBindingsXml))
        {
            p.keyBindingsXml.clear();
            notes.push_back(warn("Left out the keyboard shortcuts: they " + *problem + "."));
        }
    }

    for (auto it = p.preferences.begin(); it != p.preferences.end();)
    {
        const common::SettingClass c = common::classifySetting(it->first);
        if (c != common::SettingClass::Portable)
        {
            const std::string shown = c == common::SettingClass::Invalid ? std::string("a setting") : "\"" + it->first + "\"";
            notes.push_back(warn("Left out " + shown + ": it " + std::string(common::settingClassReason(c)) + "."));
            it = p.preferences.erase(it);
            continue;
        }
        if (!validPrefValue(it->second))
        {
            notes.push_back(warn("Left out \"" + it->first + "\": its value does not match its type."));
            it = p.preferences.erase(it);
            continue;
        }
        ++it;
    }
    if (p.preferences.size() > kMaxPreferences)
    {
        auto cut = p.preferences.begin();
        std::advance(cut, static_cast<std::ptrdiff_t>(kMaxPreferences));
        p.preferences.erase(cut, p.preferences.end());
        notes.push_back(warn("Left out preferences past the first 500."));
    }
    return notes;
}

std::string exportProfile(const WorkspaceProfile& profile, std::vector<Note>* notes)
{
    WorkspaceProfile p = profile;
    std::vector<Note> n = sanitize(p);
    if (notes)
    {
        *notes = std::move(n);
    }

    Json doc = Json::object();
    doc.set("format", std::string(kFormat));
    doc.set("version", kFormatVersion);
    doc.set("name", p.name);
    doc.set("kind", std::string(kindKey(p.kind)));
    doc.set("description", p.description);
    doc.set("closeOthers", p.closeOthers);
    Json windows = Json::array();
    for (const WindowLayout& w : p.windows)
    {
        Json o = Json::object();
        o.set("name", w.name);
        o.set("open", w.open);
        if (w.rect)
        {
            Json r = Json::object();
            r.set("left", w.rect->left);
            r.set("top", w.rect->top);
            r.set("width", w.rect->width);
            r.set("height", w.rect->height);
            o.set("rect", r);
        }
        windows.push(o);
    }
    doc.set("windows", windows);
    if (!p.toolbarsXml.empty()) doc.set("toolbarsXml", p.toolbarsXml);
    if (!p.keyBindingsXml.empty()) doc.set("keyBindingsXml", p.keyBindingsXml);
    Json prefs = Json::object();
    for (const auto& [name, v] : p.preferences)
    {
        Json o = Json::object();
        o.set("type", std::string(typeKey(v.type)));
        o.set("value", v.value);
        prefs.set(name, o);
    }
    doc.set("preferences", prefs);
    doc.set("note", "Accessibility settings, passwords, keys and stream credentials are never stored in a workspace profile.");
    return io::writeJson(doc);
}

ImportResult importProfile(std::string_view text)
{
    ImportResult result;
    io::JsonError err;
    const auto doc = io::parseJson(text, &err);
    if (!doc || !doc->isObject())
    {
        result.notes.push_back({Note::Level::Error, doc ? "This is not a workspace profile." :
                                                          "This file is not valid JSON (" + err.what + ")."});
        return result;
    }
    if ((*doc)["format"].asString() != kFormat)
    {
        result.notes.push_back({Note::Level::Error, "This is not a 6|7 World Studio workspace profile."});
        return result;
    }
    const int version = (*doc)["version"].asInt(0);
    if (version < 1)
    {
        result.notes.push_back({Note::Level::Error, "The profile has no valid format version."});
        return result;
    }
    if (version > kFormatVersion)
    {
        result.notes.push_back({Note::Level::Error, "This profile was saved by a newer version of 6|7 World Studio (format " +
                                                        std::to_string(version) + "). Update the viewer to import it."});
        return result;
    }

    static const std::set<std::string, std::less<>> kKnown{"format", "version", "name", "kind", "description",
                                                           "closeOthers", "windows", "toolbarsXml", "keyBindingsXml",
                                                           "preferences", "note"};
    for (const auto& [key, value] : doc->members())
    {
        (void)value;
        if (!kKnown.count(key))
        {
            result.notes.push_back({Note::Level::Info, "Ignored an unknown field \"" + key.substr(0, 40) + "\"."});
        }
    }

    WorkspaceProfile p;
    p.name = (*doc)["name"].asString("");
    p.kind = parseKind((*doc)["kind"].asString("custom")).value_or(ProfileKind::Custom);
    p.description = (*doc)["description"].asString("");
    p.closeOthers = (*doc)["closeOthers"].asBool(false);
    for (const Json& w : (*doc)["windows"].items())
    {
        WindowLayout l;
        l.name = w["name"].asString("");
        l.open = w["open"].asBool(true);
        if (w["rect"].isObject())
        {
            const Json& r = w["rect"];
            l.rect = Rect{r["left"].asInt(0), r["top"].asInt(0), r["width"].asInt(0), r["height"].asInt(0)};
        }
        p.windows.push_back(std::move(l));
    }
    p.toolbarsXml = (*doc)["toolbarsXml"].asString("");
    p.keyBindingsXml = (*doc)["keyBindingsXml"].asString("");
    for (const auto& [name, v] : (*doc)["preferences"].members())
    {
        const auto type = parseType(v["type"].asString(""));
        if (!type || !v["value"].isString())
        {
            result.notes.push_back(warn("Left out \"" + name.substr(0, 64) + "\": it has no valid type or value."));
            continue;
        }
        p.preferences[name] = PrefValue{*type, v["value"].asString()};
    }

    auto notes = sanitize(p);
    result.notes.insert(result.notes.end(), notes.begin(), notes.end());
    result.profile = std::move(p);
    return result;
}

std::string Change::describe() const
{
    switch (what)
    {
    case What::OpenWindow: return "Open " + target + (to.empty() ? "" : " (" + to + ")");
    case What::MoveWindow: return "Move " + target + " from " + from + " to " + to;
    case What::CloseWindow: return "Close " + target;
    case What::SetPreference: return "Set " + target + " from " + from + " to " + to;
    case What::ReplaceToolbars: return "Replace the toolbar layout";
    case What::ReplaceShortcuts: return "Replace the keyboard shortcuts";
    case What::SkipUnknownWindow: return "Skip " + target + ": this viewer has no such window";
    case What::SkipPreference: return "Skip " + target + ": " + to;
    }
    return target;
}

std::vector<Change> planApply(const WorkspaceProfile& profileIn, const CurrentState& current)
{
    WorkspaceProfile profile = profileIn;
    sanitize(profile);

    std::vector<Change> plan;
    std::set<std::string, std::less<>> listed;
    for (const WindowLayout& w : profile.windows)
    {
        listed.insert(w.name);
        const auto it = current.windows.find(w.name);
        if (it == current.windows.end())
        {
            plan.push_back({Change::What::SkipUnknownWindow, w.name, "", ""});
            continue;
        }
        const WindowLayout& now = it->second;
        if (w.open && !now.open)
        {
            plan.push_back({Change::What::OpenWindow, w.name, "", w.rect ? rectText(w.rect) : ""});
        }
        else if (w.open && w.rect && w.rect != now.rect)
        {
            plan.push_back({Change::What::MoveWindow, w.name, rectText(now.rect), rectText(w.rect)});
        }
        else if (!w.open && now.open && !kNeverClose.count(w.name))
        {
            plan.push_back({Change::What::CloseWindow, w.name, "", ""});
        }
    }
    if (profile.closeOthers)
    {
        for (const auto& [name, now] : current.windows)
        {
            if (now.open && !listed.count(name) && !kNeverClose.count(name))
            {
                plan.push_back({Change::What::CloseWindow, name, "", ""});
            }
        }
    }
    if (!profile.toolbarsXml.empty() && profile.toolbarsXml != current.toolbarsXml)
    {
        plan.push_back({Change::What::ReplaceToolbars, "toolbars", "", ""});
    }
    if (!profile.keyBindingsXml.empty() && profile.keyBindingsXml != current.keyBindingsXml)
    {
        plan.push_back({Change::What::ReplaceShortcuts, "shortcuts", "", ""});
    }
    for (const auto& [name, v] : profile.preferences)
    {
        const auto it = current.preferences.find(name);
        if (it == current.preferences.end())
        {
            plan.push_back({Change::What::SkipPreference, name, "", "this viewer has no such setting"});
            continue;
        }
        if (it->second.type != v.type)
        {
            plan.push_back({Change::What::SkipPreference, name, "", "its type differs in this viewer"});
            continue;
        }
        if (it->second.value != v.value)
        {
            plan.push_back({Change::What::SetPreference, name, it->second.value, v.value});
        }
    }
    return plan;
}

WorkspaceProfile restorePoint(const WorkspaceProfile& applied, const CurrentState& current)
{
    WorkspaceProfile undo;
    undo.name = "Before " + applied.name;
    undo.kind = ProfileKind::Custom;
    undo.description = "The layout as it was before \"" + applied.name + "\" was applied.";
    for (const Change& c : planApply(applied, current))
    {
        switch (c.what)
        {
        case Change::What::OpenWindow:
        case Change::What::MoveWindow:
        case Change::What::CloseWindow:
            undo.windows.push_back(current.windows.at(c.target));
            break;
        case Change::What::SetPreference:
            undo.preferences[c.target] = current.preferences.at(c.target);
            break;
        case Change::What::ReplaceToolbars:
            undo.toolbarsXml = current.toolbarsXml;
            break;
        case Change::What::ReplaceShortcuts:
            undo.keyBindingsXml = current.keyBindingsXml;
            break;
        case Change::What::SkipUnknownWindow:
        case Change::What::SkipPreference:
            break;
        }
    }
    return undo;
}

} // namespace sw67::workspace
