/**
 * @file pose_studio.cpp
 * @brief Pose and Photo Studio.
 *
 * Copyright (c) 2026, 6|7 World Studio contributors.
 *
 * The source code in this file is provided to you under the terms of the
 * GNU Lesser General Public License, version 2.1, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE. Terms of the LGPL can be found in doc/LGPL-license.txt
 * in this distribution, or online at http://www.gnu.org/licenses/lgpl-2.1.txt
 */

#include "sw67/pose/studio.h"

#include "sw67/creator/project.h"
#include "sw67/io/json.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <set>

namespace sw67::pose
{
namespace
{
using io::Json;

constexpr double kPi = 3.14159265358979323846;
double rad(double deg) { return deg * kPi / 180.0; }

bool finite(const Vec3& v)
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

// Offsets of a saved shot stay within a photo-studio's reach of the avatar.
bool reasonable(const Vec3& v)
{
    return finite(v) && std::fabs(v.x) <= 100.0 && std::fabs(v.y) <= 100.0 && std::fabs(v.z) <= 100.0;
}

Vec3 toLocal(const Vec3& world, const Subject& s)
{
    const double c = std::cos(rad(s.yawDeg));
    const double n = std::sin(rad(s.yawDeg));
    const double wx = world.x - s.position.x;
    const double wy = world.y - s.position.y;
    return {wx * c + wy * n, -wx * n + wy * c, world.z - s.position.z};
}

Vec3 toWorld(const Vec3& local, const Subject& s)
{
    const double c = std::cos(rad(s.yawDeg));
    const double n = std::sin(rad(s.yawDeg));
    return {s.position.x + local.x * c - local.y * n, s.position.y + local.x * n + local.y * c, s.position.z + local.z};
}

Json vecJson(const Vec3& v)
{
    Json a = Json::array();
    a.push(Json(v.x));
    a.push(Json(v.y));
    a.push(Json(v.z));
    return a;
}

std::optional<Vec3> vecFrom(const Json& j)
{
    if (!j.isArray() || j.items().size() != 3) return std::nullopt;
    for (const Json& e : j.items())
        if (!e.isNumber()) return std::nullopt;
    Vec3 v{j.items()[0].asNumber(), j.items()[1].asNumber(), j.items()[2].asNumber()};
    if (!reasonable(v)) return std::nullopt;
    return v;
}

std::string lowerId(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}
} // namespace

std::vector<Framing> builtinFramings()
{
    return {
        {"headshot", "Headshot", 0.92, 0.22, 0.0, 0.0},
        {"shoulders", "Head and shoulders", 0.86, 0.36, 0.0, 0.0},
        {"half", "Half body", 0.74, 0.55, 0.0, 0.0},
        {"three_quarter", "Three-quarter body", 0.62, 0.80, 0.0, 0.0},
        {"full", "Full body", 0.50, 1.15, 0.0, 0.0},
        {"low_hero", "Low angle, full body", 0.50, 1.15, 0.0, -18.0},
        {"high", "High angle, half body", 0.74, 0.60, 0.0, 35.0},
        {"three_quarter_left", "Three-quarter turn, left", 0.74, 0.55, 40.0, 0.0},
        {"three_quarter_right", "Three-quarter turn, right", 0.74, 0.55, -40.0, 0.0},
        {"profile_left", "Profile, left side", 0.86, 0.36, 90.0, 0.0},
        {"profile_right", "Profile, right side", 0.86, 0.36, -90.0, 0.0},
        {"back", "From behind, full body", 0.50, 1.15, 180.0, 5.0},
    };
}

std::optional<Framing> findFraming(std::string_view key)
{
    for (const Framing& f : builtinFramings())
        if (f.key == key) return f;
    return std::nullopt;
}

double CameraPose::distance() const
{
    const double dx = eye.x - focus.x, dy = eye.y - focus.y, dz = eye.z - focus.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

CameraPose frame(const Framing& framing, const Subject& subject, double fovDeg)
{
    const double height = std::clamp(subject.height, 0.2, 10.0);
    const double fov = rad(std::clamp(fovDeg, 10.0, 120.0));
    const double span = std::max(0.05, framing.spanFraction) * height;
    const double distance = std::max(0.3, (span / 2.0) / std::tan(fov / 2.0));
    const double feet = subject.position.z - height / 2.0;
    CameraPose p;
    p.focus = {subject.position.x, subject.position.y, feet + std::clamp(framing.centreFraction, 0.0, 1.2) * height};
    const double e = rad(std::clamp(framing.elevationDeg, -80.0, 80.0));
    const double a = rad(subject.yawDeg + framing.azimuthDeg);
    p.eye = {p.focus.x + distance * std::cos(e) * std::cos(a), p.focus.y + distance * std::cos(e) * std::sin(a),
             p.focus.z + distance * std::sin(e)};
    return p;
}

Shot shotFromCamera(std::string name, const CameraPose& camera, const Subject& subject, double fovDeg)
{
    Shot s;
    s.name = std::move(name);
    s.eyeOffset = toLocal(camera.eye, subject);
    s.focusOffset = toLocal(camera.focus, subject);
    s.fovDeg = std::clamp(fovDeg, 10.0, 120.0);
    return s;
}

CameraPose placeShot(const Shot& shot, const Subject& subject)
{
    return {toWorld(shot.eyeOffset, subject), toWorld(shot.focusOffset, subject)};
}

std::optional<std::size_t> stepPose(const Photoshoot& shoot, std::optional<std::size_t> current, int step)
{
    const std::size_t n = shoot.poses.size();
    if (n == 0) return std::nullopt;
    if (!current || *current >= n) return step >= 0 ? std::size_t{0} : n - 1;
    const long long next = (static_cast<long long>(*current) + step) % static_cast<long long>(n);
    return static_cast<std::size_t>(next < 0 ? next + static_cast<long long>(n) : next);
}

std::string writePhotoshoot(const Photoshoot& shoot)
{
    Json root = Json::object();
    root.set("format", Json(kFormat));
    root.set("version", Json(kFormatVersion));
    root.set("name", Json(shoot.name));
    root.set("notes", Json(shoot.notes));
    root.set("namePattern", Json(shoot.namePattern));
    Json poses = Json::array();
    for (const Pose& p : shoot.poses)
    {
        Json e = Json::object();
        e.set("itemId", Json(lowerId(p.itemId)));
        e.set("name", Json(p.name));
        poses.push(std::move(e));
    }
    root.set("poses", std::move(poses));
    Json shots = Json::array();
    for (const Shot& s : shoot.shots)
    {
        Json e = Json::object();
        e.set("name", Json(s.name));
        e.set("eye", vecJson(s.eyeOffset));
        e.set("focus", vecJson(s.focusOffset));
        e.set("fov", Json(s.fovDeg));
        if (!s.poseId.empty()) e.set("pose", Json(lowerId(s.poseId)));
        shots.push(std::move(e));
    }
    root.set("shots", std::move(shots));
    return io::writeJson(root);
}

ReadResult readPhotoshoot(std::string_view text)
{
    ReadResult r;
    const auto root = io::parseJson(text);
    if (!root || !root->isObject() || (*root)["format"].asString("") != kFormat)
    {
        r.notes.push_back("This is not a 6|7 photo shoot file.");
        return r;
    }
    const int version = (*root)["version"].asInt(0);
    if (version < 1 || version > kFormatVersion)
    {
        r.notes.push_back("This photo shoot was saved by a newer 6|7 World Studio.");
        return r;
    }
    Photoshoot s;
    s.name = (*root)["name"].asString("").substr(0, 120);
    s.notes = (*root)["notes"].asString("").substr(0, 4000);
    s.namePattern = (*root)["namePattern"].asString(s.namePattern).substr(0, 120);
    std::set<std::string> seen;
    for (const Json& e : (*root)["poses"].items())
    {
        Pose p{lowerId(e["itemId"].asString("")), e["name"].asString("").substr(0, 120)};
        if (!creator::isUuid(p.itemId))
        {
            r.notes.push_back("Left out a pose whose inventory ID is not valid.");
            continue;
        }
        if (seen.insert(p.itemId).second && s.poses.size() < 200) s.poses.push_back(std::move(p));
    }
    for (const Json& e : (*root)["shots"].items())
    {
        const auto eye = vecFrom(e["eye"]);
        const auto focus = vecFrom(e["focus"]);
        if (!eye || !focus)
        {
            r.notes.push_back("Left out a shot whose camera position is not valid or is over 100 m from you.");
            continue;
        }
        Shot shot;
        shot.name = e["name"].asString("shot").substr(0, 80);
        shot.eyeOffset = *eye;
        shot.focusOffset = *focus;
        shot.fovDeg = std::clamp(e["fov"].asNumber(60.0), 10.0, 120.0);
        const std::string pose = lowerId(e["pose"].asString(""));
        if (creator::isUuid(pose)) shot.poseId = pose;
        if (s.shots.size() < 200) s.shots.push_back(std::move(shot));
    }
    r.shoot = std::move(s);
    return r;
}

std::string snapshotStem(const Photoshoot& shoot, std::string_view shotName, int index, std::string_view date)
{
    const std::map<std::string, std::string> values = {
        {"project", shoot.name}, {"shot", std::string(shotName)}, {"angle", std::string(shotName)}, {"date", std::string(date)}};
    std::string stem = photo::expandName(shoot.namePattern, values, index);
    return stem.empty() ? "photo" : stem;
}

} // namespace sw67::pose
