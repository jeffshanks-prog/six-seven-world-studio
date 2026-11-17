/**
 * @file creator_project.cpp
 * @brief Creator project manifests, revisions, holdings and release checks.
 *
 * Copyright (c) 2026, 6|7 World Studio contributors.
 *
 * The source code in this file is provided to you under the terms of the
 * GNU Lesser General Public License, version 2.1, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE. Terms of the LGPL can be found in doc/LGPL-license.txt
 * in this distribution, or online at http://www.gnu.org/licenses/lgpl-2.1.txt
 */

#include "sw67/creator/project.h"

#include "sw67/io/json.h"

#include <algorithm>
#include <cctype>

namespace sw67::creator
{
namespace
{
using io::Json;

std::string lowerExt(std::string_view path)
{
    const auto slash = path.find_last_of('/');
    const auto dot = path.find_last_of('.');
    if (dot == std::string_view::npos || (slash != std::string_view::npos && dot < slash))
    {
        return {};
    }
    std::string ext(path.substr(dot + 1));
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
}

bool validDate(std::string_view d)
{
    if (d.size() != 10 || d[4] != '-' || d[7] != '-')
    {
        return false;
    }
    for (std::size_t i : {0u, 1u, 2u, 3u, 5u, 6u, 8u, 9u})
    {
        if (d[i] < '0' || d[i] > '9')
        {
            return false;
        }
    }
    const int month = (d[5] - '0') * 10 + (d[6] - '0');
    const int day = (d[8] - '0') * 10 + (d[9] - '0');
    return month >= 1 && month <= 12 && day >= 1 && day <= 31;
}

using Level = ProjectIssue::Level;
} // namespace

std::string_view assetKindKey(AssetKind k)
{
    switch (k)
    {
    case AssetKind::MeshSource: return "mesh_source";
    case AssetKind::TextureSource: return "texture_source";
    case AssetKind::Script: return "script";
    case AssetKind::Reference: return "reference";
    case AssetKind::Export: return "export";
    case AssetKind::Other: return "other";
    }
    return "other";
}

std::string_view assetKindName(AssetKind k)
{
    switch (k)
    {
    case AssetKind::MeshSource: return "Mesh source";
    case AssetKind::TextureSource: return "Texture source";
    case AssetKind::Script: return "Script";
    case AssetKind::Reference: return "Reference";
    case AssetKind::Export: return "Export";
    case AssetKind::Other: return "Other";
    }
    return "Other";
}

std::optional<AssetKind> parseAssetKind(std::string_view key)
{
    for (AssetKind k : {AssetKind::MeshSource, AssetKind::TextureSource, AssetKind::Script, AssetKind::Reference,
                        AssetKind::Export, AssetKind::Other})
    {
        if (assetKindKey(k) == key)
        {
            return k;
        }
    }
    return std::nullopt;
}

AssetKind guessAssetKind(std::string_view path)
{
    const std::string ext = lowerExt(path);
    if (ext == "blend" || ext == "fbx" || ext == "obj" || ext == "max" || ext == "ma" || ext == "mb" || ext == "c4d" ||
        ext == "zpr" || ext == "ztl")
        return AssetKind::MeshSource;
    if (ext == "dae" || ext == "gltf" || ext == "glb" || ext == "anim" || ext == "bvh")
        return AssetKind::Export;
    if (ext == "psd" || ext == "kra" || ext == "xcf" || ext == "spp" || ext == "sbsar" || ext == "exr" || ext == "tif" ||
        ext == "tiff")
        return AssetKind::TextureSource;
    if (ext == "lsl" || ext == "luau" || ext == "lua" || ext == "txt")
        return AssetKind::Script;
    if (ext == "png" || ext == "jpg" || ext == "jpeg" || ext == "tga" || ext == "bmp" || ext == "webp")
        return AssetKind::TextureSource;
    if (ext == "pdf" || ext == "md")
        return AssetKind::Reference;
    return AssetKind::Other;
}

std::vector<ChecklistItem> defaultReleaseChecklist()
{
    return {{"Mesh LODs checked at every distance", false},
            {"Physics shape set and tested", false},
            {"Land impact noted in the listing", false},
            {"Textures uploaded at the smallest size that looks right", false},
            {"Scripts tested in-world, including reset and owner change", false},
            {"Next-owner permissions set as intended", false},
            {"Tested on a fresh copy from inventory", false},
            {"Product photos taken", false},
            {"Listing text reviewed: no facts left unchecked", false}};
}

CreatorProject newProject(std::string name)
{
    CreatorProject p;
    p.name = std::move(name);
    p.releaseChecklist = defaultReleaseChecklist();
    return p;
}

bool isSafeRelativePath(std::string_view path)
{
    if (path.empty() || path.size() > 512)
    {
        return false;
    }
    if (path.front() == '/' || path.find('\\') != std::string_view::npos || path.find(':') != std::string_view::npos)
    {
        return false;
    }
    for (const char c : path)
    {
        if (static_cast<unsigned char>(c) < 0x20)
        {
            return false;
        }
    }
    std::size_t start = 0;
    while (start <= path.size())
    {
        const std::size_t end = std::min(path.find('/', start), path.size());
        const std::string_view seg = path.substr(start, end - start);
        if (seg.empty() || seg == "." || seg == "..")
        {
            return false;
        }
        start = end + 1;
    }
    return true;
}

bool isUuid(std::string_view t)
{
    if (t.size() != 36)
    {
        return false;
    }
    for (std::size_t i = 0; i < t.size(); ++i)
    {
        if (i == 8 || i == 13 || i == 18 || i == 23)
        {
            if (t[i] != '-') return false;
        }
        else if (!((t[i] >= '0' && t[i] <= '9') || (t[i] >= 'a' && t[i] <= 'f')))
        {
            return false;
        }
    }
    return true;
}

int addRevision(CreatorProject& project, std::string date, std::string summary)
{
    int next = 1;
    for (const Revision& r : project.revisions)
    {
        next = std::max(next, r.number + 1);
    }
    project.revisions.push_back({next, std::move(date), std::move(summary)});
    return next;
}

Holding holdingOf(const ProjectAsset& asset, bool localFileExists)
{
    if (!asset.path.empty())
    {
        return localFileExists ? Holding::LocalFile : Holding::LocalFileMissing;
    }
    return Holding::LinkOnly;
}

std::string describeHolding(const ProjectAsset& asset, bool localFileExists)
{
    std::string text;
    switch (holdingOf(asset, localFileExists))
    {
    case Holding::LocalFile: text = "File in the project folder."; break;
    case Holding::LocalFileMissing: text = "The file is missing from the project folder."; break;
    case Holding::LinkOnly: text = "Inventory link only."; break;
    }
    if (asset.inventory)
    {
        text += " Linked to inventory item \"" + asset.inventory->name +
                "\". A link is a reference: the project keeps no copy of the item.";
    }
    return text;
}

std::string describePermissions(const ItemPermissions& p)
{
    std::string text = std::string(p.copy ? "Copy" : "No copy") + ", " + (p.modify ? "modify" : "no modify") + ", " +
                       (p.transfer ? "transfer" : "no transfer") + ".";
    text += p.userIsCreator ? " You are the creator." : " You are not the creator.";
    return text;
}

std::vector<ProjectIssue> releaseReadinessImpl(const CreatorProject& project, const std::vector<bool>& exists)
{
    std::vector<ProjectIssue> issues;
    for (std::size_t i = 0; i < project.assets.size(); ++i)
    {
        const ProjectAsset& a = project.assets[i];
        if (!a.path.empty() && !exists[i])
        {
            issues.push_back({Level::Error, "Missing file: " + a.path});
        }
        if (a.inventory && !a.inventory->permissions.userIsCreator)
        {
            issues.push_back({Level::Warning, "\"" + a.inventory->name +
                                                  "\" was made by someone else. Check its licence before including it in a "
                                                  "product."});
        }
        if (a.inventory && a.inventory->permissions.userIsCreator && !a.inventory->permissions.transfer)
        {
            issues.push_back({Level::Warning, "\"" + a.inventory->name + "\" is recorded as no-transfer for you, so it could "
                                                                         "not be sold or given as it is."});
        }
    }
    const bool hasScript = std::any_of(project.assets.begin(), project.assets.end(),
                                       [](const ProjectAsset& a) { return a.kind == AssetKind::Script; });
    if (hasScript && project.testingNotes.empty())
    {
        issues.push_back({Level::Warning, "The project has scripts but no testing notes."});
    }
    if (project.revisions.empty())
    {
        issues.push_back({Level::Info, "No revisions recorded yet."});
    }
    for (const ChecklistItem& c : project.releaseChecklist)
    {
        if (!c.done)
        {
            issues.push_back({Level::Info, "Not done: " + c.text});
        }
    }
    return issues;
}

std::string writeManifest(const CreatorProject& p)
{
    Json doc = Json::object();
    doc.set("format", std::string(kFormat));
    doc.set("version", kFormatVersion);
    doc.set("name", p.name);
    doc.set("description", p.description);
    Json assets = Json::array();
    for (const ProjectAsset& a : p.assets)
    {
        Json o = Json::object();
        o.set("kind", std::string(assetKindKey(a.kind)));
        if (!a.path.empty()) o.set("path", a.path);
        if (!a.note.empty()) o.set("note", a.note);
        if (a.inventory)
        {
            Json link = Json::object();
            link.set("itemId", a.inventory->itemId);
            link.set("name", a.inventory->name);
            link.set("assetType", a.inventory->assetType);
            Json perms = Json::object();
            perms.set("copy", a.inventory->permissions.copy);
            perms.set("modify", a.inventory->permissions.modify);
            perms.set("transfer", a.inventory->permissions.transfer);
            perms.set("userIsCreator", a.inventory->permissions.userIsCreator);
            link.set("permissionsWhenLinked", perms);
            o.set("inventoryLink", link);
        }
        assets.push(o);
    }
    doc.set("assets", assets);
    Json exportSettings = Json::object();
    for (const auto& [k, v] : p.exportSettings)
    {
        exportSettings.set(k, v);
    }
    doc.set("exportSettings", exportSettings);
    Json revisions = Json::array();
    for (const Revision& r : p.revisions)
    {
        Json o = Json::object();
        o.set("number", r.number);
        o.set("date", r.date);
        o.set("summary", r.summary);
        revisions.push(o);
    }
    doc.set("revisions", revisions);
    doc.set("testingNotes", p.testingNotes);
    Json checklist = Json::array();
    for (const ChecklistItem& c : p.releaseChecklist)
    {
        Json o = Json::object();
        o.set("text", c.text);
        o.set("done", c.done);
        checklist.push(o);
    }
    doc.set("releaseChecklist", checklist);
    doc.set("note", "Inventory links are references only. This project holds no copy of any Second Life asset.");
    return io::writeJson(doc);
}

LoadResult readManifest(std::string_view text)
{
    LoadResult result;
    io::JsonError err;
    const auto doc = io::parseJson(text, &err);
    if (!doc || !doc->isObject())
    {
        result.issues.push_back({Level::Error, doc ? std::string("This is not a project manifest.")
                                                   : "The manifest is not valid JSON (" + err.what + ")."});
        return result;
    }
    if ((*doc)["format"].asString() != kFormat)
    {
        result.issues.push_back({Level::Error, "This is not a 6|7 World Studio project manifest."});
        return result;
    }
    const int version = (*doc)["version"].asInt(0);
    if (version < 1 || version > kFormatVersion)
    {
        result.issues.push_back({Level::Error, version > kFormatVersion
                                                   ? "This project was saved by a newer version of 6|7 World Studio. Its "
                                                     "files are untouched and still in the folder; update the viewer to "
                                                     "open the manifest."
                                                   : std::string("The manifest has no valid format version.")});
        return result;
    }

    CreatorProject p;
    p.name = (*doc)["name"].asString("Untitled").substr(0, 120);
    p.description = (*doc)["description"].asString("").substr(0, 4000);
    for (const Json& a : (*doc)["assets"].items())
    {
        ProjectAsset asset;
        asset.kind = parseAssetKind(a["kind"].asString("other")).value_or(AssetKind::Other);
        asset.path = a["path"].asString("");
        asset.note = a["note"].asString("").substr(0, 2000);
        if (!asset.path.empty() && !isSafeRelativePath(asset.path))
        {
            result.issues.push_back({Level::Warning, "Left out an asset whose path leaves the project folder or is not "
                                                     "valid."});
            continue;
        }
        if (a["inventoryLink"].isObject())
        {
            const Json& l = a["inventoryLink"];
            InventoryLink link;
            link.itemId = l["itemId"].asString("");
            link.name = l["name"].asString("").substr(0, 200);
            link.assetType = l["assetType"].asString("").substr(0, 40);
            const Json& perms = l["permissionsWhenLinked"];
            link.permissions = {perms["copy"].asBool(false), perms["modify"].asBool(false),
                                perms["transfer"].asBool(false), perms["userIsCreator"].asBool(false)};
            if (!isUuid(link.itemId))
            {
                result.issues.push_back({Level::Warning, "Dropped an inventory link with an invalid item ID."});
            }
            else
            {
                asset.inventory = std::move(link);
            }
        }
        if (asset.path.empty() && !asset.inventory)
        {
            result.issues.push_back({Level::Warning, "Left out an asset with neither a file nor a link."});
            continue;
        }
        p.assets.push_back(std::move(asset));
    }
    for (const auto& [k, v] : (*doc)["exportSettings"].members())
    {
        if (v.isString() && k.size() <= 80)
        {
            p.exportSettings[k] = v.asString().substr(0, 400);
        }
    }
    for (const Json& r : (*doc)["revisions"].items())
    {
        Revision rev{r["number"].asInt(0), r["date"].asString(""), r["summary"].asString("").substr(0, 2000)};
        if (rev.number < 1 || !validDate(rev.date))
        {
            result.issues.push_back({Level::Warning, "Left out a revision with no valid number or date."});
            continue;
        }
        p.revisions.push_back(std::move(rev));
    }
    std::sort(p.revisions.begin(), p.revisions.end(), [](const Revision& a, const Revision& b) { return a.number < b.number; });
    p.testingNotes = (*doc)["testingNotes"].asString("").substr(0, 20000);
    for (const Json& c : (*doc)["releaseChecklist"].items())
    {
        if (c["text"].isString() && !c["text"].asString().empty())
        {
            p.releaseChecklist.push_back({c["text"].asString().substr(0, 400), c["done"].asBool(false)});
        }
    }
    result.project = std::move(p);
    return result;
}

} // namespace sw67::creator
