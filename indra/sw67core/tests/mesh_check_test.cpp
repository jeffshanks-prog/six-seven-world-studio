/**
 * @file mesh_check_test.cpp
 * @brief Creator Workbench mesh checks.
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

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>

using namespace sw67::creator;

namespace
{
bool hasFinding(const MeshReport& r, MeshFinding::Level level, const std::string& part)
{
    return std::any_of(r.findings.begin(), r.findings.end(), [&](const MeshFinding& f) {
        return f.level == level && f.text.find(part) != std::string::npos;
    });
}

std::string collada(int materials, bool rigged = false, int joints = 0, const char* meter = "1")
{
    std::string s = "<?xml version=\"1.0\"?>\n<COLLADA version=\"1.4.1\"><asset><unit meter=\"";
    s += meter;
    s += "\" name=\"meter\"/><up_axis>Z_UP</up_axis></asset><library_geometries><geometry id=\"g\"><mesh>";
    for (int i = 0; i < materials; ++i)
    {
        s += "<triangles material=\"m" + std::to_string(i) + "\" count=\"10\"><p>0 1 2</p></triangles>";
    }
    s += "</mesh></geometry></library_geometries>";
    if (rigged)
    {
        s += "<library_controllers><controller id=\"c\"><skin><source><Name_array count=\"" + std::to_string(joints) +
             "\">a b c</Name_array></source></skin></controller></library_controllers>";
    }
    s += "</COLLADA>";
    return s;
}

std::string gltf(int primitives, const std::string& extra = "", double size = 2.0)
{
    std::string prims;
    for (int i = 0; i < primitives; ++i)
    {
        if (i) prims += ",";
        prims += "{\"attributes\":{\"POSITION\":0},\"indices\":1}";
    }
    const std::string half = std::to_string(size / 2.0);
    return "{\"asset\":{\"version\":\"2.0\"},\"meshes\":[{\"primitives\":[" + prims +
           "]}],\"accessors\":[{\"count\":24,\"min\":[-" + half + ",-" + half + ",-" + half + "],\"max\":[" + half +
           "," + half + "," + half + "]},{\"count\":36}]" + extra + "}";
}
} // namespace

TEST_CASE("Mesh check knows the uploader's formats", "[creator]")
{
    CHECK(formatFor("Chair.DAE") == MeshFormat::Collada);
    CHECK(formatFor("chair.gltf") == MeshFormat::Gltf);
    CHECK(formatFor("chair.glb") == MeshFormat::Glb);
    CHECK(formatFor("chair.fbx") == MeshFormat::Unknown);
    const MeshReport r = checkMesh("chair.fbx", "x");
    CHECK_FALSE(r.canOpenInUploader());
    CHECK(hasFinding(r, MeshFinding::Level::Error, ".dae"));
    CHECK_FALSE(checkMesh("chair.dae", "").canOpenInUploader());
}

TEST_CASE("Collada within the limits can open in the uploader", "[creator]")
{
    const MeshReport r = checkMesh("chair.dae", collada(3));
    CHECK(r.meshes == 1);
    CHECK(r.maxFacesInAMesh == 3);
    CHECK(r.triangles == 30);
    CHECK_FALSE(r.rigged);
    CHECK(r.canOpenInUploader());
    CHECK(r.describe().find("Nothing here is known to fail") != std::string::npos);
}

TEST_CASE("Collada past the face and joint limits is an error", "[creator]")
{
    const MeshReport faces = checkMesh("chair.dae", collada(9));
    CHECK(faces.maxFacesInAMesh == 9);
    CHECK(hasFinding(faces, MeshFinding::Level::Error, "9 materials"));
    CHECK_FALSE(faces.canOpenInUploader());

    const MeshReport joints = checkMesh("shirt.dae", collada(2, true, 150));
    CHECK(joints.rigged);
    CHECK(joints.joints == 150);
    CHECK(hasFinding(joints, MeshFinding::Level::Error, "150 joints"));

    const MeshReport ok = checkMesh("shirt.dae", collada(2, true, 90));
    CHECK(ok.canOpenInUploader());

    const MeshReport cm = checkMesh("chair.dae", collada(1, false, 0, "0.01"));
    CHECK(hasFinding(cm, MeshFinding::Level::Warning, "0.01 m"));
    CHECK(cm.canOpenInUploader());

    CHECK_FALSE(checkMesh("x.dae", "<html></html>").canOpenInUploader());
    CHECK(hasFinding(checkMesh("x.dae", "<COLLADA></COLLADA>"), MeshFinding::Level::Error, "no mesh"));
}

TEST_CASE("glTF is checked from its JSON", "[creator]")
{
    const MeshReport r = checkMesh("table.gltf", gltf(2));
    CHECK(r.meshes == 1);
    CHECK(r.maxFacesInAMesh == 2);
    CHECK(r.triangles == 24);
    CHECK(r.vertices == 48);
    CHECK(r.sizeMetres[0] == 2.0);
    CHECK(r.canOpenInUploader());

    CHECK(hasFinding(checkMesh("t.gltf", gltf(9)), MeshFinding::Level::Error, "9 materials"));
    CHECK(hasFinding(checkMesh("t.gltf", gltf(1, "", 80.0)), MeshFinding::Level::Warning, "64 m"));
    const MeshReport draco =
        checkMesh("t.gltf", gltf(1, ",\"extensionsRequired\":[\"KHR_draco_mesh_compression\"]"));
    CHECK(hasFinding(draco, MeshFinding::Level::Error, "KHR_draco_mesh_compression"));
    CHECK_FALSE(draco.canOpenInUploader());
    const MeshReport skin = checkMesh("t.gltf", gltf(1, ",\"skins\":[{\"joints\":[1,2,3]}]"));
    CHECK(skin.rigged);
    CHECK(skin.joints == 3);
    CHECK(hasFinding(checkMesh("t.gltf", gltf(1, ",\"buffers\":[{\"uri\":\"t.bin\"}]")), MeshFinding::Level::Info, "t.bin"));
    CHECK_FALSE(checkMesh("t.gltf", "{nope").canOpenInUploader());
    CHECK(hasFinding(checkMesh("t.gltf", "{\"asset\":{\"version\":\"1.0\"},\"meshes\":[{}]}"), MeshFinding::Level::Error, "glTF 1.0"));
}

TEST_CASE("Binary glTF reads its JSON chunk", "[creator]")
{
    std::string json = gltf(1);
    while (json.size() % 4) json += ' ';
    auto le = [](std::uint32_t v) {
        std::string s(4, '\0');
        for (int i = 0; i < 4; ++i) s[static_cast<std::size_t>(i)] = static_cast<char>((v >> (8 * i)) & 0xFF);
        return s;
    };
    const std::string glb = "glTF" + le(2) + le(static_cast<std::uint32_t>(20 + json.size())) +
                            le(static_cast<std::uint32_t>(json.size())) + "JSON" + json;
    const MeshReport r = checkMesh("t.glb", glb);
    CHECK(r.format == MeshFormat::Glb);
    CHECK(r.meshes == 1);
    CHECK(r.canOpenInUploader());

    CHECK_FALSE(checkMesh("t.glb", "glTF" + le(2) + le(100) + le(9999) + "JSON{}").canOpenInUploader());
    CHECK_FALSE(checkMesh("t.glb", "not a glb file at all").canOpenInUploader());
}
