/**
 * @file sw67floaterworkspaces.cpp
 * @brief Workspace Profiles floater.
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

#include "sw67floaterworkspaces.h"

#include "sw67accessibility.h"
#include "sw67viewerfiles.h"
#include "sw67workspaces.h"

#include "sw67/photo/product.h"

#include "llcombobox.h"
#include "llfilepicker.h"
#include "lltextbox.h"
#include "lluictrl.h"
#include "llviewermenufile.h"

using namespace sw67::workspace;
using sw67::a11y::StatusRole;

namespace
{
std::string notesText(const std::vector<Note>& notes)
{
    std::string out;
    for (const Note& n : notes)
    {
        out += "  - " + n.text + "\n";
    }
    return out;
}
} // namespace

SW67FloaterWorkspaces::SW67FloaterWorkspaces(const LLSD& key)
:   LLFloater(key)
{
}

bool SW67FloaterWorkspaces::postBuild()
{
    getChild<LLUICtrl>("profile_combo")->setCommitCallback([this](LLUICtrl*, const LLSD&) { refreshPreview(); });
    getChild<LLUICtrl>("apply_btn")->setCommitCallback([this](LLUICtrl*, const LLSD&) { onApply(); });
    getChild<LLUICtrl>("undo_btn")->setCommitCallback([this](LLUICtrl*, const LLSD&) { onUndo(); });
    getChild<LLUICtrl>("save_btn")->setCommitCallback([this](LLUICtrl*, const LLSD&) { onSaveCurrent(); });
    getChild<LLUICtrl>("delete_btn")->setCommitCallback([this](LLUICtrl*, const LLSD&) { onDelete(); });
    getChild<LLUICtrl>("export_btn")->setCommitCallback([this](LLUICtrl*, const LLSD&) { onExport(); });
    getChild<LLUICtrl>("import_btn")->setCommitCallback([this](LLUICtrl*, const LLSD&) { onImport(); });
    getChild<LLUICtrl>("undo_btn")->setEnabled(false);
    refreshList();
    return LLFloater::postBuild();
}

void SW67FloaterWorkspaces::onOpen(const LLSD& key)
{
    LLFloater::onOpen(key);
    refreshPreview();
}

void SW67FloaterWorkspaces::refreshList()
{
    mProfiles = builtinProfiles();
    mBuiltinCount = mProfiles.size();
    for (WorkspaceProfile& p : sw67viewer::savedWorkspaces())
    {
        mProfiles.push_back(std::move(p));
    }
    LLComboBox* combo = getChild<LLComboBox>("profile_combo");
    const LLSD was = combo->getValue();
    combo->clearRows();
    for (size_t i = 0; i < mProfiles.size(); ++i)
    {
        const std::string label = i < mBuiltinCount ? mProfiles[i].name + " (built in)" : mProfiles[i].name;
        combo->add(label, LLSD(static_cast<LLSD::Integer>(i)));
    }
    if (!was.isUndefined() && was.asInteger() >= 0 && static_cast<size_t>(was.asInteger()) < mProfiles.size())
    {
        combo->setValue(was);
    }
    else
    {
        combo->setValue(LLSD(0));
    }
    refreshPreview();
}

const WorkspaceProfile* SW67FloaterWorkspaces::selected() const
{
    const LLSD v = getChild<LLComboBox>("profile_combo")->getValue();
    if (v.isUndefined())
    {
        return nullptr;
    }
    const LLSD::Integer i = v.asInteger();
    return (i >= 0 && static_cast<size_t>(i) < mProfiles.size()) ? &mProfiles[static_cast<size_t>(i)] : nullptr;
}

void SW67FloaterWorkspaces::refreshPreview()
{
    const WorkspaceProfile* p = selected();
    std::string text;
    if (p)
    {
        text = p->description + "\n\n";
        const std::vector<Change> plan = planApply(*p, sw67viewer::captureWorkspace(*p));
        if (plan.empty())
        {
            text += "Applying this profile changes nothing: the viewer already matches it.\n";
        }
        else
        {
            text += "Applying this profile will:\n";
            for (const Change& c : plan)
            {
                text += "  - " + c.describe() + "\n";
            }
        }
        text += "\nYour accessibility settings stay as they are in every profile.";
        const bool builtin = p >= mProfiles.data() && p < mProfiles.data() + mBuiltinCount;
        getChild<LLUICtrl>("delete_btn")->setEnabled(!builtin);
    }
    getChild<LLUICtrl>("preview_text")->setValue(LLSD(text));
}

void SW67FloaterWorkspaces::onApply()
{
    const WorkspaceProfile* p = selected();
    if (!p)
    {
        return;
    }
    const WorkspaceProfile profile = *p; // The list may be rebuilt below.
    const CurrentState before = sw67viewer::captureWorkspace(profile);
    const std::vector<Change> plan = planApply(profile, before);
    if (plan.empty())
    {
        showStatus(StatusRole::Info, "Nothing to change.");
        return;
    }
    mUndo = restorePoint(profile, before);
    const int failed = sw67viewer::applyWorkspacePlan(plan, profile);
    getChild<LLUICtrl>("undo_btn")->setEnabled(true);
    if (failed == 0)
    {
        showStatus(StatusRole::Success, "Applied \"" + profile.name + "\". Undo puts the previous layout back.");
    }
    else
    {
        showStatus(StatusRole::Warning, "Applied \"" + profile.name + "\" with " + std::to_string(failed) +
                                            " change(s) that could not be made. Undo puts the rest back.");
    }
    refreshPreview();
}

void SW67FloaterWorkspaces::onUndo()
{
    if (!mUndo)
    {
        return;
    }
    const WorkspaceProfile undo = *mUndo;
    const std::vector<Change> plan = planApply(undo, sw67viewer::captureWorkspace(undo));
    const int failed = sw67viewer::applyWorkspacePlan(plan, undo);
    mUndo.reset();
    getChild<LLUICtrl>("undo_btn")->setEnabled(false);
    showStatus(failed ? StatusRole::Warning : StatusRole::Success,
               failed ? "Restored the previous layout, except " + std::to_string(failed) + " change(s)."
                      : std::string("Restored the previous layout."));
    refreshPreview();
}

void SW67FloaterWorkspaces::onSaveCurrent()
{
    std::string name = getChild<LLUICtrl>("save_name")->getValue().asString();
    LLStringUtil::trim(name);
    if (name.empty())
    {
        showStatus(StatusRole::Warning, "Type a name for the profile first.");
        return;
    }
    std::vector<Note> notes;
    const WorkspaceProfile p = sw67viewer::profileFromCurrent(name);
    if (!sw67viewer::saveWorkspace(p, &notes))
    {
        showStatus(StatusRole::Error, "Could not save to " + sw67viewer::workspacesDir() + ".");
        return;
    }
    refreshList();
    showStatus(StatusRole::Success, "Saved \"" + name + "\" with " + std::to_string(p.windows.size()) + " window(s)." +
                                        (notes.empty() ? "" : " Left out:\n" + notesText(notes)));
}

void SW67FloaterWorkspaces::onDelete()
{
    const WorkspaceProfile* p = selected();
    if (!p || (p >= mProfiles.data() && p < mProfiles.data() + mBuiltinCount))
    {
        return;
    }
    const std::string name = p->name;
    // Deleting removes one file the user made; the button sits apart from
    // Apply and names the profile in the result.
    if (sw67viewer::deleteWorkspace(name))
    {
        getChild<LLComboBox>("profile_combo")->setValue(LLSD(0));
        refreshList();
        showStatus(StatusRole::Info, "Deleted the saved profile \"" + name + "\".");
    }
    else
    {
        showStatus(StatusRole::Error, "Could not delete \"" + name + "\".");
    }
}

void SW67FloaterWorkspaces::onExport()
{
    const WorkspaceProfile* p = selected();
    if (!p)
    {
        return;
    }
    const WorkspaceProfile profile = *p;
    const LLHandle<LLFloater> handle = getHandle();
    LLFilePickerReplyThread::startPicker(
        [handle, profile](const std::vector<std::string>& files, LLFilePicker::ELoadFilter, LLFilePicker::ESaveFilter) {
            auto* self = static_cast<SW67FloaterWorkspaces*>(handle.get());
            if (files.empty() || !self)
            {
                return;
            }
            std::vector<Note> notes;
            const std::string text = exportProfile(profile, &notes);
            if (sw67viewer::writeFileAtomic(files.front(), text))
            {
                self->showStatus(StatusRole::Success, "Exported to " + files.front() + "." +
                                                          (notes.empty() ? "" : " Left out:\n" + notesText(notes)));
            }
            else
            {
                self->showStatus(StatusRole::Error, "Could not write " + files.front() + ".");
            }
        },
        LLFilePicker::FFSAVE_ALL, sw67::photo::safeFileStem(profile.name) + ".sw67workspace.json");
}

void SW67FloaterWorkspaces::onImport()
{
    const LLHandle<LLFloater> handle = getHandle();
    LLFilePickerReplyThread::startPicker(
        [handle](const std::vector<std::string>& files, LLFilePicker::ELoadFilter, LLFilePicker::ESaveFilter) {
            auto* self = static_cast<SW67FloaterWorkspaces*>(handle.get());
            if (files.empty() || !self)
            {
                return;
            }
            const auto text = sw67viewer::readFile(files.front(), 2 * 1024 * 1024);
            if (!text)
            {
                self->showStatus(StatusRole::Error, "Could not read that file, or it is over 2 MB.");
                return;
            }
            ImportResult r = importProfile(*text);
            if (!r.profile)
            {
                self->showStatus(StatusRole::Error, "Not imported:\n" + notesText(r.notes));
                return;
            }
            if (!sw67viewer::saveWorkspace(*r.profile))
            {
                self->showStatus(StatusRole::Error, "Could not save the imported profile.");
                return;
            }
            self->refreshList();
            self->showStatus(StatusRole::Success, "Imported \"" + r.profile->name +
                                                      "\". Choose it and check the preview before applying." +
                                                      (r.notes.empty() ? "" : "\n" + notesText(r.notes)));
        },
        LLFilePicker::FFLOAD_ALL, false);
}

void SW67FloaterWorkspaces::showStatus(StatusRole role, const std::string& text)
{
    LLTextBox* box = getChild<LLTextBox>("status_text");
    box->setText(sw67viewer::statusText(role) + "  " + text);
    box->setColor(LLUIColor(sw67viewer::statusColor(role)));
}
