/**
 * @file profile.h
 * @brief Workspace profiles: named layouts (Everyday, Creator, Photographer,
 *        DJ, Host, or the user's own) holding which windows are open and
 *        where, the toolbar layout, keyboard shortcuts and a set of
 *        preferences. Accessibility settings are never part of a profile,
 *        so switching profiles never changes them.
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

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sw67::workspace
{

inline constexpr std::string_view kFormat = "sw67.workspace";
inline constexpr int kFormatVersion = 1;

enum class ProfileKind
{
    Everyday,
    Creator,
    Photographer,
    DJ,
    Host,
    Custom,
};

std::string_view kindKey(ProfileKind kind);
std::string_view kindName(ProfileKind kind);
std::optional<ProfileKind> parseKind(std::string_view key);

struct Rect
{
    int left = 0;
    int top = 0;
    int width = 0;
    int height = 0;
    friend bool operator==(const Rect&, const Rect&) = default;
};

/// One window in a layout. Without a rect the window opens where the viewer
/// last put it.
struct WindowLayout
{
    std::string name; ///< The viewer's floater name, e.g. "inventory".
    bool open = true;
    std::optional<Rect> rect;
    friend bool operator==(const WindowLayout&, const WindowLayout&) = default;
};

/// A preference value, typed as the viewer's settings are.
struct PrefValue
{
    enum class Type
    {
        Bool,
        Int,
        Float,
        String
    };
    Type type = Type::String;
    std::string value; ///< "1"/"0" for Bool, decimal text for numbers.
    friend bool operator==(const PrefValue&, const PrefValue&) = default;
};

struct WorkspaceProfile
{
    std::string name;
    ProfileKind kind = ProfileKind::Custom;
    std::string description;
    std::vector<WindowLayout> windows;
    /// Close every window the profile does not list when it is applied.
    bool closeOthers = false;
    /// The toolbars.xml and key_bindings.xml the viewer writes, verbatim.
    /// Empty leaves the current toolbars or shortcuts alone.
    std::string toolbarsXml;
    std::string keyBindingsXml;
    std::map<std::string, PrefValue> preferences;
    friend bool operator==(const WorkspaceProfile&, const WorkspaceProfile&) = default;
};

/// The five built-in profiles. They list windows and a few preferences and
/// leave toolbars and shortcuts alone until the user saves their own.
WorkspaceProfile builtinProfile(ProfileKind kind);
std::vector<WorkspaceProfile> builtinProfiles();

/// A note about what was left out or changed while reading or saving.
struct Note
{
    enum class Level
    {
        Info,
        Warning,
        Error
    };
    Level level = Level::Info;
    std::string text;
};

struct ImportResult
{
    std::optional<WorkspaceProfile> profile; ///< Unset when the file can't be used.
    std::vector<Note> notes;
};

/// Removes everything a profile may not carry (secrets, accessibility and
/// consent settings, malformed names, out-of-range rectangles, unsafe XML)
/// and says what it removed.
std::vector<Note> sanitize(WorkspaceProfile& profile);

/// The profile as a JSON document. Sanitises a copy first, so an exported
/// file never holds what sanitize() removes.
std::string exportProfile(const WorkspaceProfile& profile, std::vector<Note>* notes = nullptr);

/// Reads an exported profile. A file of a newer format version is refused
/// rather than half read; unknown fields are ignored with a note.
ImportResult importProfile(std::string_view text);

/// The viewer's current state, as far as applying a profile needs it.
struct CurrentState
{
    std::map<std::string, WindowLayout> windows; ///< Every window the viewer knows, open or not.
    std::map<std::string, PrefValue> preferences; ///< At least those the profile names.
    std::string toolbarsXml;
    std::string keyBindingsXml;
};

/// One change that applying a profile makes, for the preview.
struct Change
{
    enum class What
    {
        OpenWindow,
        MoveWindow,
        CloseWindow,
        SetPreference,
        ReplaceToolbars,
        ReplaceShortcuts,
        SkipUnknownWindow,
        SkipPreference,
    };
    What what = What::SetPreference;
    std::string target;
    std::string from;
    std::string to;
    std::string describe() const;
};

/// What applying `profile` over `current` would change, in order: windows,
/// toolbars, shortcuts, preferences. Applies nothing. The viewer runs the
/// same list when the user confirms, and keeps `current` to restore.
std::vector<Change> planApply(const WorkspaceProfile& profile, const CurrentState& current);

/// The profile that puts the viewer back as `current` describes it, for
/// undoing an apply: the windows and preferences the plan touched.
WorkspaceProfile restorePoint(const WorkspaceProfile& applied, const CurrentState& current);

} // namespace sw67::workspace
