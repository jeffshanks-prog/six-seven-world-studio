/**
 * @file sw67floatercreatorprojects.cpp
 * @brief Creator Project Manager floater.
 *
 * Copyright (c) 2026, 6|7 World Studio contributors.
 *
 * The source code in this file is provided to you under the terms of the
 * GNU Lesser General Public License, version 2.1, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE. Terms of the LGPL can be found in doc/LGPL-license.txt
 * in this distribution, or online at http://www.gnu.org/licenses/lgpl-2.1.txt
 */

#include "llviewerprecompiledheaders.h"

#include "sw67floatercreatorprojects.h"

#include "sw67accessibility.h"
#include "sw67viewerfiles.h"

#include "lldate.h"
#include "llformat.h"
#include "lldirpicker.h"
#include "llscrolllistctrl.h"
#include "lltextbox.h"
#include "lluictrl.h"

#include <algorithm>
#include <filesystem>
#include <set>
#include <system_error>

using namespace sw67::creator;
using sw67::a11y::StatusRole;

namespace
{
constexpr size_t kMaxScanFiles = 2000;
constexpr int kMaxScanDepth = 6;

std::string today()
{
    return LLDate::now().asString().substr(0, 10);
}

std::string issueText(const std::vector<ProjectIssue>& issues)
{
    std::string out;
    for (const ProjectIssue& i : issues)
    {
        const char* tag = i.level == ProjectIssue::Level::Error ? "[Problem] "
                          : i.level == ProjectIssue::Level::Warning ? "[Check] " : "[To do] ";
        out += std::string(tag) + i.text + "\n";
    }
    return out;
}
} // namespace

SW67FloaterCreatorProjects::SW67FloaterCreatorProjects(const LLSD& key)
:   LLFloater(key)
{
}

bool SW67FloaterCreatorProjects::postBuild()
{
    getChild<LLUICtrl>("browse_btn")->setCommitCallback([this](LLUICtrl*, const LLSD&) { onBrowse(); });
    getChild<LLUICtrl>("open_btn")->setCommitCallback(
        [this](LLUICtrl*, const LLSD&) { openFolder(getChild<LLUICtrl>("folder_path")->getValue().asString()); });
    getChild<LLUICtrl>("scan_btn")->setCommitCallback([this](LLUICtrl*, const LLSD&) { onScan(); });
    getChild<LLUICtrl>("remove_asset_btn")->setCommitCallback([this](LLUICtrl*, const LLSD&) { onRemoveAsset(); });
    getChild<LLUICtrl>("toggle_done_btn")->setCommitCallback([this](LLUICtrl*, const LLSD&) { onToggleChecklist(); });
    getChild<LLUICtrl>("add_revision_btn")->setCommitCallback([this](LLUICtrl*, const LLSD&) { onAddRevision(); });
    getChild<LLUICtrl>("save_btn")->setCommitCallback([this](LLUICtrl*, const LLSD&) { onSave(); });
    getChild<LLUICtrl>("project_name")->setCommitCallback([this](LLUICtrl* c, const LLSD&) {
        if (mProject) { mProject->name = c->getValue().asString(); markDirty(); }
    });
    getChild<LLUICtrl>("testing_notes")->setCommitCallback([this](LLUICtrl* c, const LLSD&) {
        if (mProject) { mProject->testingNotes = c->getValue().asString(); markDirty(); }
    });
    refresh();
    return LLFloater::postBuild();
}

void SW67FloaterCreatorProjects::onBrowse()
{
    const LLHandle<LLFloater> handle = getHandle();
    (new LLDirPickerThread(
         [handle](const std::vector<std::string>& folders, std::string) {
             auto* self = static_cast<SW67FloaterCreatorProjects*>(handle.get());
             if (!self || folders.empty())
             {
                 return;
             }
             self->getChild<LLUICtrl>("folder_path")->setValue(LLSD(folders.front()));
             self->openFolder(folders.front());
         },
         mFolder))
        ->getFile();
}

