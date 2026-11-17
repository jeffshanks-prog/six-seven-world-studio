/**
 * @file photo_product.cpp
 * @brief Product photography planning: angles, sizes, crops, marks, names.
 *
 * Copyright (c) 2026, 6|7 World Studio contributors.
 *
 * The source code in this file is provided to you under the terms of the
 * GNU Lesser General Public License, version 2.1, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE. Terms of the LGPL can be found in doc/LGPL-license.txt
 * in this distribution, or online at http://www.gnu.org/licenses/lgpl-2.1.txt
 */

#include "sw67/photo/product.h"

#include "sw67/io/json.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>

namespace sw67::photo
{
namespace
{
constexpr double kPi = 3.14159265358979323846;

double rad(double deg)
{
    return deg * kPi / 180.0;
}

std::string upper(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return s;
}

std::string stepKindKey(EditStep::Kind k)
{
    switch (k)
    {
    case EditStep::Kind::CoverCrop: return "cover_crop";
    case EditStep::Kind::Resize: return "resize";
    case EditStep::Kind::Watermark: return "watermark";
    case EditStep::Kind::AIEdit: return "ai_edit";
    }
    return "resize";
}

std::string withoutExtension(const std::string& path)
{
    const auto slash = path.find_last_of('/');
    const auto dot = path.find_last_of('.');
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash))
    {
        return path;
    }
    return path.substr(0, dot);
}

std::string extensionOf(const std::string& path)
{
    const auto slash = path.find_last_of('/');
    const auto dot = path.find_last_of('.');
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash))
    {
        return "";
    }
    return path.substr(dot);
}
} // namespace

std::vector<CameraAngle> builtinAngles()
{
    return {{"front", 0.0, 10.0, 2.5},           {"three-quarter-left", 45.0, 15.0, 2.5},
            {"side-left", 90.0, 5.0, 2.5},       {"back", 180.0, 10.0, 2.5},
            {"three-quarter-right", -45.0, 15.0, 2.5}, {"top", 0.0, 80.0, 2.5},
            {"detail", 20.0, 10.0, 1.4}};
}

CameraPlacement placeCamera(const CameraAngle& angle, const Vec3& centre, double radius, double subjectYawDeg)
{
    const double distance = std::max(0.5, std::max(0.0, radius) * std::max(0.1, angle.distanceFactor));
    const double elevation = rad(std::clamp(angle.elevationDeg, -89.0, 89.0));
    const double azimuth = rad(angle.azimuthDeg + subjectYawDeg);
    CameraPlacement p;
    p.focus = centre;
    p.eye = {centre.x + distance * std::cos(elevation) * std::cos(azimuth),
             centre.y + distance * std::cos(elevation) * std::sin(azimuth),
             centre.z + distance * std::sin(elevation)};
    return p;
}

std::vector<ExportSize> builtinExportSizes()
{
    return {{"square1080", "Square 1080 x 1080", 1080, 1080},
            {"listing1024x768", "Listing 4:3, 1024 x 768", 1024, 768},
            {"portrait1080x1350", "Portrait 4:5, 1080 x 1350", 1080, 1350},
            {"story1080x1920", "Story 9:16, 1080 x 1920", 1080, 1920},
            {"wide1920x1080", "Wide 16:9, 1920 x 1080", 1920, 1080},
            {"thumb512", "Thumbnail 512 x 512", 512, 512}};
}

PixelRect coverCrop(int sw, int sh, int tw, int th)
{
    if (sw <= 0 || sh <= 0 || tw <= 0 || th <= 0)
    {
        return {};
    }
    // Compare sw/sh with tw/th without rounding: sw*th vs tw*sh.
    const long long lhs = static_cast<long long>(sw) * th;
    const long long rhs = static_cast<long long>(tw) * sh;
    if (lhs > rhs)
    {
        // Source is wider: keep the full height.
        const int w = static_cast<int>(std::llround(static_cast<double>(sh) * tw / th));
        return {(sw - w) / 2, 0, w, sh};
    }
    if (lhs < rhs)
    {
        const int h = static_cast<int>(std::llround(static_cast<double>(sw) * th / tw));
        return {0, (sh - h) / 2, sw, h};
    }
    return {0, 0, sw, sh};
}

