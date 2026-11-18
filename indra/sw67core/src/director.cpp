/**
 * @file director.cpp
 * @brief Director Mode. See director.h.
 *
 * Copyright (c) 2026, 6|7 World Studio contributors.
 *
 * The source code in this file is provided to you under the terms of the
 * GNU Lesser General Public License, version 2.1, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE. Terms of the LGPL can be found in doc/LGPL-license.txt
 * in this distribution, or online at http://www.gnu.org/licenses/lgpl-2.1.txt
 */

#include "sw67/director/director.h"

#include "sw67/io/json.h"
#include "sw67/photo/product.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>

namespace sw67::director
{
namespace
{
using io::Json;
using photo::Vec3;

bool finite(const Vec3& v)
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

bool reasonable(const Vec3& v)
{
    return finite(v) && std::fabs(v.x) <= 100.0 && std::fabs(v.y) <= 100.0 && std::fabs(v.z) <= 100.0;
}

Vec3 add(const Vec3& a, const Vec3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 sub(const Vec3& a, const Vec3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 mul(const Vec3& a, double k) { return {a.x * k, a.y * k, a.z * k}; }

/// Catmull-Rom between p1 and p2 with neighbours p0 and p3, u in [0, 1].
Vec3 catmull(const Vec3& p0, const Vec3& p1, const Vec3& p2, const Vec3& p3, double u)
{
    const double u2 = u * u;
    const double u3 = u2 * u;
    const Vec3 a = mul(p1, 2.0);
    const Vec3 b = mul(sub(p2, p0), u);
    const Vec3 c = mul(add(sub(mul(p0, 2.0), mul(p1, 5.0)), sub(mul(p2, 4.0), p3)), u2);
    const Vec3 d = mul(add(sub(mul(p1, 3.0), p0), sub(p3, mul(p2, 3.0))), u3);
    return mul(add(add(a, b), add(c, d)), 0.5);
}

double ease(double u, Easing e)
{
    u = std::clamp(u, 0.0, 1.0);
    return e == Easing::Smooth ? u * u * (3.0 - 2.0 * u) : u;
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
    for (const Json& n : j.items())
        if (!n.isNumber()) return std::nullopt;
    const Vec3 v{j.items()[0].asNumber(), j.items()[1].asNumber(), j.items()[2].asNumber()};
    if (!reasonable(v)) return std::nullopt;
    return v;
}

Json shotJson(const pose::Shot& s)
{
    Json e = Json::object();
    e.set("name", Json(s.name));
    e.set("eye", vecJson(s.eyeOffset));
    e.set("focus", vecJson(s.focusOffset));
    e.set("fov", Json(s.fovDeg));
    return e;
}

std::optional<pose::Shot> shotFrom(const Json& e)
{
    const auto eye = vecFrom(e["eye"]);
    const auto focus = vecFrom(e["focus"]);
    if (!eye || !focus) return std::nullopt;
    pose::Shot s;
    s.name = e["name"].asString("").substr(0, 80);
    s.eyeOffset = *eye;
    s.focusOffset = *focus;
    s.fovDeg = std::clamp(e["fov"].asNumber(60.0), 10.0, 120.0);
    return s;
}
} // namespace

double CameraPath::duration() const
{
    return keys.empty() ? 0.0 : keys.back().atSeconds;
}

std::vector<std::string> validate(const CameraPath& path)
{
    std::vector<std::string> out;
    if (path.keys.size() < 2) out.push_back("A path needs at least two keys. Add the camera where it starts and where it ends.");
    if (path.keys.size() > kMaxKeys) out.push_back("A path can have at most " + std::to_string(kMaxKeys) + " keys.");
    for (std::size_t i = 0; i < path.keys.size(); ++i)
    {
        const Key& k = path.keys[i];
        if (!std::isfinite(k.atSeconds) || k.atSeconds < 0.0 || k.atSeconds > kMaxPathSeconds)
        {
            out.push_back("Key " + std::to_string(i + 1) + " has a time outside 0 to 3600 seconds.");
        }
        if (i > 0 && k.atSeconds - path.keys[i - 1].atSeconds < kMinKeyGap - 1e-9)
        {
            out.push_back("Keys " + std::to_string(i) + " and " + std::to_string(i + 1) +
                          " are less than 0.1 s apart, or out of order.");
        }
        if (!reasonable(k.shot.eyeOffset) || !reasonable(k.shot.focusOffset))
        {
            out.push_back("Key " + std::to_string(i + 1) + " is more than 100 m from the subject.");
        }
    }
    return out;
}

void sortKeys(CameraPath& path)
{
    std::stable_sort(path.keys.begin(), path.keys.end(),
                     [](const Key& a, const Key& b) { return a.atSeconds < b.atSeconds; });
    for (std::size_t i = 1; i < path.keys.size(); ++i)
    {
        const double min = path.keys[i - 1].atSeconds + kMinKeyGap;
        if (path.keys[i].atSeconds < min) path.keys[i].atSeconds = std::round(min * 1000.0) / 1000.0;
    }
}

pose::Shot sample(const CameraPath& path, double t, bool cuts)
{
    const std::vector<Key>& k = path.keys;
    if (k.empty()) return {};
    if (k.size() == 1) return k.front().shot;
    const double total = path.duration();
    if (path.loop && total > 0.0)
    {
        t = std::fmod(t, total);
        if (t < 0.0) t += total;
    }
    if (t <= k.front().atSeconds) return k.front().shot;
    if (t >= k.back().atSeconds) return k.back().shot;

    std::size_t i = 0;
    while (i + 1 < k.size() && k[i + 1].atSeconds <= t) ++i;
    if (cuts) return k[i].shot;

    const std::size_t j = i + 1;
    const double span = k[j].atSeconds - k[i].atSeconds;
    const double u = ease(span > 0.0 ? (t - k[i].atSeconds) / span : 1.0, path.easing);
    const Key& a = k[i > 0 ? i - 1 : i];
    const Key& d = k[j + 1 < k.size() ? j + 1 : j];

    pose::Shot s;
    s.name = k[i].shot.name;
    s.eyeOffset = catmull(a.shot.eyeOffset, k[i].shot.eyeOffset, k[j].shot.eyeOffset, d.shot.eyeOffset, u);
    s.focusOffset = catmull(a.shot.focusOffset, k[i].shot.focusOffset, k[j].shot.focusOffset, d.shot.focusOffset, u);
    s.fovDeg = k[i].shot.fovDeg + (k[j].shot.fovDeg - k[i].shot.fovDeg) * u;
    return s;
}

pose::CameraPose sampleWorld(const CameraPath& path, double t, const pose::Subject& subject, bool cuts)
{
    return pose::placeShot(sample(path, t, cuts), subject);
}

FramePlan planFrames(const CameraPath& path, int fps, std::string_view stem)
{
    FramePlan plan;
    if (fps < 1 || fps > 60 || path.keys.size() < 2) return plan;
    const double total = path.duration();
    const double frames = std::floor(total * fps) + 1.0;
    const int count = static_cast<int>(std::min(frames, static_cast<double>(kMaxExportFrames)));
    plan.capped = frames > kMaxExportFrames;
    const std::string base = photo::safeFileStem(stem.empty() ? std::string_view("path") : stem);
    for (int n = 0; n < count; ++n)
    {
        plan.times.push_back(static_cast<double>(n) / fps);
        char num[16];
        std::snprintf(num, sizeof(num), "_%05d", n + 1);
        plan.stems.push_back(base + num);
    }
    return plan;
}

const CameraPath* Production::findPath(std::string_view n) const
{
    for (const CameraPath& p : paths)
        if (p.name == n) return &p;
    return nullptr;
}

const pose::Shot* Production::findAngle(std::string_view n) const
{
    for (const pose::Shot& s : angles)
        if (s.name == n) return &s;
    return nullptr;
}

double Production::plannedSeconds() const
{
    double total = 0.0;
    for (const ShotEntry& s : shots) total += s.seconds;
    return total;
}

std::size_t Production::doneCount() const
{
    return static_cast<std::size_t>(std::count_if(shots.begin(), shots.end(), [](const ShotEntry& s) { return s.done; }));
}

std::vector<std::string> checkShotList(const Production& production)
{
    std::vector<std::string> out;
    for (std::size_t i = 0; i < production.shots.size(); ++i)
    {
        const ShotEntry& s = production.shots[i];
        const std::string label = "Shot " + std::to_string(i + 1) + (s.title.empty() ? "" : " (" + s.title + ")");
        if (!s.angle.empty() && !production.findAngle(s.angle))
            out.push_back(label + " uses the angle \"" + s.angle + "\", which is not saved.");
        if (!s.path.empty() && !production.findPath(s.path))
            out.push_back(label + " uses the path \"" + s.path + "\", which is not saved.");
        if (s.seconds <= 0.0) out.push_back(label + " has no length.");
    }
    return out;
}

std::string writeProduction(const Production& p)
{
    Json root = Json::object();
    root.set("format", Json(kFormat));
    root.set("version", Json(kFormatVersion));
    root.set("name", Json(p.name));
    root.set("notes", Json(p.notes));
    Json angles = Json::array();
    for (const pose::Shot& s : p.angles) angles.push(shotJson(s));
    root.set("angles", std::move(angles));
    Json paths = Json::array();
    for (const CameraPath& path : p.paths)
    {
        Json e = Json::object();
        e.set("name", Json(path.name));
        e.set("easing", Json(path.easing == Easing::Smooth ? "smooth" : "linear"));
        e.set("loop", Json(path.loop));
        Json keys = Json::array();
        for (const Key& k : path.keys)
        {
            Json kj = shotJson(k.shot);
            kj.set("at", Json(k.atSeconds));
            keys.push(std::move(kj));
        }
        e.set("keys", std::move(keys));
        paths.push(std::move(e));
    }
    root.set("paths", std::move(paths));
    Json shots = Json::array();
    for (const ShotEntry& s : p.shots)
    {
        Json e = Json::object();
        e.set("title", Json(s.title));
        if (!s.angle.empty()) e.set("angle", Json(s.angle));
        if (!s.path.empty()) e.set("path", Json(s.path));
        e.set("seconds", Json(s.seconds));
        e.set("notes", Json(s.notes));
        e.set("done", Json(s.done));
        shots.push(std::move(e));
    }
    root.set("shots", std::move(shots));
    return io::writeJson(root);
}

ReadResult readProduction(std::string_view text)
{
    ReadResult r;
    io::JsonError err;
    const auto root = io::parseJson(text, &err);
    if (!root || !root->isObject() || (*root)["format"].asString("") != kFormat)
    {
        r.notes.push_back("This is not a 6|7 Director Mode file.");
        return r;
    }
    const int version = (*root)["version"].asInt(0);
    if (version < 1 || version > kFormatVersion)
    {
        r.notes.push_back("This file was saved by a newer 6|7 World Studio. Update the viewer to open it.");
        return r;
    }
    Production p;
    p.name = (*root)["name"].asString("").substr(0, 120);
    p.notes = (*root)["notes"].asString("").substr(0, 4000);
    for (const Json& e : (*root)["angles"].items())
    {
        if (p.angles.size() >= kMaxShots) break;
        if (auto s = shotFrom(e)) p.angles.push_back(std::move(*s));
        else r.notes.push_back("Left out an angle more than 100 m from the subject, or malformed.");
    }
    for (const Json& e : (*root)["paths"].items())
    {
        if (p.paths.size() >= kMaxPaths) break;
        CameraPath path;
        path.name = e["name"].asString("path").substr(0, 80);
        path.easing = e["easing"].asString("smooth") == "linear" ? Easing::Linear : Easing::Smooth;
        path.loop = e["loop"].asBool(false);
        for (const Json& kj : e["keys"].items())
        {
            if (path.keys.size() >= kMaxKeys)
            {
                r.notes.push_back("Path \"" + path.name + "\": only the first " + std::to_string(kMaxKeys) + " keys were read.");
                break;
            }
            const double at = kj["at"].asNumber(-1.0);
            auto s = shotFrom(kj);
            if (!s || !std::isfinite(at) || at < 0.0 || at > kMaxPathSeconds)
            {
                r.notes.push_back("Path \"" + path.name + "\": left out a key with a bad time or position.");
                continue;
            }
            path.keys.push_back({at, std::move(*s)});
        }
        sortKeys(path);
        p.paths.push_back(std::move(path));
    }
    for (const Json& e : (*root)["shots"].items())
    {
        if (p.shots.size() >= kMaxShots) break;
        ShotEntry s;
        s.title = e["title"].asString("").substr(0, 120);
        s.angle = e["angle"].asString("").substr(0, 80);
        s.path = e["path"].asString("").substr(0, 80);
        s.seconds = std::clamp(e["seconds"].asNumber(5.0), 0.0, kMaxPathSeconds);
        if (!std::isfinite(s.seconds)) s.seconds = 5.0;
        s.notes = e["notes"].asString("").substr(0, 2000);
        s.done = e["done"].asBool(false);
        p.shots.push_back(std::move(s));
    }
    r.production = std::move(p);
    return r;
}

std::string fileNameFor(const Production& production)
{
    const std::string stem = production.name.empty() ? std::string("production") : photo::safeFileStem(production.name);
    return stem + ".sw67director.json";
}

} // namespace sw67::director