void SW67FloaterCreatorProjects::openFolder(const std::string& folder)
{
    if (folder.empty())
    {
        showStatus(StatusRole::Warning, "Choose a folder first.");
        return;
    }
    if (!sw67viewer::ensureDir(folder))
    {
        showStatus(StatusRole::Error, "Could not open or create " + folder + ".");
        return;
    }
    mFolder = folder;
    const std::string manifest = sw67viewer::joinPath(folder, std::string(kManifestName));
    if (const auto text = sw67viewer::readFile(manifest))
    {
        LoadResult r = readManifest(*text);
        if (!r.project)
        {
            mProject.reset();
            showStatus(StatusRole::Error, "Could not read the project:\n" + issueText(r.issues));
            refresh();
            return;
        }
        mProject = std::move(r.project);
        mDirty = false;
        showStatus(r.issues.empty() ? StatusRole::Success : StatusRole::Warning,
                   "Opened \"" + mProject->name + "\"." + (r.issues.empty() ? "" : "\n" + issueText(r.issues)));
    }
    else
    {
        const std::string name = fsyspath(std::filesystem::path(fsyspath(folder)).filename()).string();
        mProject = newProject(name.empty() ? std::string("New project") : name);
        mDirty = true;
        showStatus(StatusRole::Info, "Started a new project here. Scan the folder to add its files, then Save.");
    }
    refresh();
}

bool SW67FloaterCreatorProjects::fileExists(const std::string& relative) const
{
    if (mFolder.empty() || !isSafeRelativePath(relative))
    {
        return false;
    }
    std::error_code ec;
    return std::filesystem::is_regular_file(fsyspath(sw67viewer::joinPath(mFolder, relative)), ec);
}

void SW67FloaterCreatorProjects::onScan()
{
    if (!mProject)
    {
        return;
    }
    std::set<std::string> listed;
    for (const ProjectAsset& a : mProject->assets)
    {
        listed.insert(a.path);
    }
    const std::filesystem::path root = fsyspath(mFolder);
    std::error_code ec;
    size_t seen = 0;
    int added = 0;
    for (std::filesystem::recursive_directory_iterator it(root, std::filesystem::directory_options::skip_permission_denied, ec), end;
         !ec && it != end && seen < kMaxScanFiles; it.increment(ec))
    {
        const std::string leaf = fsyspath(it->path().filename()).string();
        if (!leaf.empty() && leaf[0] == '.')
        {
            if (it->is_directory(ec)) it.disable_recursion_pending();
            continue;
        }
        if (it.depth() >= kMaxScanDepth)
        {
            it.disable_recursion_pending();
        }
        if (!it->is_regular_file(ec))
        {
            continue;
        }
        ++seen;
        std::string rel = fsyspath(std::filesystem::relative(it->path(), root, ec)).string();
        std::replace(rel.begin(), rel.end(), '\\', '/');
        if (ec || rel == kManifestName || (rel.size() > 4 && rel.compare(rel.size() - 4, 4, ".tmp") == 0) ||
            !isSafeRelativePath(rel) || listed.count(rel))
        {
            continue;
        }
        mProject->assets.push_back({guessAssetKind(rel), rel, "", std::nullopt});
        listed.insert(rel);
        ++added;
    }
    if (added > 0)
    {
        markDirty();
    }
    showStatus(StatusRole::Info, llformat("Added %d file(s) found in the folder%s.", added,
                                          seen >= kMaxScanFiles ? " (stopped after 2000 files)" : ""));
    refresh();
}

void SW67FloaterCreatorProjects::onRemoveAsset()
{
    LLScrollListItem* item = getChild<LLScrollListCtrl>("asset_list")->getFirstSelected();
    if (!mProject || !item)
    {
        return;
    }
    const LLSD::Integer i = item->getValue().asInteger();
    if (i < 0 || static_cast<size_t>(i) >= mProject->assets.size())
    {
        return;
    }
    // Removes the entry from the project only. The file stays on disk.
    const std::string what = mProject->assets[static_cast<size_t>(i)].path;
    mProject->assets.erase(mProject->assets.begin() + i);
    markDirty();
    showStatus(StatusRole::Info, "Removed " + (what.empty() ? std::string("the link") : what) +
                                     " from the project. No file was deleted.");
    refresh();
}

void SW67FloaterCreatorProjects::onToggleChecklist()
{
    LLScrollListItem* item = getChild<LLScrollListCtrl>("checklist")->getFirstSelected();
    if (!mProject || !item)
    {
        return;
    }
    const LLSD::Integer i = item->getValue().asInteger();
    if (i < 0 || static_cast<size_t>(i) >= mProject->releaseChecklist.size())
    {
        return;
    }
    ChecklistItem& c = mProject->releaseChecklist[static_cast<size_t>(i)];
    c.done = !c.done;
    markDirty();
    refresh();
    getChild<LLScrollListCtrl>("checklist")->selectNthItem(static_cast<S32>(i));
}

