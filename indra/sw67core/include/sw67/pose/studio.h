/**
 * @file studio.h
 * @brief Pose and Photo Studio (manual): framings worked out from where the
 *        avatar stands and faces (headshot to full body, low and high angle,
 *        profiles), the user's own shots saved relative to the avatar so they
 *        work wherever it stands, and a list of poses to step through.
 *
 *        Poses play only on the user's own avatar. Posing someone else needs
 *        their consent through an in-world script; that is not part of this
 *        module and is never assumed.
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

#include "sw67/photo/product.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sw67::pose
{

inline constexpr std::string_view kFormat = "sw67.photoshoot";
inline constexpr int kFormatVersion = 1;

using photo::Vec3;

/// Where the avatar is: the position the viewer reports for it (about the
/// pelvis), the way it faces (degrees about the vertical, 0 = +X, counter-
/// clockwise seen from above) and its height in metres.
struct Subject
{
    Vec3 position;
    double yawDeg = 0.0;
    double height = 1.9;
};

/// A framing: what part of the avatar fills the frame, and from where.
struct Framing
{
    std::string key;
    std::string name;
    double centreFraction = 0.5; ///< Height of the frame's centre, as a fraction of the avatar's height from its feet.
    double spanFraction = 1.0;   ///< Vertical extent shown, as a fraction of the avatar's height.
    double azimuthDeg = 0.0;     ///< 0 in front of the avatar, 90 on its left.
    double elevationDeg = 0.0;   ///< Up from level.
};

std::vector<Framing> builtinFramings();
std::optional<Framing> findFraming(std::string_view key);

struct CameraPose
{
    Vec3 eye;
    Vec3 focus;
    double distance() const;
};

/// The camera for `framing` on `subject` with a vertical field of view of
/// `fovDeg`. The camera is never nearer than 0.3 m.
CameraPose frame(const Framing& framing, const Subject& subject, double fovDeg);

/// A shot the user saved: the camera in the avatar's own frame (x forward,
/// y to its left, z up, from its position), so it repeats wherever the
/// avatar stands and faces.
struct Shot
{
    std::string name;
    Vec3 eyeOffset;
    Vec3 focusOffset;
    double fovDeg = 60.0;
    std::string poseId; ///< The pose for this shot, if any (an inventory item UUID).
};

Shot shotFromCamera(std::string name, const CameraPose& camera, const Subject& subject, double fovDeg);
CameraPose placeShot(const Shot& shot, const Subject& subject);

/// An animation in the user's inventory, used as a pose on their own avatar.
struct Pose
{
    std::string itemId;
    std::string name;
};

/// A photo shoot: poses to step through and shots to take, with notes. Kept
/// as a plain file the user owns.
struct Photoshoot
{
    std::string name;
    std::string notes;
    std::string namePattern = "{project}_{shot}_{index:3}";
    std::vector<Pose> poses;
    std::vector<Shot> shots;
};

/// The pose after (`step` = 1) or before (-1) `current` in the list,
/// wrapping. With no current pose, the first or last.
std::optional<std::size_t> stepPose(const Photoshoot& shoot, std::optional<std::size_t> current, int step);

std::string writePhotoshoot(const Photoshoot& shoot);

struct ReadResult
{
    std::optional<Photoshoot> shoot;
    std::vector<std::string> notes;
};
ReadResult readPhotoshoot(std::string_view text);

/// A snapshot's file stem for `shot` in `shoot`, numbered `index`.
std::string snapshotStem(const Photoshoot& shoot, std::string_view shotName, int index, std::string_view date);

} // namespace sw67::pose
