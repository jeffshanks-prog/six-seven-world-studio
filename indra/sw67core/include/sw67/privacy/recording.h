/**
 * @file recording.h
 * @brief Recording Privacy Mode: one switch that hides, on this screen only,
 *        what a stream or recording should not show: name tags (yours and
 *        others'), group titles, chat and IM pop-ups, chat bubbles, your
 *        location in the navigation bar, your L$ balance and hover tips.
 *
 *        It changes only this viewer's own display settings. Nothing is
 *        sent, nobody else's view changes, and it does not start, stop or
 *        detect any recording. Turning it off puts back exactly the values
 *        it changed, except any the user changed again while it was on.
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

#include "sw67/perf/assistant.h"

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sw67::privacy
{

inline constexpr std::string_view kFormat = "sw67.privacy";
inline constexpr int kFormatVersion = 1;

/// Each thing privacy mode can hide. All are on by default.
enum class Hide
{
    MyName,        ///< Your own name tag.
    OtherNames,    ///< Everyone else's name tags.
    GroupTitles,
    ChatPopups,    ///< Nearby chat toasts.
    IMPopups,      ///< IM, group and conference toasts, and object IMs.
    ChatBubbles,
    Location,      ///< Navigation bar and mini location panel.
    Balance,       ///< L$ in the status bar and in snapshots.
    HoverTips,     ///< In-world hover tips (object and land names).
    Count
};

std::string_view hideKey(Hide h);
std::string_view hideLabel(Hide h);
std::optional<Hide> parseHide(std::string_view key);

struct Options
{
    std::map<Hide, bool> hide; ///< Missing entries count as on.
    bool hides(Hide h) const;
};

/// One display setting privacy mode sets, and to what.
struct Managed
{
    std::string setting;
    std::string value; ///< "1"/"0" for booleans, decimal or text otherwise.
    Hide because = Hide::Count;
};

/// The settings an option sets, in the viewer's own names.
std::vector<Managed> settingsFor(Hide h);

/// Every setting privacy mode may ever change. Nothing else is touched.
const std::vector<std::string>& managedSettings();
bool isManagedSetting(std::string_view name);

using Values = std::map<std::string, std::string>; ///< Setting -> value, as perf::GraphicsSettings holds them.

/// What turning privacy mode on would change, from `current`. Settings
/// already hiding are not listed. Applies nothing.
perf::ChangePlan planEnable(const Options& options, const Values& current);

/// What privacy mode remembers while it is on: each value it changed, as it
/// was before and as privacy mode set it.
struct State
{
    bool active = false;
    Options options;
    Values before; ///< Setting -> value before privacy mode.
    Values set;    ///< Setting -> value privacy mode set.
};

/// The state to save before applying `plan`.
State stateFor(const Options& options, const perf::ChangePlan& plan);

struct DisablePlan
{
    perf::ChangePlan plan;          ///< Settings put back.
    std::vector<std::string> kept;  ///< Settings the user changed while privacy mode was on: left as they are.
};

/// What turning privacy mode off would change. A setting the user changed
/// again while it was on is kept as they set it, and listed.
DisablePlan planDisable(const State& state, const Values& current);

/// The state as JSON and back, so privacy mode can be turned off after a
/// restart or a crash. A tampered file cannot name a setting outside
/// managedSettings().
std::string writeState(const State& state);
std::optional<State> readState(std::string_view text);

/// The line the status indicator shows.
std::string statusLine(bool active, std::size_t hiddenCount);

} // namespace sw67::privacy
