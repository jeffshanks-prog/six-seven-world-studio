/**
 * @file director.h
 * @brief Director Mode: saved camera angles, editable camera paths that play
 *        smoothly (or as cuts, with reduced motion), shot lists, and the
 *        frame timetable for exporting a path as an image sequence through
 *        the viewer's snapshot capture.
 *
 *        Everything here is local. A camera path moves only this viewer's
 *        camera; nothing is sent to other residents and no effect is shown
 *        to anyone else.
 *
 *        Keys are kept in the subject's frame (pose::Shot), so a path made
 *        around your avatar replays wherever you stand.
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

#include "sw67/pose/studio.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sw67::director
{

inline constexpr std::string_view kFormat = "sw67.director";
inline constexpr int kFormatVersion = 1;
inline constexpr std::size_t kMaxKeys = 200;
inline constexpr std::size_t kMaxPaths = 100;
inline constexpr std::size_t kMaxShots = 300;
inline constexpr double kMaxPathSeconds = 3600.0;
inline constexpr double kMinKeyGap = 0.1;
inline constexpr int kMaxExportFrames = 18000; ///< Ten minutes at 30 frames a second.

struct Key
{
    double atSeconds = 0.0;
    pose::Shot shot;     ///< Eye and focus offsets in the subject's frame, and the field of view.
};

enum class Easing
{
    Linear,
    Smooth,  ///< Ease in and out of every key.
};

struct CameraPath
{
    std::string name;
    std::vector<Key> keys;  ///< Sorted by time.
    Easing easing = Easing::Smooth;
    bool loop = false;

    double duration() const;
};

/// Problems that stop a path playing, one line each; empty when it can.
std::vector<std::string> validate(const CameraPath& path);

/// Keeps the keys in time order; a key closer than kMinKeyGap to another is
/// pushed later so no two keys share a moment.
void sortKeys(CameraPath& path);

/// The camera at time `t` (seconds), in the subject's frame. A Catmull-Rom
/// spline through the keys' eye and focus offsets, eased per `path.easing`;
/// the field of view moves linearly. With `cuts` (reduced motion), the camera
/// holds each key until the next one's time and then cuts, with no movement
/// in between.
pose::Shot sample(const CameraPath& path, double t, bool cuts = false);

/// The camera in the world at time `t`, for a subject standing as `subject` does.
pose::CameraPose sampleWorld(const CameraPath& path, double t, const pose::Subject& subject, bool cuts = false);

/// Exporting a path: the time of every frame at `fps`, capped at
/// kMaxExportFrames, and a numbered file stem for each.
struct FramePlan
{
    std::vector<double> times;
    std::vector<std::string> stems;
    bool capped = false;
};
FramePlan planFrames(const CameraPath& path, int fps, std::string_view stem);

/// A shot list entry: what to film, with which saved angle or path, how long.
struct ShotEntry
{
    std::string title;
    std::string angle;     ///< A saved angle's name, or empty.
    std::string path;      ///< A camera path's name, or empty.
    double seconds = 5.0;
    std::string notes;
    bool done = false;
};

struct Production
{
    std::string name;
    std::string notes;
    std::vector<pose::Shot> angles;  ///< Saved camera angles.
    std::vector<CameraPath> paths;
    std::vector<ShotEntry> shots;

    const CameraPath* findPath(std::string_view name) const;
    const pose::Shot* findAngle(std::string_view name) const;
    double plannedSeconds() const;   ///< The shot list's total.
    std::size_t doneCount() const;
};

/// Shot list problems: entries naming an angle or path that does not exist.
std::vector<std::string> checkShotList(const Production& production);

std::string writeProduction(const Production& production);

struct ReadResult
{
    std::optional<Production> production;
    std::vector<std::string> notes;
};
/// Reads a production file: newer formats refused, offsets over 100 m,
/// out-of-range times and malformed entries dropped with a note.
ReadResult readProduction(std::string_view text);

std::string fileNameFor(const Production& production);

} // namespace sw67::director