PixelRect placeWatermark(const Watermark& mark, double markAspect, int iw, int ih)
{
    if (iw <= 0 || ih <= 0)
    {
        return {};
    }
    const double aspect = (markAspect > 0.0 && std::isfinite(markAspect)) ? markAspect : 1.0;
    const double widthFraction = std::clamp(mark.widthFraction, 0.02, 0.8);
    const double marginFraction = std::clamp(mark.marginFraction, 0.0, 0.2);
    int w = std::max(1, static_cast<int>(std::lround(iw * widthFraction)));
    int h = std::max(1, static_cast<int>(std::lround(w / aspect)));
    const int margin = static_cast<int>(std::lround(std::min(iw, ih) * marginFraction));
    // Keep it inside the image, margins included.
    const int maxW = std::max(1, iw - 2 * margin);
    const int maxH = std::max(1, ih - 2 * margin);
    if (w > maxW || h > maxH)
    {
        const double scale = std::min(static_cast<double>(maxW) / w, static_cast<double>(maxH) / h);
        w = std::max(1, static_cast<int>(std::floor(w * scale)));
        h = std::max(1, static_cast<int>(std::floor(h * scale)));
    }
    PixelRect r{0, 0, w, h};
    switch (mark.corner)
    {
    case Corner::TopLeft: r.x = margin; r.y = margin; break;
    case Corner::TopRight: r.x = iw - margin - w; r.y = margin; break;
    case Corner::BottomLeft: r.x = margin; r.y = ih - margin - h; break;
    case Corner::BottomRight: r.x = iw - margin - w; r.y = ih - margin - h; break;
    case Corner::Centre: r.x = (iw - w) / 2; r.y = (ih - h) / 2; break;
    }
    r.x = std::clamp(r.x, 0, iw - w);
    r.y = std::clamp(r.y, 0, ih - h);
    return r;
}

std::string safeFileStem(std::string_view text)
{
    std::string out;
    bool lastUnderscore = false;
    for (const char c : text)
    {
        const unsigned char u = static_cast<unsigned char>(c);
        const bool bad = u < 0x20 || u == 0x7F || c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' ||
                         c == '"' || c == '<' || c == '>' || c == '|' || c == ' ';
        if (bad)
        {
            if (!lastUnderscore && !out.empty())
            {
                out.push_back('_');
                lastUnderscore = true;
            }
            continue;
        }
        out.push_back(c);
        lastUnderscore = c == '_';
    }
    while (!out.empty() && (out.back() == '.' || out.back() == '_' || out.back() == '-' || out.back() == ' '))
    {
        out.pop_back();
    }
    std::size_t lead = 0;
    while (lead < out.size() && (out[lead] == '.' || out[lead] == '_'))
    {
        ++lead;
    }
    out.erase(0, lead);
    if (out.size() > 80)
    {
        out.resize(80);
        // Do not leave half a UTF-8 sequence at the end.
        while (!out.empty() && (static_cast<unsigned char>(out.back()) & 0xC0) == 0x80)
        {
            out.pop_back();
        }
        if (!out.empty() && (static_cast<unsigned char>(out.back()) & 0x80))
        {
            out.pop_back();
        }
        while (!out.empty() && (out.back() == '.' || out.back() == '_'))
        {
            out.pop_back();
        }
    }
    if (out.empty())
    {
        out = "image";
    }
    static const std::array<std::string_view, 22> kReserved{
        "CON", "PRN", "AUX", "NUL", "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7",
        "COM8", "COM9", "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"};
    const std::string base = upper(out.substr(0, out.find('.')));
    if (std::find(kReserved.begin(), kReserved.end(), base) != kReserved.end())
    {
        out = "_" + out;
    }
    return out;
}

std::string expandName(std::string_view pattern, const std::map<std::string, std::string>& values, int index)
{
    std::string out;
    std::size_t i = 0;
    while (i < pattern.size())
    {
        if (pattern[i] != '{')
        {
            out.push_back(pattern[i++]);
            continue;
        }
        const std::size_t close = pattern.find('}', i);
        if (close == std::string_view::npos)
        {
            out.append(pattern.substr(i));
            break;
        }
        std::string_view token = pattern.substr(i + 1, close - i - 1);
        i = close + 1;
        if (token.substr(0, 5) == "index")
        {
            int width = 0;
            if (token.size() > 6 && token[5] == ':')
            {
                const char d = token[6];
                if (d >= '1' && d <= '6')
                {
                    width = d - '0';
                }
            }
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%0*d", width, index);
            out += buf;
            continue;
        }
        const auto it = values.find(std::string(token));
        if (it != values.end())
        {
            out += it->second;
        }
    }
    return safeFileStem(out);
}

std::string uniqueStem(const std::string& stem, const std::set<std::string>& taken)
{
    if (!taken.count(stem))
    {
        return stem;
    }
    for (int n = 2; n < 100000; ++n)
    {
        std::string candidate = stem + "-" + std::to_string(n);
        if (!taken.count(candidate))
        {
            return candidate;
        }
    }
    return stem + "-x";
}

