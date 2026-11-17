/**
 * @file project.h
 * @brief Creator projects: one folder on the user's disk that keeps the
 *        mesh and texture sources, scripts, reference images, export
 *        settings, revision history, testing notes and release checklist of
 *        one product together, in a plain JSON manifest
 *        (project.sw67.json) beside the files.
 *
 *        Projects are the user's own files. Opening, reading and exporting
 *        them needs no subscription, so they stay usable after one ends.
 *        Second Life inventory is only ever linked, by item ID, with the
 *        permissions the viewer saw; a project never holds a copy of an
 *        inventory asset and never says it does.
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

namespace sw67::creator
{

inline constexpr std::string_view kManifestName = "project.sw67.json";
inline constexpr std::string_view kFormat = "sw67.creator.project";
inline constexpr int kFormatVersion = 1;

enum class AssetKind
{
    MeshSource,    ///< .blend, .fbx, .dae, .gltf...
    TextureSource, ///< .psd, .kra, .png, .tga...
    Script,        ///< .lsl, .luau
    Reference,     ///< Reference images and documents.
    Export,        ///< Files made for upload: .dae, .png...
    Other,
};

std::string_view assetKindKey(AssetKind k);
std::string_view assetKindName(AssetKind k);
std::optional<AssetKind> parseAssetKind(std::string_view key);
/// A guess from the file's extension, for files dropped into a project.
AssetKind guessAssetKind(std::string_view path);

/// Second Life permissions on an inventory item, as the viewer saw them for
/// the user when the link was made.
struct ItemPermissions
{
    bool copy = false;
    bool modify = false;
    bool transfer = false;
    bool userIsCreator = false;
    friend bool operator==(const ItemPermissions&, const ItemPermissions&) = default;
};

/// A link to an inventory item. It is a reference only.
struct InventoryLink
{
    std::string itemId; ///< The item's UUID, lower case.
    std::string name;   ///< The item's name when linked, for display.
    std::string assetType; ///< "object", "texture", "script"... as the viewer names it.
    ItemPermissions permissions;
    friend bool operator==(const InventoryLink&, const InventoryLink&) = default;
};

struct ProjectAsset
{
    AssetKind kind = AssetKind::Other;
    std::string path; ///< Relative to the project folder, '/' separated. Empty for a link only.
    std::string note;
    std::optional<InventoryLink> inventory;
    friend bool operator==(const ProjectAsset&, const ProjectAsset&) = default;
};

struct Revision
{
    int number = 0;
    std::string date; ///< YYYY-MM-DD.
    std::string summary;
    friend bool operator==(const Revision&, const Revision&) = default;
};

struct ChecklistItem
{
    std::string text;
    bool done = false;
    friend bool operator==(const ChecklistItem&, const ChecklistItem&) = default;
};

struct CreatorProject
{
    std::string name;
    std::string description;
    std::vector<ProjectAsset> assets;
    std::map<std::string, std::string> exportSettings; ///< Free-form, e.g. "lod.high": "8000 tris".
    std::vector<Revision> revisions;
    std::string testingNotes;
    std::vector<ChecklistItem> releaseChecklist;
    friend bool operator==(const CreatorProject&, const CreatorProject&) = default;
};

/// A new project with the default release checklist.
CreatorProject newProject(std::string name);
std::vector<ChecklistItem> defaultReleaseChecklist();

/// Whether `path` is a safe project-relative path: not absolute, no drive,
/// no "..", no backslashes, no empty or "." segments, no control characters.
bool isSafeRelativePath(std::string_view path);

/// A valid lower-case UUID string (8-4-4-4-12 hex).
bool isUuid(std::string_view text);

/// Adds the next revision. Returns its number.
int addRevision(CreatorProject& project, std::string date, std::string summary);

/// What the project holds for an asset, said plainly.
enum class Holding
{
    LocalFile,        ///< A file in the project folder.
    LocalFileMissing, ///< The project lists a file that is not there.
    LinkOnly,         ///< An inventory link; no copy of the asset is kept.
};
Holding holdingOf(const ProjectAsset& asset, bool localFileExists);
std::string describeHolding(const ProjectAsset& asset, bool localFileExists);

/// Whether the user could re-upload or give the linked item away, from the
/// permissions recorded. Informational: the server decides.
std::string describePermissions(const ItemPermissions& p);

struct ProjectIssue
{
    enum class Level
    {
        Info,
        Warning,
        Error
    };
    Level level = Level::Info;
    std::string text;
};

/// What stands between the project and a release: unticked checklist items,
/// missing files, scripts with no testing notes, no revisions, and linked
/// items whose permissions would stop the user selling them as recorded.
/// `fileExists` answers for project-relative paths.
template <typename FileExists>
std::vector<ProjectIssue> releaseReadiness(const CreatorProject& project, FileExists fileExists);

struct LoadResult
{
    std::optional<CreatorProject> project;
    std::vector<ProjectIssue> issues;
};

std::string writeManifest(const CreatorProject& project);
/// Reads a manifest. Assets with unsafe paths or malformed links are dropped
/// with an issue; a newer format version is refused.
LoadResult readManifest(std::string_view text);

// -- implementation of the template ---------------------------------------

std::vector<ProjectIssue> releaseReadinessImpl(const CreatorProject& project, const std::vector<bool>& exists);

template <typename FileExists>
std::vector<ProjectIssue> releaseReadiness(const CreatorProject& project, FileExists fileExists)
{
    std::vector<bool> exists;
    exists.reserve(project.assets.size());
    for (const ProjectAsset& a : project.assets)
    {
        exists.push_back(!a.path.empty() && fileExists(a.path));
    }
    return releaseReadinessImpl(project, exists);
}

} // namespace sw67::creator
