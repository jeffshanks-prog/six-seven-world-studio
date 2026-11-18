/**
 * @file looks.h
 * @brief Photo Studio looks: local colour and lighting presets for photo
 *        sessions. A look is a set of the viewer's own post-processing
 *        settings (Lightbox colour grade, vignette, grain, bloom, exposure)
 *        and, optionally, one of the viewer's fixed local skies. Applying a
 *        look records what it changed so turning it off restores exactly
 *        that, keeping any setting the user changed meanwhile.
 *
 *        Everything here is local to this viewer: other residents see the
 *        region's own sky and none of these effects.
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
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace sw67::photo
{

inline constexpr std::string_view kLooksFormat = "sw67.photolooks";
inline constexpr int kLooksFormatVersion = 1;
inline constexpr std::size_t kMaxCustomLooks = 100;

using Values = std::map<std::string, std::string>;

/// The local skies a look can choose: the viewer's own fixed skies.
enum class Sky
{
    Keep,      ///< Leave the sky as it is.
    Sunrise,
    Midday,
    Sunset,
    Midnight,
};
std::string_view skyKey(Sky sky);
std::optional<Sky> parseSky(std::string_view key);

struct Look
{
    std::string key;    ///< "product_neutral"; custom looks get "custom_<name>".
    std::string name;
    std::string description;
    Values settings;    ///< Only settings in lookSettings().
    Sky sky = Sky::Keep;
    bool builtin = false;
};

/// The settings a look may set, with the range each accepts.
struct SettingRange
{
    double min = 0.0;
    double max = 1.0;
    bool boolean = false;
};
const std::map<std::string, SettingRange>& lookSettings();

/// Clamps and checks a look's values; drops anything not in lookSettings().
/// Returns the notes for what was dropped or clamped.
std::vector<std::string> sanitise(Look& look);

std::vector<Look> builtinLooks();

/// A custom look from the current values of every look setting.
Look captureLook(std::string name, const Values& current, Sky sky = Sky::Keep);

/// What applying the look would change from `current`. Applies nothing.
perf::ChangePlan planLook(const Look& look, const Values& current);

/// What a look set, kept so it can be turned off again.
struct LookState
{
    std::string key;
    std::string name;
    Values before;
    Values set;
    Sky sky = Sky::Keep;
};
LookState stateFor(const Look& look, const Values& current);

/// Turning the look off: every setting it set goes back, unless the user
/// changed it since (then it is kept, and named in `kept`).
struct RevertPlan
{
    perf::ChangePlan plan;
    std::vector<std::string> kept;
    bool restoreSky = false;  ///< The look chose a sky: go back to the region's.
};
RevertPlan planRevert(const LookState& state, const Values& current);

std::string writeLooks(const std::vector<Look>& custom, const std::optional<LookState>& active);

struct LooksFile
{
    std::vector<Look> custom;
    std::optional<LookState> active;
    std::vector<std::string> notes;
};
LooksFile readLooks(std::string_view text);

} // namespace sw67::photo