std::vector<Shot> planBatch(const PhotoSession& s, const std::set<std::string>& existing)
{
    std::vector<Shot> shots;
    std::set<std::string> taken;
    // Existing files are given as paths relative to the output folder; their
    // stems in originals/ are what a new name must avoid.
    for (const std::string& path : existing)
    {
        std::string p = withoutExtension(path);
        const auto slash = p.find_last_of('/');
        taken.insert(slash == std::string::npos ? p : p.substr(slash + 1));
    }

    const std::vector<std::string> backgrounds = s.backgrounds.empty() ? std::vector<std::string>{"default"} : s.backgrounds;
    const std::vector<std::string> lighting = s.lighting.empty() ? std::vector<std::string>{"current"} : s.lighting;
    const std::string originalExt = "png"; // Originals are lossless whatever was asked.
    const std::string exportExt = (s.exportFormat == "png" || s.exportFormat == "jpg") ? s.exportFormat : "jpg";
    int index = 1;
    for (const std::string& bg : backgrounds)
    {
        for (const std::string& light : lighting)
        {
            for (const CameraAngle& angle : s.angles)
            {
                Shot shot;
                shot.index = index;
                shot.angle = angle;
                shot.background = bg;
                shot.lighting = light;
                const std::map<std::string, std::string> values{{"project", s.project}, {"angle", angle.name},
                                                                {"date", s.date},       {"background", bg},
                                                                {"lighting", light},    {"size", "original"}};
                const std::string stem = uniqueStem(expandName(s.namePattern, values, index), taken);
                taken.insert(stem);
                shot.original.path = "originals/" + stem + "." + originalExt;
                shot.original.width = s.captureWidth;
                shot.original.height = s.captureHeight;
                shot.original.original = true;
                for (const ExportSize& size : s.sizes)
                {
                    PlannedFile f;
                    f.path = "exports/" + safeFileStem(size.key) + "/" + stem + "_" + safeFileStem(size.key) + "." + exportExt;
                    f.width = size.width;
                    f.height = size.height;
                    f.derivedFrom = shot.original.path;
                    const PixelRect crop = coverCrop(s.captureWidth, s.captureHeight, size.width, size.height);
                    if (crop.width != s.captureWidth || crop.height != s.captureHeight)
                    {
                        f.steps.push_back({EditStep::Kind::CoverCrop, std::to_string(crop.width) + "x" +
                                                                          std::to_string(crop.height) + "+" +
                                                                          std::to_string(crop.x) + "+" + std::to_string(crop.y)});
                    }
                    f.steps.push_back({EditStep::Kind::Resize, std::to_string(size.width) + "x" + std::to_string(size.height)});
                    if (s.watermark)
                    {
                        f.steps.push_back({EditStep::Kind::Watermark,
                                           s.watermark->text.empty() ? s.watermark->imagePath : s.watermark->text});
                    }
                    shot.exports.push_back(std::move(f));
                }
                shots.push_back(std::move(shot));
                ++index;
            }
        }
    }
    return shots;
}

PlannedFile planAIEdit(const PlannedFile& source, std::string operation)
{
    PlannedFile f;
    const std::string original = source.original ? source.path : source.derivedFrom;
    std::string base = withoutExtension(source.path);
    if (base.rfind("originals/", 0) == 0)
    {
        base = "exports/ai/" + base.substr(10);
    }
    f.path = base + std::string(kAIMarker) + extensionOf(source.path);
    f.width = source.width;
    f.height = source.height;
    f.aiAltered = true;
    f.derivedFrom = original;
    f.steps = source.original ? std::vector<EditStep>{} : source.steps;
    f.steps.push_back({EditStep::Kind::AIEdit, std::move(operation)});
    return f;
}

std::string writeBatchManifest(const PhotoSession& s, const std::vector<Shot>& shots)
{
    using io::Json;
    Json doc = Json::object();
    doc.set("format", "sw67.photo.batch");
    doc.set("version", 1);
    doc.set("project", s.project);
    doc.set("date", s.date);
    Json files = Json::array();
    auto add = [&files](const PlannedFile& f, const Shot& shot) {
        Json o = Json::object();
        o.set("path", f.path);
        o.set("width", f.width);
        o.set("height", f.height);
        o.set("original", f.original);
        o.set("aiAltered", f.aiAltered);
        if (!f.derivedFrom.empty()) o.set("derivedFrom", f.derivedFrom);
        o.set("angle", shot.angle.name);
        o.set("background", shot.background);
        o.set("lighting", shot.lighting);
        Json steps = Json::array();
        for (const EditStep& st : f.steps)
        {
            Json so = Json::object();
            so.set("kind", stepKindKey(st.kind));
            so.set("detail", st.detail);
            steps.push(so);
        }
        o.set("steps", steps);
        files.push(o);
    };
    for (const Shot& shot : shots)
    {
        add(shot.original, shot);
        for (const PlannedFile& f : shot.exports)
        {
            add(f, shot);
        }
    }
    doc.set("files", files);
    doc.set("note", "Originals are never overwritten. Exports are remade from originals using the recorded steps. "
                    "Files marked aiAltered were changed by a generative service.");
    return io::writeJson(doc);
}

} // namespace sw67::photo
