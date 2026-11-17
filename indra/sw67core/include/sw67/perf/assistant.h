/**
 * @file assistant.h
 * @brief The Performance Assistant: reads the viewer's own measurements,
 *        explains likely contributors to a low frame rate or slow loading,
 *        and plans graphics presets (Everyday, Shopping, Building,
 *        Photography, Event) as a previewable, restorable list of changes to
 *        the user's own settings.
 *
 *        Every finding says whether it rests on a measurement or is an
 *        estimate, and none promises a frame rate. Presets touch only the
 *        viewer's local graphics settings: nothing here can change another
 *        resident's objects or settings.
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

namespace sw67::perf
{

/// What the viewer measured. Each field is unset when the viewer could not
/// read it, and findings that need it are then not made.
struct Sample
{
    std::optional<double> fps;
    std::optional<double> frameTimeMs;
    std::optional<double> pingMs;
    std::optional<double> packetLossPercent;
    std::optional<double> textureMemoryUsedMB;
    std::optional<double> textureMemoryBudgetMB;
    std::optional<int> texturesPending;    ///< Fetches still waiting.
    std::optional<int> avatarsNearby;       ///< Within draw distance.
    std::optional<int> avatarsFullyDrawn;   ///< Not impostored or hidden.
    std::optional<int> attachmentsHUDs;     ///< The user's own worn HUDs.
};

/// The graphics settings the assistant reads and may change. All are the
/// viewer's own; the strings are the settings' names in settings.xml.
struct GraphicsSettings
{
    std::map<std::string, std::string> values; ///< name -> value as text.
    std::optional<double> number(std::string_view name) const;
};

enum class Basis
{
    Observed,  ///< Read directly from a measurement this session.
    Estimated, ///< A likely contributor, inferred from measurements and settings.
};

std::string_view basisLabel(Basis b);

struct Finding
{
    std::string id;          ///< Stable key, e.g. "low_fps".
    Basis basis = Basis::Observed;
    std::string title;
    std::string explanation; ///< Plain language; never promises an outcome.
    std::vector<std::string> settings; ///< Settings the user could look at.
};

/// Explains the sample. Measured facts come first, then estimates.
std::vector<Finding> explain(const Sample& sample, const GraphicsSettings& settings);

enum class Preset
{
    Everyday,
    Shopping,
    Building,
    Photography,
    Event,
};

std::string_view presetName(Preset p);
std::string_view presetKey(Preset p);
std::optional<Preset> parsePreset(std::string_view key);
std::string_view presetPurpose(Preset p);

/// The settings a preset sets, by name, with the reason for each.
struct PresetValue
{
    std::string value;
    std::string why;
};
std::map<std::string, PresetValue> presetValues(Preset p);

/// The settings presets may change. A preset or restore point naming any
/// other setting is refused.
const std::vector<std::string>& managedSettings();
bool isManagedSetting(std::string_view name);

struct SettingChange
{
    std::string setting;
    std::string from; ///< Empty when the viewer has no current value.
    std::string to;
    std::string why;
};

struct ChangePlan
{
    std::string title;
    std::vector<SettingChange> changes;
    /// Shown with every plan: what the change may and may not do.
    std::string disclaimer;
    bool empty() const { return changes.empty(); }
};

/// What choosing the preset would change from `current`. Applies nothing.
ChangePlan planPreset(Preset preset, const GraphicsSettings& current);

/// The values the plan's settings had before, to restore later.
struct RestorePoint
{
    std::string label;
    std::map<std::string, std::string> values;
};
RestorePoint makeRestorePoint(const ChangePlan& plan, const GraphicsSettings& current, std::string label);

/// What restoring would change from `current` back to the restore point.
ChangePlan planRestore(const RestorePoint& point, const GraphicsSettings& current);

/// The restore point as a small JSON document and back, so it survives a
/// restart. Settings outside managedSettings() are dropped on read.
std::string writeRestorePoint(const RestorePoint& point);
std::optional<RestorePoint> readRestorePoint(std::string_view text);

} // namespace sw67::perf