void SW67FloaterCreatorProjects::onAddRevision()
{
    if (!mProject)
    {
        return;
    }
    std::string summary = getChild<LLUICtrl>("revision_summary")->getValue().asString();
    LLStringUtil::trim(summary);
    if (summary.empty())
    {
        showStatus(StatusRole::Warning, "Describe the revision first.");
        return;
    }
    const int n = addRevision(*mProject, today(), summary);
    getChild<LLUICtrl>("revision_summary")->setValue(LLSD(""));
    markDirty();
    showStatus(StatusRole::Info, llformat("Added revision %d. Save to keep it.", n));
    refresh();
}

void SW67FloaterCreatorProjects::onSave()
{
    if (!mProject || mFolder.empty())
    {
        return;
    }
    mProject->name = getChild<LLUICtrl>("project_name")->getValue().asString();
    mProject->testingNotes = getChild<LLUICtrl>("testing_notes")->getValue().asString();
    const std::string manifest = sw67viewer::joinPath(mFolder, std::string(kManifestName));
    if (sw67viewer::writeFileAtomic(manifest, writeManifest(*mProject)))
    {
        mDirty = false;
        showStatus(StatusRole::Success, "Saved " + manifest + ".");
    }
    else
    {
        showStatus(StatusRole::Error, "Could not write " + manifest + ".");
    }
    refresh();
}

void SW67FloaterCreatorProjects::markDirty()
{
    mDirty = true;
}

void SW67FloaterCreatorProjects::refresh()
{
    const bool open = mProject.has_value();
    for (const char* name : {"scan_btn", "remove_asset_btn", "toggle_done_btn", "add_revision_btn", "save_btn",
                             "project_name", "testing_notes", "revision_summary"})
    {
        getChild<LLUICtrl>(name)->setEnabled(open);
    }
    LLScrollListCtrl* assets = getChild<LLScrollListCtrl>("asset_list");
    LLScrollListCtrl* checklist = getChild<LLScrollListCtrl>("checklist");
    assets->deleteAllItems();
    checklist->deleteAllItems();
    if (!open)
    {
        getChild<LLUICtrl>("readiness_text")->setValue(LLSD("No project open. Choose a folder: a new one starts a project, "
                                                            "an existing one opens it."));
        return;
    }
    getChild<LLUICtrl>("project_name")->setValue(LLSD(mProject->name));
    getChild<LLUICtrl>("testing_notes")->setValue(LLSD(mProject->testingNotes));
    for (size_t i = 0; i < mProject->assets.size(); ++i)
    {
        const ProjectAsset& a = mProject->assets[i];
        LLSD row;
        row["value"] = static_cast<LLSD::Integer>(i);
        row["columns"][0]["column"] = "kind";
        row["columns"][0]["value"] = std::string(assetKindName(a.kind));
        row["columns"][1]["column"] = "item";
        row["columns"][1]["value"] = (a.path.empty() && a.inventory) ? a.inventory->name : a.path;
        row["columns"][2]["column"] = "holds";
        row["columns"][2]["value"] = describeHolding(a, fileExists(a.path));
        assets->addElement(row);
    }
    for (size_t i = 0; i < mProject->releaseChecklist.size(); ++i)
    {
        const ChecklistItem& c = mProject->releaseChecklist[i];
        LLSD row;
        row["value"] = static_cast<LLSD::Integer>(i);
        row["columns"][0]["column"] = "done";
        row["columns"][0]["value"] = c.done ? "Done" : "To do";
        row["columns"][1]["column"] = "text";
        row["columns"][1]["value"] = c.text;
        checklist->addElement(row);
    }
    std::string text;
    if (!mProject->revisions.empty())
    {
        const Revision& r = mProject->revisions.back();
        text += llformat("Latest revision %d, %s: ", r.number, r.date.c_str()) + r.summary + "\n";
    }
    const auto issues = releaseReadiness(*mProject, [this](const std::string& p) { return fileExists(p); });
    text += issues.empty() ? std::string("Nothing outstanding for release.\n") : issueText(issues);
    text += "\nInventory links are references only: the project keeps no copy of any Second Life item.";
    if (mDirty)
    {
        text = "Unsaved changes.\n" + text;
    }
    getChild<LLUICtrl>("readiness_text")->setValue(LLSD(text));
}

void SW67FloaterCreatorProjects::showStatus(StatusRole role, const std::string& text)
{
    LLTextBox* box = getChild<LLTextBox>("status_text");
    box->setText(sw67viewer::statusText(role) + "  " + text);
    box->setColor(LLUIColor(sw67viewer::statusColor(role)));
}
