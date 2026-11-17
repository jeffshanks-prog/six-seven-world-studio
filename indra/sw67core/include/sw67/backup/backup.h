/**
 * @file backup.h
 * @brief Settings Backup and Restore: the user's own viewer settings and
 *        6|7 files (workspace profiles, outfits, photo shoots, event plans,
 *        toolbars and shortcuts) in one plain JSON file they keep, and a
 *        restore that previews every change before making it.
 *
 *        What goes in is decided by sw67::common::classifySetting, the rule
 *        workspace profiles use too: passwords, tokens, API and stream keys
 *        and anything credential-like never enter a backup, and consent
 *        opt-ins (RLV, captioning, transcripts) are never switched on by a
 *        restore. Accessibility settings go in and come back only when the
 *        user leaves that choice on. A restore never deletes anything.
 *
 * Copyright (c) 2026, 6|7 World Studio contributors.
 *
 * The source code in this file is provided to you under the terms of the
 * GNU Lesser General Public License, version 2.1, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE. Terms of the LGPL can be found in doc/LGPL-license.txt
 * in this distribution, or online at http://www.gnu.org/licenses/lgpl-2.1.txt
 */

#pragma once

#include "sw67/common/settings_policy.h"
#include "sw67/workspace/profile.h"

#include <cstddef>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sw67::backup
{

inline constexpr std::string_view kFormat = "sw67.backup";
inline constexpr int kFormatVersion = 1;

/// Largest single file a backup carries, and all of them together. The
/// total stays under the JSON reader's 4 MB limit with room for settings.
inline constexpr std::size_t kMaxFileBytes = 1024u * 1024u;
inline constexpr std::size_t kMaxTotalFileBytes = 3u * 1024u * 1024u;
inline constexpr std::size_t kMaxValueBytes = 16u * 1024u;

/// Which of the viewer's settings groups a setting lives in.
enum class Scope
{
    Global,  ///< Settings for every account on this computer.
    Account, ///< Settings of the account that is logged in.
};

std::string_view scopeKey(Scope scope);
std::optional<Scope> parseScope(std::string_view key);

using workspace::PrefValue;

struct SettingEntry
{
    Scope scope = Scope::Global;
    std::string name;
    PrefValue value;
    friend bool operator==(const SettingEntry&, const SettingEntry&) = default;
};

/// One of our files, by its path under the viewer's `sw67` folder, e.g.
/// "workspaces/My desk.sw67workspace.json" or "account/toolbars.xml".
struct IncludedFile
{
    std::string path;
    std::string content;
    friend bool operator==(const IncludedFile&, const IncludedFile&) = default;
};

/// True for a path a backup may carry: relative, inside one of our folders
/// (workspaces, performance, outfits, photoshoots, events, privacy,
/// account), and a .json or .xml file.
bool isBackupFilePath(std::string_view path);

struct Backup
{
    std::string created;       ///< When it was made, as the viewer wrote it.
    std::string viewerVersion; ///< For information; restore does not need it.
    std::string note;          ///< The user's own label.
    bool hasAccessibility = false; ///< Whether accessibility settings were included.
    std::vector<SettingEntry> settings;
    std::vector<IncludedFile> files;
    friend bool operator==(const Backup&, const Backup&) = default;
};

struct Excluded
{
    std::string name;
    std::string reason;
};

struct BackupOptions
{
    bool includeAccessibility = true;
    std::string created;
    std::string viewerVersion;
    std::string note;
};

struct BuildResult
{
    Backup backup;
    std::vector<Excluded> excluded; ///< What was left out, and why.
};

/// Builds a backup from the settings the viewer holds and our files.
/// Secrets, consent opt-ins and malformed names are always left out;
/// accessibility settings unless chosen. A string value that looks like it
/// holds a credential is left out whatever its name. Files outside our
/// folders, or over the size limits, are left out too.
BuildResult makeBackup(const std::vector<SettingEntry>& settings, const std::vector<IncludedFile>& files,
                       const BackupOptions& options);

std::string writeBackup(const Backup& backup);

struct ReadResult
{
    std::optional<Backup> backup;
    std::vector<std::string> notes; ///< Anything dropped or refused, for the user.
};

/// Reads a backup file. Newer formats and other files are refused. The same
/// rules as makeBackup are applied again, so a hand-edited file cannot carry
/// a password or switch RLV on.
ReadResult readBackup(std::string_view text);

/// The viewer's settings as they are now, by scope and name. Only settings
/// this viewer has can be restored.
using CurrentSettings = std::map<std::pair<Scope, std::string>, PrefValue>;

struct RestoreOptions
{
    bool settings = true;
    bool accessibility = true;
    bool files = true;
    bool replaceDifferentFiles = false; ///< Off: a file that exists with other content is kept.
};

struct RestoreStep
{
    enum class What
    {
        SetSetting,
        WriteFile,
        SkipSame,       ///< Already as the backup has it.
        SkipUnknown,    ///< This viewer has no such setting.
        SkipType,       ///< The setting has another type here, or the value does not parse.
        SkipPolicy,     ///< The rules keep it out (secret, consent, accessibility left off).
        KeepFile,       ///< A file with other content exists and is kept.
    };
    What what = What::SkipSame;
    Scope scope = Scope::Global;
    std::string target; ///< Setting name or file path.
    std::string from;
    std::string to;
    std::string reason;

    bool changes() const { return what == What::SetSetting || what == What::WriteFile; }
};

struct RestorePlan
{
    std::vector<RestoreStep> steps;
    std::size_t changeCount() const;
    std::string describe() const; ///< One line a step, for the preview.
};

/// What restoring `backup` would do. Changes nothing.
RestorePlan planRestore(const Backup& backup, const CurrentSettings& current,
                        const std::map<std::string, std::string>& existingFiles, const RestoreOptions& options);

/// A backup of the current values of every setting `plan` changes, made
/// before a restore so the restore itself can be undone. Files are not in
/// it: a restore writes new files or replaces only when asked, and the
/// replaced file is kept by the viewer beside it.
Backup undoPointFor(const RestorePlan& plan, const CurrentSettings& current, std::string created);

/// Whether `value` parses as `type` ("1"/"0" for Bool, decimal for numbers).
bool valueParses(const PrefValue& value);

/// Whether a string value looks like it holds a credential (a password or
/// token in a URL or query, a private key block).
bool valueLooksSecret(std::string_view value);

} // namespace sw67::backup
