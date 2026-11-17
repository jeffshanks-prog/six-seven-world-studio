/**
 * @file backup.cpp
 * @brief Settings Backup and Restore.
 *
 * Copyright (c) 2026, 6|7 World Studio contributors.
 *
 * The source code in this file is provided to you under the terms of the
 * GNU Lesser General Public License, version 2.1, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE. Terms of the LGPL can be found in doc/LGPL-license.txt
 * in this distribution, or online at http://www.gnu.org/licenses/lgpl-2.1.txt
 */

#include "sw67/backup/backup.h"

#include "sw67/creator/project.h"
#include "sw67/io/json.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>

namespace sw67::backup
{
namespace
{
using common::SettingClass;
using io::Json;

std::string lower(std::string_view s)
{
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

// Settings that describe this computer rather than the user's choices:
// folders on this disk, the last version run, the window's own size.
constexpr std::array<std::string_view, 14> kMachinePrefixes = {
    "cachelocation",   "newcachelocation", "texturesavelocation", "snapshotlocalpath", "lastrunversion",
    "lastfeaturever",  "lastgpu",          "windowwidth",         "windowheight",      "windowx",
    "windowy",         "windowmaximized",  "fullscreen",          "firstrun"};

bool isMachineSetting(std::string_view name)
{
    const std::string l = lower(name);
    return std::any_of(kMachinePrefixes.begin(), kMachinePrefixes.end(),
                       [&](std::string_view p) { return l.rfind(p, 0) == 0; });
}

// Why a setting may not enter or leave a backup, or nullopt when it may.
std::optional<std::string> policyBlock(const SettingEntry& s, bool accessibilityAllowed)
{
    const SettingClass c = common::classifySetting(s.name);
    switch (c)
    {
    case SettingClass::Portable: break;
    case SettingClass::Accessibility:
        if (accessibilityAllowed) break;
        return std::string("is an accessibility setting, and you chose to leave those out");
    case SettingClass::Secret:
    case SettingClass::Consent:
    case SettingClass::Invalid: return std::string(common::settingClassReason(c));
    }
    if (isMachineSetting(s.name))
    {
        return std::string("belongs to this computer (a folder, version or window size), not to you");
    }
    if (!valueParses(s.value))
    {
        return std::string("has a value that is not valid for its type");
    }
    if (s.value.type == PrefValue::Type::String && valueLooksSecret(s.value.value))
    {
        return std::string("has a value that looks like a password or key, so it is never written to a file");
    }
    return std::nullopt;
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

std::string shown(const PrefValue& v)
{
    if (v.type == PrefValue::Type::Bool) return v.value == "1" ? "on" : "off";
    if (v.type == PrefValue::Type::String) return "\"" + (v.value.size() > 60 ? v.value.substr(0, 57) + "..." : v.value) + "\"";
    return v.value;
}

bool sameValue(const PrefValue& a, const PrefValue& b)
{
    if (a.type != b.type) return false;
    if (a.type == PrefValue::Type::Float)
    {
        const double x = std::strtod(a.value.c_str(), nullptr);
        const double y = std::strtod(b.value.c_str(), nullptr);
        const double scale = std::max({1.0, x < 0 ? -x : x, y < 0 ? -y : y});
        return (x > y ? x - y : y - x) <= 1e-6 * scale;
    }
    if (a.type == PrefValue::Type::Int)
    {
        return std::strtol(a.value.c_str(), nullptr, 10) == std::strtol(b.value.c_str(), nullptr, 10);
    }
    return a.value == b.value;
}

// Files admitted in order until the total limit; returns the ones left out.
void admitFiles(const std::vector<IncludedFile>& files, std::vector<IncludedFile>& into, std::vector<Excluded>& out)
{
    std::size_t total = 0;
    std::set<std::string> seen;
    for (const IncludedFile& f : files)
    {
        if (!isBackupFilePath(f.path))
        {
            out.push_back({f.path, "is not one of the 6|7 files a backup carries"});
            continue;
        }
        if (!seen.insert(f.path).second)
        {
            out.push_back({f.path, "appears twice; the first copy is kept"});
            continue;
        }
        if (f.content.size() > kMaxFileBytes)
        {
            out.push_back({f.path, "is larger than 1 MB"});
            continue;
        }
        if (total + f.content.size() > kMaxTotalFileBytes)
        {
            out.push_back({f.path, "would take the backup's files past 3 MB"});
            continue;
        }
        total += f.content.size();
        into.push_back(f);
    }
}

void sortSettings(std::vector<SettingEntry>& settings)
{
    std::sort(settings.begin(), settings.end(), [](const SettingEntry& a, const SettingEntry& b) {
        return std::pair(a.scope, a.name) < std::pair(b.scope, b.name);
    });
}
} // namespace

std::string_view scopeKey(Scope scope)
{
    return scope == Scope::Account ? "account" : "global";
}

std::optional<Scope> parseScope(std::string_view key)
{
    if (key == "global") return Scope::Global;
    if (key == "account") return Scope::Account;
    return std::nullopt;
}

bool isBackupFilePath(std::string_view path)
{
    if (!creator::isSafeRelativePath(path))
    {
        return false;
    }
    static constexpr std::array<std::string_view, 7> kFolders = {"workspaces/", "performance/", "outfits/",
                                                                 "photoshoots/", "events/", "privacy/", "account/"};
    const bool inFolder =
        std::any_of(kFolders.begin(), kFolders.end(), [&](std::string_view f) { return path.rfind(f, 0) == 0; });
    const std::string l = lower(path);
    const bool okType = (l.size() > 5 && l.compare(l.size() - 5, 5, ".json") == 0) ||
                        (l.size() > 4 && l.compare(l.size() - 4, 4, ".xml") == 0);
    return inFolder && okType;
}

bool valueParses(const PrefValue& v)
{
    switch (v.type)
    {
    case PrefValue::Type::Bool: return v.value == "0" || v.value == "1";
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
    case PrefValue::Type::String: return v.value.size() <= kMaxValueBytes;
    }
    return false;
}

bool valueLooksSecret(std::string_view value)
{
    const std::string l = lower(value);
    static constexpr std::array<std::string_view, 12> kMarkers = {
        "password=", "passwd=", "pwd=", "token=", "apikey=", "api_key=", "secret=", "access_key=", "auth=",
        "-----begin", "bearer ", "streamkey="};
    if (std::any_of(kMarkers.begin(), kMarkers.end(), [&](std::string_view m) { return l.find(m) != std::string::npos; }))
    {
        return true;
    }
    // user:password@host in a URL.
    const std::size_t scheme = l.find("://");
    if (scheme != std::string::npos)
    {
        const std::size_t at = l.find('@', scheme + 3);
        const std::size_t slash = l.find('/', scheme + 3);
        if (at != std::string::npos && (slash == std::string::npos || at < slash))
        {
            const std::string_view userinfo = std::string_view(l).substr(scheme + 3, at - scheme - 3);
            if (userinfo.find(':') != std::string_view::npos)
            {
                return true;
            }
        }
    }
    return false;
}

BuildResult makeBackup(const std::vector<SettingEntry>& settings, const std::vector<IncludedFile>& files,
                       const BackupOptions& options)
{
    BuildResult r;
    r.backup.created = options.created;
    r.backup.viewerVersion = options.viewerVersion;
    r.backup.note = options.note.substr(0, std::min<std::size_t>(options.note.size(), 500));
    r.backup.hasAccessibility = options.includeAccessibility;
    std::set<std::pair<Scope, std::string>> seen;
    for (const SettingEntry& s : settings)
    {
        if (const auto why = policyBlock(s, options.includeAccessibility))
        {
            r.excluded.push_back({s.name, *why});
            continue;
        }
        if (!seen.insert({s.scope, s.name}).second)
        {
            continue;
        }
        r.backup.settings.push_back(s);
    }
    sortSettings(r.backup.settings);
    admitFiles(files, r.backup.files, r.excluded);
    return r;
}

std::string writeBackup(const Backup& b)
{
    Json root = Json::object();
    root.set("format", Json(kFormat));
    root.set("version", Json(kFormatVersion));
    root.set("created", Json(b.created));
    root.set("viewerVersion", Json(b.viewerVersion));
    root.set("note", Json(b.note));
    root.set("hasAccessibility", Json(b.hasAccessibility));
    Json settings = Json::array();
    for (const SettingEntry& s : b.settings)
    {
        Json e = Json::object();
        e.set("scope", Json(scopeKey(s.scope)));
        e.set("name", Json(s.name));
        e.set("type", Json(typeKey(s.value.type)));
        e.set("value", Json(s.value.value));
        settings.push(std::move(e));
    }
    root.set("settings", std::move(settings));
    Json files = Json::array();
    for (const IncludedFile& f : b.files)
    {
        Json e = Json::object();
        e.set("path", Json(f.path));
        e.set("content", Json(f.content));
        files.push(std::move(e));
    }
    root.set("files", std::move(files));
    return io::writeJson(root);
}

ReadResult readBackup(std::string_view text)
{
    ReadResult r;
    io::JsonError err;
    const auto root = io::parseJson(text, &err);
    if (!root || !root->isObject())
    {
        r.notes.push_back("This is not a readable backup file (" + (root ? std::string("not an object") : err.what) +
                          "). Nothing was changed.");
        return r;
    }
    if ((*root)["format"].asString("") != kFormat)
    {
        r.notes.push_back("This file is not a 6|7 settings backup. Nothing was changed.");
        return r;
    }
    const int version = (*root)["version"].asInt(0);
    if (version < 1 || version > kFormatVersion)
    {
        r.notes.push_back("This backup was made by a newer 6|7 World Studio (format " + std::to_string(version) +
                          "). Update the viewer to restore it. Nothing was changed.");
        return r;
    }
    Backup b;
    b.created = (*root)["created"].asString("");
    b.viewerVersion = (*root)["viewerVersion"].asString("");
    b.note = (*root)["note"].asString("");
    b.hasAccessibility = (*root)["hasAccessibility"].asBool(false);
    std::set<std::pair<Scope, std::string>> seen;
    for (const Json& e : (*root)["settings"].items())
    {
        const auto scope = parseScope(e["scope"].asString(""));
        const auto type = parseType(e["type"].asString(""));
        if (!e.isObject() || !scope || !type || !e["name"].isString() || !e["value"].isString())
        {
            r.notes.push_back("Left out a setting entry that is not complete.");
            continue;
        }
        SettingEntry s{*scope, e["name"].asString(), PrefValue{*type, e["value"].asString()}};
        if (const auto why = policyBlock(s, b.hasAccessibility))
        {
            r.notes.push_back("Left out " + s.name + ": it " + *why + ".");
            continue;
        }
        if (seen.insert({s.scope, s.name}).second)
        {
            b.settings.push_back(std::move(s));
        }
    }
    sortSettings(b.settings);
    std::vector<IncludedFile> files;
    for (const Json& e : (*root)["files"].items())
    {
        if (!e.isObject() || !e["path"].isString() || !e["content"].isString())
        {
            r.notes.push_back("Left out a file entry that is not complete.");
            continue;
        }
        files.push_back({e["path"].asString(), e["content"].asString()});
    }
    std::vector<Excluded> dropped;
    admitFiles(files, b.files, dropped);
    for (const Excluded& x : dropped)
    {
        r.notes.push_back("Left out the file " + x.name + ": it " + x.reason + ".");
    }
    for (const auto& [key, value] : root->members())
    {
        static const std::set<std::string> kKnown = {"format", "version", "created", "viewerVersion", "note",
                                                     "hasAccessibility", "settings", "files"};
        (void)value;
        if (!kKnown.count(key))
        {
            r.notes.push_back("Ignored the unknown field \"" + key + "\".");
        }
    }
    r.backup = std::move(b);
    return r;
}

std::size_t RestorePlan::changeCount() const
{
    return static_cast<std::size_t>(
        std::count_if(steps.begin(), steps.end(), [](const RestoreStep& s) { return s.changes(); }));
}

std::string RestorePlan::describe() const
{
    std::string out;
    for (const RestoreStep& s : steps)
    {
        const std::string where = s.what == RestoreStep::What::WriteFile || s.what == RestoreStep::What::KeepFile
                                      ? s.target
                                      : s.target + (s.scope == Scope::Account ? " (this account)" : "");
        switch (s.what)
        {
        case RestoreStep::What::SetSetting: out += "Change " + where + ": " + s.from + " -> " + s.to; break;
        case RestoreStep::What::WriteFile: out += (s.from.empty() ? "Add file " : "Replace file ") + where; break;
        case RestoreStep::What::SkipSame: out += "Already set: " + where; break;
        case RestoreStep::What::SkipUnknown: out += "Skip " + where; break;
        case RestoreStep::What::SkipType: out += "Skip " + where; break;
        case RestoreStep::What::SkipPolicy: out += "Skip " + where; break;
        case RestoreStep::What::KeepFile: out += "Keep your file " + where; break;
        }
        if (!s.reason.empty()) out += " (" + s.reason + ")";
        out += "\n";
    }
    return out;
}

RestorePlan planRestore(const Backup& backup, const CurrentSettings& current,
                        const std::map<std::string, std::string>& existingFiles, const RestoreOptions& options)
{
    RestorePlan plan;
    if (options.settings)
    {
        for (const SettingEntry& s : backup.settings)
        {
            RestoreStep step;
            step.scope = s.scope;
            step.target = s.name;
            step.to = shown(s.value);
            const bool isA11y = common::classifySetting(s.name) == SettingClass::Accessibility;
            if (const auto why = policyBlock(s, options.accessibility && backup.hasAccessibility))
            {
                step.what = RestoreStep::What::SkipPolicy;
                step.reason = "it " + *why;
            }
            else if (const auto it = current.find({s.scope, s.name}); it == current.end())
            {
                step.what = RestoreStep::What::SkipUnknown;
                step.reason = "this viewer has no such setting";
            }
            else if (it->second.type != s.value.type)
            {
                step.what = RestoreStep::What::SkipType;
                step.reason = "it holds a different type of value in this viewer";
            }
            else if (sameValue(it->second, s.value))
            {
                step.what = RestoreStep::What::SkipSame;
                step.from = shown(it->second);
            }
            else
            {
                step.what = RestoreStep::What::SetSetting;
                step.from = shown(it->second);
                if (isA11y) step.reason = "accessibility";
            }
            plan.steps.push_back(std::move(step));
        }
    }
    if (options.files)
    {
        for (const IncludedFile& f : backup.files)
        {
            RestoreStep step;
            step.target = f.path;
            const auto it = existingFiles.find(f.path);
            if (it == existingFiles.end())
            {
                step.what = RestoreStep::What::WriteFile;
            }
            else if (it->second == f.content)
            {
                step.what = RestoreStep::What::SkipSame;
                step.reason = "the same file is already there";
            }
            else if (options.replaceDifferentFiles)
            {
                step.what = RestoreStep::What::WriteFile;
                step.from = "existing";
                step.reason = "your current copy is kept beside it as .before-restore";
            }
            else
            {
                step.what = RestoreStep::What::KeepFile;
                step.reason = "it differs from the backup; turn on replacing files to restore it";
            }
            plan.steps.push_back(std::move(step));
        }
    }
    return plan;
}

Backup undoPointFor(const RestorePlan& plan, const CurrentSettings& current, std::string created)
{
    Backup b;
    b.created = std::move(created);
    b.note = "Settings before a restore";
    b.hasAccessibility = true;
    for (const RestoreStep& s : plan.steps)
    {
        if (s.what != RestoreStep::What::SetSetting) continue;
        const auto it = current.find({s.scope, s.target});
        if (it != current.end())
        {
            b.settings.push_back({s.scope, s.target, it->second});
        }
    }
    sortSettings(b.settings);
    return b;
}

} // namespace sw67::backup
