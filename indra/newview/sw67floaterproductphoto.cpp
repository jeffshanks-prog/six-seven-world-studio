/**
 * @file sw67floaterproductphoto.cpp
 * @brief Product Photography Assistant floater.
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

#include "sw67floaterproductphoto.h"

#include "sw67accessibility.h"
#include "sw67rlvstate.h"
#include "sw67viewerfiles.h"

#include "sw67/rlv/gate.h"

#include "llagent.h"
#include "llagentcamera.h"
#include "llbbox.h"
#include "lldate.h"
#include "lldirpicker.h"
#include "llformat.h"
#include "llimagejpeg.h"
#include "llimagepng.h"
#include "llscrolllistctrl.h"
#include "llselectmgr.h"
#include "lltextbox.h"
#include "lluictrl.h"
#include "llviewerobject.h"
#include "llviewerwindow.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <system_error>

using namespace sw67::photo;
using sw67::a11y::StatusRole;

namespace
{
constexpr F32 kSettleSeconds = 1.5f; // Let LODs and textures catch up after each camera move.

/// The centred crop of a raw image. Rows are bottom-up in LLImageRaw, and
/// the crop is centred, so the same rectangle works either way up.
LLPointer<LLImageRaw> cropRaw(LLImageRaw* src, const PixelRect& r)
{
    const S32 comps = src->getComponents();
    const S32 w = src->getWidth();
    const S32 h = src->getHeight();
    if (r.width <= 0 || r.height <= 0 || r.x < 0 || r.y < 0 || r.x + r.width > w || r.y + r.height > h)
    {
        return nullptr;
    }
    LLPointer<LLImageRaw> out = new LLImageRaw(static_cast<U16>(r.width), static_cast<U16>(r.height), static_cast<S8>(comps));
    const S32 yGL = h - r.y - r.height;
    for (S32 row = 0; row < r.height; ++row)
    {
        const U8* from = src->getData() + (static_cast<size_t>(yGL + row) * w + r.x) * comps;
        U8* to = out->getData() + static_cast<size_t>(row) * r.width * comps;
        std::memcpy(to, from, static_cast<size_t>(r.width) * comps);
    }
    return out;
}

bool saveImage(LLImageRaw* raw, const std::string& path, bool png)
{
    LLPointer<LLImageFormatted> formatted;
    if (png)
    {
        formatted = new LLImagePNG;
    }
    else
    {
        formatted = new LLImageJPEG(92);
    }
    return formatted->encode(raw, 0.f) && formatted->save(path);
}

std::string nativePath(const std::string& dir, const std::string& relative)
{
    std::string out = dir;
    std::string part;
    for (const char c : relative)
    {
        if (c == '/')
        {
            out = sw67viewer::joinPath(out, part);
            part.clear();
        }
        else
        {
            part.push_back(c);
        }
    }
    return sw67viewer::joinPath(out, part);
}

sw67::rlv::Decision cameraDecision()
{
    sw67::rlv::ActionRequest request;
    request.kind = sw67::rlv::ActionKind::ChangeCamera;
    return SW67RlvState::check(request);
}

std::string parentOf(const std::string& path)
{
    return fsyspath(std::filesystem::path(fsyspath(path)).parent_path()).string();
}
} // namespace

SW67FloaterProductPhoto::SW67FloaterProductPhoto(const LLSD& key)
:   LLFloater(key)
{
}

bool SW67FloaterProductPhoto::postBuild()
{
    LLScrollListCtrl* angles = getChild<LLScrollListCtrl>("angle_list");
    for (const CameraAngle& a : builtinAngles())
    {
        LLSD row;
        row["value"] = a.name;
        row["columns"][0]["column"] = "name";
        row["columns"][0]["value"] = a.name;
        row["columns"][1]["column"] = "detail";
        row["columns"][1]["value"] = llformat("%.0f deg around, %.0f deg up, %.1fx size away", a.azimuthDeg, a.elevationDeg, a.distanceFactor);
        angles->addElement(row);
    }
    angles->selectNthItem(0);
    LLScrollListCtrl* sizes = getChild<LLScrollListCtrl>("size_list");
    for (const ExportSize& s : builtinExportSizes())
    {
        LLSD row;
        row["value"] = s.key;
        row["columns"][0]["column"] = "name";
        row["columns"][0]["value"] = s.label;
        sizes->addElement(row);
    }
    sizes->selectNthItem(0);

    for (const char* name : {"angle_list", "size_list", "project_name", "name_pattern", "export_format", "capture_size",
                             "output_dir"})
    {
        getChild<LLUICtrl>(name)->setCommitCallback([this](LLUICtrl*, const LLSD&) { refreshPlan(); });
    }
    getChild<LLUICtrl>("browse_btn")->setCommitCallback([this](LLUICtrl*, const LLSD&) { onBrowse(); });
    getChild<LLUICtrl>("start_btn")->setCommitCallback([this](LLUICtrl*, const LLSD&) { onStart(); });
    getChild<LLUICtrl>("stop_btn")->setCommitCallback([this](LLUICtrl*, const LLSD&) { onStop(); });
    getChild<LLUICtrl>("stop_btn")->setEnabled(false);
    getChild<LLUICtrl>("output_dir")->setValue(LLSD(sw67viewer::userDir("photos")));
    refreshPlan();
    return LLFloater::postBuild();
}

PhotoSession SW67FloaterProductPhoto::sessionFromControls() const
{
    PhotoSession s;
    s.project = getChild<LLUICtrl>("project_name")->getValue().asString();
    if (s.project.empty())
    {
        s.project = "product";
    }
    s.namePattern = getChild<LLUICtrl>("name_pattern")->getValue().asString();
    if (s.namePattern.empty())
    {
        s.namePattern = "{project}_{angle}_{index:3}";
    }
    s.exportFormat = getChild<LLUICtrl>("export_format")->getValue().asString();
    const S32 capture = getChild<LLUICtrl>("capture_size")->getValue().asInteger();
    s.captureWidth = s.captureHeight = (capture >= 512 && capture <= 4096) ? capture : 2048;
    s.date = LLDate::now().asString().substr(0, 10);

    const std::vector<CameraAngle> allAngles = builtinAngles();
    for (LLScrollListItem* item : getChild<LLScrollListCtrl>("angle_list")->getAllSelected())
    {
        for (const CameraAngle& a : allAngles)
        {
            if (a.name == item->getValue().asString()) s.angles.push_back(a);
        }
    }
    const std::vector<ExportSize> allSizes = builtinExportSizes();
    for (LLScrollListItem* item : getChild<LLScrollListCtrl>("size_list")->getAllSelected())
    {
        for (const ExportSize& e : allSizes)
        {
            if (e.key == item->getValue().asString()) s.sizes.push_back(e);
        }
    }
    // This increment captures under the current backdrop and lighting;
    // switching backdrops and lighting looks between passes is planned.
    s.backgrounds = {"current"};
    s.lighting = {"current"};
    return s;
}

std::set<std::string> SW67FloaterProductPhoto::existingFiles() const
{
    std::set<std::string> out;
    const std::string dir = sw67viewer::joinPath(getChild<LLUICtrl>("output_dir")->getValue().asString(), "originals");
    for (const std::string& f : sw67viewer::listFiles(dir, ".png"))
    {
        out.insert("originals/" + f);
    }
    return out;
}

void SW67FloaterProductPhoto::onBrowse()
{
    const LLHandle<LLFloater> handle = getHandle();
    (new LLDirPickerThread(
         [handle](const std::vector<std::string>& folders, std::string) {
             auto* self = static_cast<SW67FloaterProductPhoto*>(handle.get());
             if (!self || folders.empty())
             {
                 return;
             }
             self->getChild<LLUICtrl>("output_dir")->setValue(LLSD(folders.front()));
             self->refreshPlan();
         },
         getChild<LLUICtrl>("output_dir")->getValue().asString()))
        ->getFile();
}

void SW67FloaterProductPhoto::refreshPlan()
{
    if (mRunning)
    {
        return;
    }
    const PhotoSession s = sessionFromControls();
    const std::vector<Shot> shots = planBatch(s, existingFiles());
    std::string text;
    if (shots.empty())
    {
        text = "Choose at least one angle.";
    }
    else
    {
        size_t files = 0;
        for (const Shot& shot : shots)
        {
            files += 1 + shot.exports.size();
            text += llformat("Shot %d, %s:\n", shot.index, shot.angle.name.c_str());
            text += "  original  " + shot.original.path + "\n";
            for (const PlannedFile& f : shot.exports)
            {
                text += "  export    " + f.path + "\n";
            }
        }
        text = llformat("%d shot(s), %d file(s). Originals are saved as PNG and never overwritten; exports are made "
                        "from them.\n\n",
                        static_cast<int>(shots.size()), static_cast<int>(files)) +
               text;
    }
    getChild<LLUICtrl>("plan_text")->setValue(LLSD(text));
}

void SW67FloaterProductPhoto::onStart()
{
    if (mRunning)
    {
        return;
    }
    LLViewerObject* subject = LLSelectMgr::getInstance()->getSelection()->getFirstRootObject(true);
    if (!subject)
    {
        showStatus(StatusRole::Warning, "Select the object to photograph first (right-click it and choose Edit, or "
                                        "select it in the build tools).");
        return;
    }
    const sw67::rlv::Decision camera = cameraDecision();
    if (camera.verdict != sw67::rlv::Verdict::Allowed)
    {
        showStatus(StatusRole::Restricted, camera.explanation);
        return;
    }
    mSession = sessionFromControls();
    mOutputDir = getChild<LLUICtrl>("output_dir")->getValue().asString();
    mShots = planBatch(mSession, existingFiles());
    if (mShots.empty())
    {
        showStatus(StatusRole::Warning, "Choose at least one angle.");
        return;
    }
    if (!sw67viewer::ensureDir(mOutputDir))
    {
        showStatus(StatusRole::Error, "Could not open or create " + mOutputDir + ".");
        return;
    }
    const LLBBox box = LLSelectMgr::getInstance()->getBBoxOfSelection();
    mSubjectCentre = gAgent.getPosGlobalFromAgent(box.getCenterAgent());
    mSubjectRadius = llmax(0.05, 0.5 * static_cast<double>(box.getExtentLocal().length()));
    F32 roll = 0.f, pitch = 0.f, yaw = 0.f;
    box.getRotation().getEulerAngles(&roll, &pitch, &yaw);
    mSubjectYawDeg = static_cast<double>(yaw) * 180.0 / F_PI;
    mSubjectId = subject->getID();
    mSavedEye = gAgentCamera.getCameraPositionGlobal();
    mSavedFocus = gAgentCamera.getFocusGlobal();
    mNext = 0;
    mWritten = 0;
    mRunning = true;
    mWaiting = false;
    getChild<LLUICtrl>("start_btn")->setEnabled(false);
    getChild<LLUICtrl>("stop_btn")->setEnabled(true);
    showStatus(StatusRole::Live, llformat("Capturing %d shot(s)...", static_cast<int>(mShots.size())));
}

void SW67FloaterProductPhoto::onStop()
{
    if (mRunning)
    {
        finish(llformat("Stopped after %d of %d shot(s). Files already written are kept.", static_cast<int>(mNext),
                        static_cast<int>(mShots.size())),
               StatusRole::Warning);
    }
}

void SW67FloaterProductPhoto::onClose(bool app_quitting)
{
    if (mRunning && !app_quitting)
    {
        onStop();
    }
    LLFloater::onClose(app_quitting);
}

void SW67FloaterProductPhoto::draw()
{
    if (mRunning)
    {
        step();
    }
    LLFloater::draw();
}

void SW67FloaterProductPhoto::step()
{
    if (mNext >= mShots.size())
    {
        finish(llformat("Done: %d file(s) written to %s. A manifest lists every file and how it was made.", mWritten,
                        mOutputDir.c_str()),
               StatusRole::Success);
        return;
    }
    const Shot& shot = mShots[mNext];
    if (!mWaiting)
    {
        // Check again before every move: a restriction may have arrived mid-batch.
        const sw67::rlv::Decision camera = cameraDecision();
        if (camera.verdict != sw67::rlv::Verdict::Allowed)
        {
            finish(camera.explanation, StatusRole::Restricted);
            return;
        }
        const Vec3 centre{mSubjectCentre.mdV[VX], mSubjectCentre.mdV[VY], mSubjectCentre.mdV[VZ]};
        const CameraPlacement place = placeCamera(shot.angle, centre, mSubjectRadius, mSubjectYawDeg);
        gAgentCamera.setCameraPosAndFocusGlobal(LLVector3d(place.eye.x, place.eye.y, place.eye.z),
                                                LLVector3d(place.focus.x, place.focus.y, place.focus.z), mSubjectId);
        mSettle.reset();
        mWaiting = true;
        return;
    }
    if (mSettle.getElapsedTimeF32() < kSettleSeconds)
    {
        return;
    }
    std::string error;
    if (!captureShot(shot, error))
    {
        finish(error, StatusRole::Error);
        return;
    }
    ++mNext;
    mWaiting = false;
    showStatus(StatusRole::Live, llformat("Captured %d of %d...", static_cast<int>(mNext), static_cast<int>(mShots.size())));
}

bool SW67FloaterProductPhoto::captureShot(const Shot& shot, std::string& error)
{
    LLPointer<LLImageRaw> raw = new LLImageRaw;
    if (!gViewerWindow->rawSnapshot(raw, mSession.captureWidth, mSession.captureHeight, false, false,
                                    false /*show_ui*/, false /*show_hud*/, true /*do_rebuild*/, false, false))
    {
        error = "The viewer could not take the snapshot for " + shot.original.path + ".";
        return false;
    }
    const std::string originalPath = nativePath(mOutputDir, shot.original.path);
    std::error_code ec;
    if (std::filesystem::exists(fsyspath(originalPath), ec))
    {
        // planBatch avoids existing names; never overwrite an original.
        error = "Refused to overwrite " + originalPath + ".";
        return false;
    }
    if (!sw67viewer::ensureDir(parentOf(originalPath)) || !saveImage(raw, originalPath, true))
    {
        error = "Could not write " + originalPath + ".";
        return false;
    }
    ++mWritten;
    for (const PlannedFile& f : shot.exports)
    {
        const PixelRect crop = coverCrop(raw->getWidth(), raw->getHeight(), f.width, f.height);
        LLPointer<LLImageRaw> out = cropRaw(raw, crop);
        if (out.isNull() || !out->scale(f.width, f.height))
        {
            error = "Could not resize for " + f.path + ".";
            return false;
        }
        const std::string path = nativePath(mOutputDir, f.path);
        const bool png = f.path.size() > 4 && f.path.compare(f.path.size() - 4, 4, ".png") == 0;
        if (!sw67viewer::ensureDir(parentOf(path)) || !saveImage(out, path, png))
        {
            error = "Could not write " + path + ".";
            return false;
        }
        ++mWritten;
    }
    return true;
}

