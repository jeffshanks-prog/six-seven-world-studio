/**
 * @file mesh_check.cpp
 * @brief Creator Workbench mesh checks. See mesh_check.h.
 *
 * Copyright (c) 2026, 6|7 World Studio contributors.
 *
 * The source code in this file is provided to you under the terms of the
 * GNU Lesser General Public License, version 2.1, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE. Terms of the LGPL can be found in doc/LGPL-license.txt
 * in this distribution, or online at http://www.gnu.org/licenses/lgpl-2.1.txt
 */

#include "sw67/creator/mesh_check.h"

#include "sw67/io/json.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>

namespace sw67::creator
{
namespace
{
using io::Json;
using Level = MeshFinding::Level;

std::string lower(std::string_view s)
{
    std::string out(s);
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

void add(MeshReport& r, Level level, std::string text)
{
    r.findings.push_back({level, std::move(text)});
}

/// A start tag found by the scanner: its name and attribute text.
struct Tag
{
    std::string name;
    std::string attrs;
    std::size_t end = 0; ///< Just past '>'.
};

/// The next start tag at or after `pos` (comments, declarations and end tags skipped).
bool nextTag(std::string_view text, std::size_t& pos, Tag& tag)
{
    while (pos < text.size())
    {
        const std::size_t lt = text.find('<', pos);
        if (lt == std::string_view::npos || lt + 1 >= text.size()) return false;
        if (text.compare(lt, 4, "<!--") == 0)
        {
            const std::size_t e = text.find("-->", lt + 4);
            pos = e == std::string_view::npos ? text.size() : e + 3;
            continue;
        }
        const std::size_t gt = text.find('>', lt);
        if (gt == std::string_view::npos) return false;
        const char c = text[lt + 1];
        if (c == '/' || c == '?' || c == '!')
        {
            pos = gt + 1;
            continue;
        }
        std::size_t n = lt + 1;
        while (n < gt && !std::isspace(static_cast<unsigned char>(text[n])) && text[n] != '/') ++n;
        tag.name = std::string(text.substr(lt + 1, n - lt - 1));
        tag.attrs = std::string(text.substr(n, gt - n));
        tag.end = gt + 1;
        pos = gt + 1;
        return true;
    }
    return false;
}

std::string attr(const std::string& attrs, std::string_view name)
{
    std::size_t p = 0;
    while ((p = attrs.find(name, p)) != std::string::npos)
    {
        const bool start = p == 0 || std::isspace(static_cast<unsigned char>(attrs[p - 1]));
        std::size_t q = p + name.size();
        while (q < attrs.size() && std::isspace(static_cast<unsigned char>(attrs[q]))) ++q;
        if (start && q < attrs.size() && attrs[q] == '=')
        {
            ++q;
            while (q < attrs.size() && std::isspace(static_cast<unsigned char>(attrs[q]))) ++q;
            if (q < attrs.size() && (attrs[q] == '"' || attrs[q] == '\''))
            {
                const char quote = attrs[q];
                const std::size_t e = attrs.find(quote, q + 1);
                if (e != std::string::npos) return attrs.substr(q + 1, e - q - 1);
            }
        }
        p += name.size();
    }
    return {};
}

long long toLong(const std::string& s)
{
    if (s.empty() || s.size() > 12) return 0;
    long long v = 0;
    for (char c : s)
    {
        if (!std::isdigit(static_cast<unsigned char>(c))) return 0;
        v = v * 10 + (c - '0');
    }
    return v;
}

void checkCollada(std::string_view text, MeshReport& r)
{
    if (text.find("<COLLADA") == std::string_view::npos)
    {
        add(r, Level::Error, "This is not a Collada file: it has no <COLLADA> element.");
        return;
    }
    std::size_t pos = 0;
    Tag tag;
    std::set<std::string> currentMaterials;
    bool inGeometry = false;
    int images = 0;
    while (nextTag(text, pos, tag))
    {
        if (tag.name == "geometry")
        {
            if (inGeometry)
            {
                r.maxFacesInAMesh = std::max(r.maxFacesInAMesh, static_cast<int>(currentMaterials.size()));
            }
            inGeometry = true;
            currentMaterials.clear();
            ++r.meshes;
        }
        else if (tag.name == "triangles" || tag.name == "polylist" || tag.name == "polygons")
        {
            currentMaterials.insert(attr(tag.attrs, "material"));
            const long long count = toLong(attr(tag.attrs, "count"));
            r.triangles += count; // a polygon is at least one triangle
            if (tag.name != "triangles")
            {
                add(r, Level::Info, "The file has polygons as well as triangles; the uploader triangulates them, "
                                    "so the triangle count rises.");
            }
        }
        else if (tag.name == "controller")
        {
            r.rigged = true;
        }
        else if (tag.name == "Name_array" || tag.name == "IDREF_array")
        {
            const long long count = toLong(attr(tag.attrs, "count"));
            if (r.rigged) r.joints = std::max(r.joints, static_cast<int>(std::min<long long>(count, 100000)));
        }
        else if (tag.name == "unit")
        {
            const std::string meter = attr(tag.attrs, "meter");
            if (!meter.empty() && meter != "1" && meter != "1.0")
            {
                add(r, Level::Warning, "The file's unit is " + meter + " m, not 1 m. Check the size in the uploader's preview.");
            }
        }
        else if (tag.name == "image")
        {
            ++images;
        }
    }
    if (inGeometry) r.maxFacesInAMesh = std::max(r.maxFacesInAMesh, static_cast<int>(currentMaterials.size()));
    if (images > 0)
    {
        add(r, Level::Info, std::to_string(images) + " texture(s) are referenced. Include them in the upload only "
                            "if you mean to; each is charged.");
    }
    // The informational polygon note may repeat; keep one.
    auto& f = r.findings;
    f.erase(std::unique(f.begin(), f.end(), [](const MeshFinding& a, const MeshFinding& b) { return a.text == b.text; }),
            f.end());
}

void checkGltfJson(const Json& root, MeshReport& r, bool glb)
{
    if (!root.isObject() || !root["asset"].isObject())
    {
        add(r, Level::Error, "This is not a glTF file: it has no asset section.");
        return;
    }
    const std::string version = root["asset"]["version"].asString("");
    if (version.rfind("2.", 0) != 0)
    {
        add(r, Level::Error, "glTF " + (version.empty() ? std::string("of unknown version") : version) +
                                 " is not supported; export glTF 2.0.");
    }
    for (const Json& ext : root["extensionsRequired"].items())
    {
        const std::string e = ext.asString("");
        if (e == "KHR_draco_mesh_compression" || e == "EXT_meshopt_compression")
        {
            add(r, Level::Error, "The file is compressed with " + e + ", which the uploader cannot read. Export without mesh compression.");
        }
        else if (!e.empty())
        {
            add(r, Level::Warning, "The file requires the extension " + e + ", which the uploader may not support.");
        }
    }
    const auto& accessors = root["accessors"].items();
    double mins[3] = {1e30, 1e30, 1e30};
    double maxs[3] = {-1e30, -1e30, -1e30};
    bool haveBounds = false;
    for (const Json& mesh : root["meshes"].items())
    {
        ++r.meshes;
        const auto& prims = mesh["primitives"].items();
        r.maxFacesInAMesh = std::max(r.maxFacesInAMesh, static_cast<int>(prims.size()));
        for (const Json& p : prims)
        {
            const int mode = p["mode"].asInt(4);
            if (mode != 4)
            {
                add(r, Level::Warning, "A primitive is not made of triangles (mode " + std::to_string(mode) +
                                           "); the uploader reads triangles.");
            }
            const int posIndex = p["attributes"]["POSITION"].asInt(-1);
            long long verts = 0;
            if (posIndex >= 0 && static_cast<std::size_t>(posIndex) < accessors.size())
            {
                const Json& acc = accessors[static_cast<std::size_t>(posIndex)];
                verts = static_cast<long long>(acc["count"].asNumber(0));
                const auto& mn = acc["min"].items();
                const auto& mx = acc["max"].items();
                if (mn.size() == 3 && mx.size() == 3)
                {
                    haveBounds = true;
                    for (std::size_t a = 0; a < 3; ++a)
                    {
                        mins[a] = std::min(mins[a], mn[a].asNumber(0));
                        maxs[a] = std::max(maxs[a], mx[a].asNumber(0));
                    }
                }
            }
            r.vertices += verts;
            const int idx = p["indices"].asInt(-1);
            if (idx >= 0 && static_cast<std::size_t>(idx) < accessors.size())
            {
                r.triangles += static_cast<long long>(accessors[static_cast<std::size_t>(idx)]["count"].asNumber(0)) / 3;
            }
            else
            {
                r.triangles += verts / 3;
            }
            if (verts > kVerticesPerFaceSplit)
            {
                add(r, Level::Info, "A face has " + std::to_string(verts) + " vertices; the uploader splits faces "
                                        "past 65,535, which adds faces.");
            }
        }
    }
    if (haveBounds)
    {
        for (int a = 0; a < 3; ++a) r.sizeMetres[a] = std::max(0.0, maxs[a] - mins[a]);
    }
    for (const Json& skin : root["skins"].items())
    {
        r.rigged = true;
        r.joints = std::max(r.joints, static_cast<int>(skin["joints"].items().size()));
    }
    for (const Json& b : root["buffers"].items())
    {
        const std::string uri = b["uri"].asString("");
        if (!glb && !uri.empty() && uri.rfind("data:", 0) != 0)
        {
            add(r, Level::Info, "The file reads its data from \"" + uri + "\"; keep that file beside it.");
        }
    }
}

io::JsonLimits gltfLimits()
{
    io::JsonLimits l;
    l.maxBytes = kMaxCheckBytes;       // Embedded data: URIs make .gltf files large.
    l.maxStringBytes = kMaxCheckBytes;
    l.maxItems = 1000000;
    return l;
}

std::uint32_t le32(std::string_view b, std::size_t at)
{
    return static_cast<std::uint32_t>(static_cast<unsigned char>(b[at])) |
           static_cast<std::uint32_t>(static_cast<unsigned char>(b[at + 1])) << 8 |
           static_cast<std::uint32_t>(static_cast<unsigned char>(b[at + 2])) << 16 |
           static_cast<std::uint32_t>(static_cast<unsigned char>(b[at + 3])) << 24;
}
} // namespace

MeshFormat formatFor(std::string_view fileName)
{
    const std::string n = lower(fileName);
    auto ends = [&](std::string_view s) { return n.size() >= s.size() && n.compare(n.size() - s.size(), s.size(), s) == 0; };
    if (ends(".dae")) return MeshFormat::Collada;
    if (ends(".gltf")) return MeshFormat::Gltf;
    if (ends(".glb")) return MeshFormat::Glb;
    return MeshFormat::Unknown;
}

bool MeshReport::canOpenInUploader() const
{
    return format != MeshFormat::Unknown &&
           std::none_of(findings.begin(), findings.end(), [](const MeshFinding& f) { return f.level == Level::Error; });
}

std::string MeshReport::describe() const
{
    std::string out;
    const char* fmt = format == MeshFormat::Collada ? "Collada" : format == MeshFormat::Gltf ? "glTF"
                    : format == MeshFormat::Glb     ? "glTF binary" : "unknown";
    out += std::string("Format: ") + fmt + "\n";
    out += "Meshes: " + std::to_string(meshes) + ", most faces in one mesh: " + std::to_string(maxFacesInAMesh) +
           " (limit " + std::to_string(kMaxFacesPerMesh) + ")\n";
    out += "Triangles: " + std::to_string(triangles) + (vertices ? ", vertices: " + std::to_string(vertices) : "") + "\n";
    if (rigged) out += "Rigged: yes, " + std::to_string(joints) + " joint(s) in the largest skin (limit " +
                       std::to_string(kMaxJointsPerMesh) + ")\n";
    if (sizeMetres[0] > 0 || sizeMetres[1] > 0 || sizeMetres[2] > 0)
    {
        char buf[96];
        std::snprintf(buf, sizeof(buf), "Size: %.2f x %.2f x %.2f m\n", sizeMetres[0], sizeMetres[1], sizeMetres[2]);
        out += buf;
    }
    int errors = 0;
    for (const MeshFinding& f : findings)
    {
        if (f.level == Level::Error) ++errors;
        out += std::string(f.level == Level::Error ? "Error: " : f.level == Level::Warning ? "Warning: " : "Note: ") + f.text + "\n";
    }
    out += errors ? "\nFix the errors before uploading."
                  : "\nNothing here is known to fail. The uploader's own checks and its fee quote still decide.";
    return out;
}

MeshReport checkMesh(std::string_view fileName, std::string_view bytes)
{
    MeshReport r;
    r.format = formatFor(fileName);
    if (r.format == MeshFormat::Unknown)
    {
        add(r, Level::Error, "Second Life's uploader reads .dae (Collada), .gltf and .glb files.");
        return r;
    }
    if (bytes.empty())
    {
        add(r, Level::Error, "The file is empty.");
        return r;
    }
    if (bytes.size() > kMaxCheckBytes)
    {
        add(r, Level::Error, "The file is over 512 MB, too large to check or upload.");
        return r;
    }
    if (r.format == MeshFormat::Collada)
    {
        checkCollada(bytes, r);
    }
    else if (r.format == MeshFormat::Gltf)
    {
        io::JsonError err;
        const auto root = io::parseJson(bytes, &err, gltfLimits());
        if (!root) add(r, Level::Error, "The glTF file is not valid JSON.");
        else checkGltfJson(*root, r, false);
    }
    else
    {
        if (bytes.size() < 20 || bytes.substr(0, 4) != "glTF")
        {
            add(r, Level::Error, "This is not a binary glTF (.glb) file.");
            return r;
        }
        const std::uint32_t chunkLen = le32(bytes, 12);
        if (bytes.compare(16, 4, "JSON") != 0 || static_cast<std::size_t>(chunkLen) + 20 > bytes.size())
        {
            add(r, Level::Error, "The .glb file's JSON chunk is missing or cut short.");
            return r;
        }
        io::JsonError err;
        const auto root = io::parseJson(bytes.substr(20, chunkLen), &err, gltfLimits());
        if (!root) add(r, Level::Error, "The .glb file's JSON chunk is not valid JSON.");
        else checkGltfJson(*root, r, true);
    }
    if (r.meshes == 0 && std::none_of(r.findings.begin(), r.findings.end(),
                                      [](const MeshFinding& f) { return f.level == Level::Error; }))
    {
        add(r, Level::Error, "The file has no mesh in it.");
    }
    if (r.maxFacesInAMesh > kMaxFacesPerMesh)
    {
        add(r, Level::Error, "A mesh has " + std::to_string(r.maxFacesInAMesh) + " materials; Second Life allows " +
                                 std::to_string(kMaxFacesPerMesh) + " faces per mesh. Merge materials or split the mesh.");
    }
    if (r.rigged && r.joints > kMaxJointsPerMesh)
    {
        add(r, Level::Error, "A skin uses " + std::to_string(r.joints) + " joints; Second Life allows " +
                                 std::to_string(kMaxJointsPerMesh) + " per mesh.");
    }
    for (int a = 0; a < 3; ++a)
    {
        if (r.sizeMetres[a] > kMaxObjectSize)
        {
            add(r, Level::Warning, "The model is over 64 m along one side; most regions limit objects to 64 m, and "
                                   "the uploader will scale it.");
            break;
        }
    }
    return r;
}

} // namespace sw67::creator
