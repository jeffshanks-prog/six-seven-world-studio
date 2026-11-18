/**
 * @file mesh_check.h
 * @brief Creator Workbench: checks a mesh file against Second Life's upload
 *        limits before the viewer's own uploader opens it, so problems show
 *        up before an upload fee is quoted.
 *
 *        Reads Collada (.dae) and glTF (.gltf, .glb). It reads the file's
 *        structure only: it does not build the model, so the uploader's own
 *        checks still decide. A clean result means "nothing here is known to
 *        fail", not "this will upload".
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

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace sw67::creator
{

/// Second Life's limits, as the viewer's own code states them.
inline constexpr int kMaxFacesPerMesh = 8;            ///< LL_SCULPT_MESH_MAX_FACES
inline constexpr int kMaxJointsPerMesh = 110;         ///< LL_MAX_JOINTS_PER_MESH_OBJECT
inline constexpr double kMaxObjectSize = 64.0;        ///< Metres on a side, on most regions.
inline constexpr long long kVerticesPerFaceSplit = 65535; ///< The uploader splits a face past this.
inline constexpr std::size_t kMaxCheckBytes = 512u * 1024u * 1024u;

enum class MeshFormat
{
    Unknown,
    Collada,
    Gltf,
    Glb,
};

MeshFormat formatFor(std::string_view fileName);

struct MeshFinding
{
    enum class Level
    {
        Error,    ///< Known to fail upload, or to upload wrongly.
        Warning,  ///< Will upload, but probably not as intended.
        Info,
    };
    Level level = Level::Info;
    std::string text;
};

struct MeshReport
{
    MeshFormat format = MeshFormat::Unknown;
    int meshes = 0;
    int maxFacesInAMesh = 0;   ///< Materials (faces) in the mesh with the most.
    long long triangles = 0;   ///< Where the file states them.
    long long vertices = 0;
    bool rigged = false;
    int joints = 0;            ///< Largest joint count of any skin.
    double sizeMetres[3] = {0.0, 0.0, 0.0}; ///< Bounds, where the file states them (glTF).
    std::vector<MeshFinding> findings;

    bool canOpenInUploader() const; ///< No errors.
    std::string describe() const;
};

/// Checks a mesh file's bytes. `fileName` decides the format.
MeshReport checkMesh(std::string_view fileName, std::string_view bytes);

} // namespace sw67::creator