void SW67FloaterProductPhoto::finish(const std::string& message, StatusRole role)
{
    mRunning = false;
    mWaiting = false;
    // Only the shots that were captured go in the manifest.
    const std::vector<Shot> done(mShots.begin(), mShots.begin() + static_cast<std::ptrdiff_t>(std::min(mNext, mShots.size())));
    if (!done.empty())
    {
        const std::string manifest = sw67viewer::joinPath(
            mOutputDir, "manifest-" + safeFileStem(mSession.project) + "-" + mSession.date + ".sw67.json");
        sw67viewer::writeFileAtomic(manifest, writeBatchManifest(mSession, done));
    }
    gAgentCamera.setCameraPosAndFocusGlobal(mSavedEye, mSavedFocus, LLUUID::null);
    getChild<LLUICtrl>("start_btn")->setEnabled(true);
    getChild<LLUICtrl>("stop_btn")->setEnabled(false);
    showStatus(role, message + " The camera is back where it was.");
    refreshPlan();
}

void SW67FloaterProductPhoto::showStatus(StatusRole role, const std::string& text)
{
    LLTextBox* box = getChild<LLTextBox>("status_text");
    box->setText(sw67viewer::statusText(role) + "  " + text);
    box->setColor(LLUIColor(sw67viewer::statusColor(role)));
}
