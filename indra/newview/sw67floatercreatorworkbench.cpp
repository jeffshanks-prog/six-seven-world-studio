/**
 * @file sw67floatercreatorworkbench.cpp
 * @brief Creator Workbench floater.
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

#include "sw67floatercreatorworkbench.h"

#include "sw67accessibility.h"
#include "sw67kingjahmir.h"
#include "sw67viewerfiles.h"

#include "llbutton.h"
#include "llclipboard.h"
#include "llfilepicker.h"
#include "llfloatermodelpreview.h"
#include "llfloaterreg.h"
#include "llmodel.h"
#include "lltextbox.h"
#include "lluictrl.h"
#include "llviewermenufile.h"

using namespace sw67::creator;
using sw67::a11y::StatusRole;

namespace
{
constexpr size_t kMaxMeshReadBytes = 256u * 1024u * 1024u;

std::string fileNameOf(const std::string& path)
{
    const size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}
} // namespace

SW67FloaterCreatorWorkbench::SW67FloaterCreatorWorkbench(const LLSD& key)
:   LLFloater(key)
{
}

bool SW67FloaterCreatorWorkbench::postBuild()
{
    getChild<LLUICtrl>("pick_mesh_btn")->setCommitCallback([this](LLUICtrl*, const LLSD&) { onPickMesh(); });
    getChild<LLUICtrl>("recheck_btn")->setCommitCallback([this](LLUICtrl*, const LLSD&) {
        if (!mMeshPath.empty()) checkFile(mMeshPath);
    });
    getChild<LLUICtrl>("open_uploader_btn")->setCommitCallback([this](LLUICtrl*, const LLSD&) { onOpenUploader(); });
    getChild<LLUICtrl>("draft_btn")->setCommitCallback([this](LLUICtrl*, const LLSD&) { onDraft(); });
    getChild<LLUICtrl>("reviewed_check")->setCommitCallback([this](LLUICtrl*, const LLSD&) { onReviewed(); });
    getChild<LLUICtrl>("copy_btn")->setCommitCallback([this](LLUICtrl*, const LLSD&) { onCopy(); });
    getChild<LLUICtrl>("save_lsl_btn")->setCommitCallback([this](LLUICtrl*, const LLSD&) { onSaveLsl(); });
    getChild<LLUICtrl>("script_studio_btn")->setCommitCallback([](LLUICtrl*, const LLSD&) {
        LLFloaterReg::showInstance("script_studio");
    });
    getChild<LLUICtrl>("mesh_file")->setEnabled(false);
    return LLFloater::postBuild();
}

void SW67FloaterCreatorWorkbench::onOpen(const LLSD& key)
{
    LLFloater::onOpen(key);
    refreshButtons();
    if (sw67viewer::kingjahmir::gatewayUrl().empty() || !sw67viewer::kingjahmir::signedIn())
    {
        getChild<LLUICtrl>("draft_notes")
            ->setValue(LLSD("AI drafting needs the 6|7 gateway and a 6|7 sign-in (King Jahmir's Settings tab). "
                            "You can still paste or write a script here and save it."));
    }
}

void SW67FloaterCreatorWorkbench::onPickMesh()
{
    const LLHandle<LLFloater> handle = getHandle();
    LLFilePickerReplyThread::startPicker(
        [handle](const std::vector<std::string>& files, LLFilePicker::ELoadFilter, LLFilePicker::ESaveFilter) {
            auto* self = ALViewType::as<SW67FloaterCreatorWorkbench>(handle.get());
            if (!self || files.empty()) return;
            self->checkFile(files.front());
        },
        LLFilePicker::FFLOAD_MODEL, false);
}

void SW67FloaterCreatorWorkbench::checkFile(const std::string& path)
{
    mMeshPath = path;
    mMeshOk = false;
    getChild<LLUICtrl>("mesh_file")->setValue(LLSD(path));
    const auto bytes = sw67viewer::readFile(path, kMaxMeshReadBytes);
    if (!bytes)
    {
        getChild<LLUICtrl>("mesh_report")->setValue(LLSD(std::string()));
        showStatus(StatusRole::Error, "Could not read " + fileNameOf(path) + ", or it is over 256 MB.");
        refreshButtons();
        return;
    }
    const MeshReport report = checkMesh(fileNameOf(path), *bytes);
    mMeshOk = report.canOpenInUploader();
    getChild<LLUICtrl>("mesh_report")->setValue(LLSD(report.describe()));
    showStatus(mMeshOk ? StatusRole::Success : StatusRole::Error,
               mMeshOk ? "Checked " + fileNameOf(path) + ": no known problems. Open it in the uploader to see the fee."
                       : "Checked " + fileNameOf(path) + ": fix the errors listed before uploading.");
    refreshButtons();
}

void SW67FloaterCreatorWorkbench::onOpenUploader()
{
    if (!mMeshOk || mMeshPath.empty()) return;
    // Second Life's own uploader: level of detail, physics, fee quote. Nothing
    // is uploaded or charged until the creator presses Upload there.
    if (LLFloaterModelPreview* fmp = LLFloaterReg::showTypedInstance<LLFloaterModelPreview>("upload_model"))
    {
        if (fmp->isModelLoading())
        {
            showStatus(StatusRole::Warning, "The uploader is still loading another model. Try again when it finishes.");
            return;
        }
        fmp->loadModel(LLModel::LOD_HIGH, mMeshPath);
        showStatus(StatusRole::Info, "Opened in the uploader. Check the preview and fee there; nothing is uploaded "
                                     "until you choose Upload.");
    }
    else
    {
        showStatus(StatusRole::Error, "The model uploader could not open. Mesh upload may not be available here.");
    }
}

void SW67FloaterCreatorWorkbench::onDraft()
{
    const std::string request = getChild<LLUICtrl>("lsl_request")->getValue().asString();
    if (request.find_first_not_of(" \t\r\n") == std::string::npos)
    {
        showStatus(StatusRole::Info, "Describe what the script should do first.");
        return;
    }
    const std::string existing = getChild<LLUICtrl>("use_draft_check")->getValue().asBoolean()
                                     ? getChild<LLUICtrl>("lsl_draft")->getValue().asString()
                                     : std::string();
    const LLHandle<LLFloater> handle = getHandle();
    std::string whyNot;
    const bool sent = sw67viewer::kingjahmir::draftLsl(
        request, existing,
        [handle](const sw67viewer::kingjahmir::LslDraft& d) {
            auto* self = ALViewType::as<SW67FloaterCreatorWorkbench>(handle.get());
            if (!self) return;
            self->mDrafting = false;
            if (!d.ok)
            {
                self->showStatus(StatusRole::Warning, d.error);
            }
            else if (d.code.empty())
            {
                self->getChild<LLUICtrl>("draft_notes")->setValue(LLSD(d.notes));
                self->showStatus(StatusRole::Info, "No script came back; the notes say why.");
            }
            else
            {
                self->getChild<LLUICtrl>("lsl_draft")->setValue(LLSD(d.code));
                self->getChild<LLUICtrl>("draft_notes")->setValue(LLSD(d.draftNote + (d.notes.empty() ? "" : "\n" + d.notes)));
                self->getChild<LLUICtrl>("reviewed_check")->setValue(LLSD(false));
                self->mHaveDraft = true;
                self->showStatus(StatusRole::Warning, "Draft ready. Not compiled or tested: read it, then tick the "
                                                      "review box to copy or save it.");
            }
            self->refreshButtons();
        },
        whyNot);
    if (!sent)
    {
        showStatus(StatusRole::Info, whyNot);
        return;
    }
    mDrafting = true;
    showStatus(StatusRole::Info, "Drafting...");
    refreshButtons();
}

void SW67FloaterCreatorWorkbench::onReviewed()
{
    refreshButtons();
}

void SW67FloaterCreatorWorkbench::onCopy()
{
    if (!getChild<LLUICtrl>("reviewed_check")->getValue().asBoolean()) return;
    const std::string code = getChild<LLUICtrl>("lsl_draft")->getValue().asString();
    LLClipboard::instance().copyToClipboard(code, 0, static_cast<S32>(code.size()));
    showStatus(StatusRole::Success, "Copied. Paste it into a script in Script Studio or the script editor, compile "
                                    "it, and test it on a test object.");
}

void SW67FloaterCreatorWorkbench::onSaveLsl()
{
    if (!getChild<LLUICtrl>("reviewed_check")->getValue().asBoolean()) return;
    const std::string code = getChild<LLUICtrl>("lsl_draft")->getValue().asString();
    const LLHandle<LLFloater> handle = getHandle();
    LLFilePickerReplyThread::startPicker(
        [handle, code](const std::vector<std::string>& files, LLFilePicker::ELoadFilter, LLFilePicker::ESaveFilter) {
            auto* self = ALViewType::as<SW67FloaterCreatorWorkbench>(handle.get());
            if (!self || files.empty()) return;
            if (!sw67viewer::writeFileAtomic(files.front(), code))
            {
                self->showStatus(StatusRole::Error, "Could not write " + files.front() + ".");
                return;
            }
            self->showStatus(StatusRole::Success, "Saved " + files.front() +
                                                      ". It has not been compiled: open it in Script Studio to compile and test it.");
        },
        LLFilePicker::FFSAVE_SCRIPT, "draft.lsl");
}

void SW67FloaterCreatorWorkbench::refreshButtons()
{
    getChild<LLUICtrl>("recheck_btn")->setEnabled(!mMeshPath.empty());
    getChild<LLUICtrl>("open_uploader_btn")->setEnabled(mMeshOk);
    getChild<LLUICtrl>("draft_btn")->setEnabled(!mDrafting);
    const bool hasText = !getChild<LLUICtrl>("lsl_draft")->getValue().asString().empty();
    const bool reviewed = getChild<LLUICtrl>("reviewed_check")->getValue().asBoolean();
    getChild<LLUICtrl>("copy_btn")->setEnabled(hasText && reviewed);
    getChild<LLUICtrl>("save_lsl_btn")->setEnabled(hasText && reviewed);
}

void SW67FloaterCreatorWorkbench::showStatus(StatusRole role, const std::string& text)
{
    LLTextBox* box = getChild<LLTextBox>("status_text");
    box->setText(sw67viewer::statusText(role) + "  " + text);
    box->setColor(LLUIColor(sw67viewer::statusColor(role)));
}
