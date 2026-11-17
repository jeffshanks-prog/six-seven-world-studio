/**
 * @file sw67floaterposestudio.cpp
 * @brief Pose and Photo Studio floater.
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

#include "sw67floaterposestudio.h"

#include "sw67accessibility.h"
#include "sw67rlvstate.h"
#include "sw67viewerfiles.h"

#include "llagent.h"
#include "llagentcamera.h"
#include "llcombobox.h"
#include "llfloaterreg.h"
#include "llfolderviewitem.h"
#include "llfolderviewmodelinventory.h"
#include "llformat.h"
#include "llinventorymodel.h"
#include "llinventorypanel.h"
#include "llscrolllistctrl.h"
#include "lltextbox.h"
#include "lluictrl.h"
#include "llviewercamera.h"
#include "llviewerinventory.h"
#include "llvoavatarself.h"

#include <algorithm>
#include <cmath>

using namespace sw67::pose;
using sw67::a11y::StatusRole;

namespace
{
LLVector3d toGlobal(const Vec3& v)
{
    return LLVector3d(v.x, v.y, v.z);
}

Vec3 fromGlobal(const LLVector3d& v)
{
    return {v.mdV[VX], v.mdV[VY], v.mdV[VZ]};
}

double fovDegNow()
{
    return static_cast<double>(LLViewerCamera::getInstance()->getView()) * 180.0 / F_PI;
}

void addRow(LLScrollListCtrl* list, const std::string& value, const std::string& text)
{
    LLSD row;
    row["value"] = value;
    row["columns"][0]["column"] = "name";
    row["columns"][0]["value"] = text;
    list->addElement(row);
}
} // namespace

SW67FloaterPoseStudio::SW67FloaterPoseStudio(const LLSD& key)
:   LLFloater(key)
{
    mShoot.name = "Photo shoot";
}

bool SW67FloaterPoseStudio::postBuild()
{
    LLScrollListCtrl* framings = getChild<LLScrollListCtrl>("framing_list");
    for (const Framing& f : builtinFramings())
    {
        addRow(framings, f.key, f.name);
    }
    framings->selectFirstItem();
    framings->setDoubleClickCallback([this]() { onFrame(); });
    getChild<LLUICtrl>("frame_btn")->setCommitCallback([this](LLUICtrl*, const LLSD&) { onFrame(); });
    getChild<LLUICtrl>("reset_btn")->setCommitCallback([](LLUICtrl*, const LLSD&) { gAgentCamera.resetView(); });
    getChild<LLUICtrl>("save_shot_btn")->setCommitCallback([this](LLUICtrl*, const LLSD&) { onSaveShot(); });
    getChild<LLUICtrl>("goto_shot_btn")->setCommitCallback([this](LLUICtrl*, const LLSD&) { onGotoShot(); });
    getChild<LLScrollListCtrl>("shot_list")->setDoubleClickCallback([this]() { onGotoShot(); });
    getChild<LLUICtrl>("remove_shot_btn")->setCommitCallback([this](LLUICtrl*, const LLSD&) { onRemoveShot(); });
    getChild<LLUICtrl>("add_pose_btn")->setCommitCallback([this](LLUICtrl*, const LLSD&) { onAddPoses(); });
    getChild<LLUICtrl>("remove_pose_btn")->setCommitCallback([this](LLUICtrl*, const LLSD&) { onRemovePose(); });
    getChild<LLUICtrl>("play_btn")->setCommitCallback([this](LLUICtrl*, const LLSD&) {
        const S32 sel = getChild<LLScrollListCtrl>("pose_list")->getFirstSelectedIndex();
        playPose(sel >= 0 ? std::optional<size_t>(static_cast<size_t>(sel)) : stepPose(mShoot, mPoseIndex, 1));
    });
    getChild<LLUICtrl>("next_btn")->setCommitCallback(
        [this](LLUICtrl*, const LLSD&) { playPose(stepPose(mShoot, mPoseIndex, 1)); });
    getChild<LLUICtrl>("prev_btn")->setCommitCallback(
        [this](LLUICtrl*, const LLSD&) { playPose(stepPose(mShoot, mPoseIndex, -1)); });
    getChild<LLUICtrl>("stop_btn")->setCommitCallback([this](LLUICtrl*, const LLSD&) { stopPose(); });
    getChild<LLUICtrl>("snapshot_btn")->setCommitCallback(
        [](LLUICtrl*, const LLSD&) { LLFloaterReg::showInstance("snapshot"); });
    getChild<LLUICtrl>("save_shoot_btn")->setCommitCallback([this](LLUICtrl*, const LLSD&) { onSaveShoot(); });
    getChild<LLUICtrl>("shoot_combo")->setCommitCallback([this](LLUICtrl*, const LLSD&) { onLoadShoot(); });
    getChild<LLUICtrl>("new_shoot_btn")->setCommitCallback([this](LLUICtrl*, const LLSD&) { onNewShoot(); });
    return LLFloater::postBuild();
}

void SW67FloaterPoseStudio::onOpen(const LLSD& key)
{
    LLFloater::onOpen(key);
    getChild<LLUICtrl>("shoot_name")->setValue(LLSD(mShoot.name));
    refreshShoots();
    refreshShots();
    refreshPoses();
}

void SW67FloaterPoseStudio::onClose(bool app_quitting)
{
    if (!app_quitting)
    {
        stopPose();
    }
    LLFloater::onClose(app_quitting);
}

std::string SW67FloaterPoseStudio::shootsDir()
{
    return sw67viewer::userDir("photoshoots");
}

std::optional<Subject> SW67FloaterPoseStudio::subjectNow()
{
    if (!isAgentAvatarValid())
    {
        return std::nullopt;
    }
    Subject s;
    s.position = fromGlobal(gAgent.getPositionGlobal());
    const LLVector3 at = gAgent.getAtAxis();
    s.yawDeg = std::atan2(static_cast<double>(at.mV[VY]), static_cast<double>(at.mV[VX])) * 180.0 / F_PI;
    const double h = static_cast<double>(gAgentAvatarp->mBodySize.mV[VZ]);
    if (h > 0.2)
    {
        s.height = h;
    }
    return s;
}

bool SW67FloaterPoseStudio::moveCamera(const CameraPose& pose)
{
    sw67::rlv::ActionRequest request;
    request.kind = sw67::rlv::ActionKind::ChangeCamera;
    const sw67::rlv::Decision d = SW67RlvState::check(request);
    if (!d.allowed())
    {
        showStatus(StatusRole::Restricted, d.explanation);
        return false;
    }
    gAgentCamera.setCameraPosAndFocusGlobal(toGlobal(pose.eye), toGlobal(pose.focus), gAgent.getID());
    return true;
}

void SW67FloaterPoseStudio::onFrame()
{
    LLScrollListItem* sel = getChild<LLScrollListCtrl>("framing_list")->getFirstSelected();
    const auto framing = sel ? findFraming(sel->getValue().asString()) : std::nullopt;
    const auto subject = subjectNow();
    if (!framing || !subject)
    {
        showStatus(StatusRole::Info, "Choose a framing. Your avatar needs to be in the world.");
        return;
    }
    if (moveCamera(frame(*framing, *subject, fovDegNow())))
    {
        showStatus(StatusRole::Success, framing->name + ". Camera back to normal returns it behind you.");
    }
}

void SW67FloaterPoseStudio::refreshShots()
{
    LLScrollListCtrl* list = getChild<LLScrollListCtrl>("shot_list");
    list->deleteAllItems();
    for (size_t i = 0; i < mShoot.shots.size(); ++i)
    {
        addRow(list, std::to_string(i), mShoot.shots[i].name);
    }
}

void SW67FloaterPoseStudio::refreshPoses()
{
    LLScrollListCtrl* list = getChild<LLScrollListCtrl>("pose_list");
    list->deleteAllItems();
    for (size_t i = 0; i < mShoot.poses.size(); ++i)
    {
        const bool playing = mPoseIndex && *mPoseIndex == i && mPlayingAsset.notNull();
        addRow(list, std::to_string(i), (playing ? "> " : "") + mShoot.poses[i].name + (playing ? " (playing)" : ""));
    }
}

void SW67FloaterPoseStudio::refreshShoots()
{
    LLComboBox* combo = getChild<LLComboBox>("shoot_combo");
    combo->removeall();
    for (const std::string& file : sw67viewer::listFiles(shootsDir(), ".sw67photoshoot.json"))
    {
        combo->add(file.substr(0, file.size() - std::string(".sw67photoshoot.json").size()), LLSD(file));
    }
    combo->setLabel(std::string("Open a saved shoot"));
}

void SW67FloaterPoseStudio::onSaveShot()
{
    const auto subject = subjectNow();
    if (!subject)
    {
        showStatus(StatusRole::Info, "Your avatar needs to be in the world.");
        return;
    }
    std::string name = getChild<LLUICtrl>("shot_name")->getValue().asString();
    if (name.empty()) name = llformat("Shot %d", static_cast<int>(mShoot.shots.size() + 1));
    const CameraPose camera{fromGlobal(gAgentCamera.getCameraPositionGlobal()), fromGlobal(gAgentCamera.getFocusGlobal())};
    Shot shot = shotFromCamera(name, camera, *subject, fovDegNow());
    if (mPoseIndex && *mPoseIndex < mShoot.poses.size())
    {
        shot.poseId = mShoot.poses[*mPoseIndex].itemId;
    }
    mShoot.shots.push_back(std::move(shot));
    refreshShots();
    showStatus(StatusRole::Success, "Saved \"" + name + "\" relative to you, so it works wherever you stand.");
}

void SW67FloaterPoseStudio::onGotoShot()
{
    const S32 sel = getChild<LLScrollListCtrl>("shot_list")->getFirstSelectedIndex();
    const auto subject = subjectNow();
    if (sel < 0 || static_cast<size_t>(sel) >= mShoot.shots.size() || !subject)
    {
        return;
    }
    const Shot& shot = mShoot.shots[static_cast<size_t>(sel)];
    if (!shot.poseId.empty())
    {
        for (size_t i = 0; i < mShoot.poses.size(); ++i)
        {
            if (mShoot.poses[i].itemId == shot.poseId) playPose(i);
        }
    }
    if (moveCamera(placeShot(shot, *subject)))
    {
        showStatus(StatusRole::Success, "Camera on \"" + shot.name + "\".");
    }
}

void SW67FloaterPoseStudio::onRemoveShot()
{
    const S32 sel = getChild<LLScrollListCtrl>("shot_list")->getFirstSelectedIndex();
    if (sel < 0 || static_cast<size_t>(sel) >= mShoot.shots.size()) return;
    mShoot.shots.erase(mShoot.shots.begin() + sel);
    refreshShots();
}

void SW67FloaterPoseStudio::onAddPoses()
{
    LLInventoryPanel* panel = LLInventoryPanel::getActiveInventoryPanel(false);
    if (!panel)
    {
        showStatus(StatusRole::Info, "Open your inventory and select the animations to use as poses first.");
        return;
    }
    int added = 0;
    for (LLFolderViewItem* fv : panel->getSelectedItems())
    {
        const auto* model = fv ? static_cast<LLFolderViewModelItemInventory*>(fv->getViewModelItem()) : nullptr;
        const LLViewerInventoryItem* item = model ? gInventory.getItem(model->getUUID()) : nullptr;
        if (item && item->getIsLinkType()) item = item->getLinkedItem();
        if (!item || item->getType() != LLAssetType::AT_ANIMATION) continue;
        const std::string id = item->getUUID().asString();
        if (std::none_of(mShoot.poses.begin(), mShoot.poses.end(), [&](const Pose& p) { return p.itemId == id; }))
        {
            mShoot.poses.push_back({id, item->getName()});
            ++added;
        }
    }
    refreshPoses();
    showStatus(StatusRole::Info, llformat("Added %d pose(s). Only animations can be poses.", added));
}

void SW67FloaterPoseStudio::onRemovePose()
{
    const S32 sel = getChild<LLScrollListCtrl>("pose_list")->getFirstSelectedIndex();
    if (sel < 0 || static_cast<size_t>(sel) >= mShoot.poses.size()) return;
    if (mPoseIndex && *mPoseIndex == static_cast<size_t>(sel)) stopPose();
    mShoot.poses.erase(mShoot.poses.begin() + sel);
    mPoseIndex.reset();
    refreshPoses();
}

void SW67FloaterPoseStudio::playPose(std::optional<size_t> index)
{
    if (!index || *index >= mShoot.poses.size())
    {
        showStatus(StatusRole::Info, "Add poses from your inventory first.");
        return;
    }
    const Pose& pose = mShoot.poses[*index];
    LLUUID id;
    const LLViewerInventoryItem* item = id.set(pose.itemId, false) ? gInventory.getItem(id) : nullptr;
    if (!item)
    {
        showStatus(StatusRole::Warning, "\"" + pose.name + "\" is no longer in your inventory.");
        return;
    }
    stopPose();
    // Plays on your own avatar only. Others nearby see you pose, as with any
    // animation you play yourself.
    mPlayingAsset = item->getAssetUUID();
    gAgent.sendAnimationRequest(mPlayingAsset, ANIM_REQUEST_START);
    mPoseIndex = index;
    refreshPoses();
    showStatus(StatusRole::Live, "Posing: " + pose.name + ". Stop pose ends it.");
}

void SW67FloaterPoseStudio::stopPose()
{
    if (mPlayingAsset.notNull())
    {
        gAgent.sendAnimationRequest(mPlayingAsset, ANIM_REQUEST_STOP);
        mPlayingAsset.setNull();
        refreshPoses();
        showStatus(StatusRole::Neutral, "Pose stopped.");
    }
}

void SW67FloaterPoseStudio::onSaveShoot()
{
    mShoot.name = getChild<LLUICtrl>("shoot_name")->getValue().asString();
    if (mShoot.name.empty())
    {
        showStatus(StatusRole::Warning, "Give the shoot a name first.");
        return;
    }
    const std::string path =
        sw67viewer::joinPath(shootsDir(), sw67::photo::safeFileStem(mShoot.name) + ".sw67photoshoot.json");
    if (!sw67viewer::writeFileAtomic(path, writePhotoshoot(mShoot)))
    {
        showStatus(StatusRole::Error, "Could not save " + path + ".");
        return;
    }
    refreshShoots();
    showStatus(StatusRole::Success, "Saved the shoot to " + path + ".");
}

void SW67FloaterPoseStudio::onLoadShoot()
{
    const std::string file = getChild<LLUICtrl>("shoot_combo")->getValue().asString();
    if (file.empty()) return;
    const auto text = sw67viewer::readFile(sw67viewer::joinPath(shootsDir(), file), 2 * 1024 * 1024);
    ReadResult r = text ? readPhotoshoot(*text) : ReadResult{};
    if (!r.shoot)
    {
        showStatus(StatusRole::Error, "Could not read that shoot.");
        return;
    }
    stopPose();
    mShoot = std::move(*r.shoot);
    mPoseIndex.reset();
    getChild<LLUICtrl>("shoot_name")->setValue(LLSD(mShoot.name));
    refreshShots();
    refreshPoses();
    showStatus(r.notes.empty() ? StatusRole::Success : StatusRole::Warning,
               "Opened \"" + mShoot.name + "\"." + (r.notes.empty() ? "" : " " + r.notes.front()));
}

void SW67FloaterPoseStudio::onNewShoot()
{
    stopPose();
    mShoot = Photoshoot();
    mShoot.name = "Photo shoot";
    mPoseIndex.reset();
    getChild<LLUICtrl>("shoot_name")->setValue(LLSD(mShoot.name));
    refreshShots();
    refreshPoses();
}

void SW67FloaterPoseStudio::showStatus(StatusRole role, const std::string& text)
{
    LLTextBox* box = getChild<LLTextBox>("status_text");
    box->setText(sw67viewer::statusText(role) + "  " + text);
    box->setColor(LLUIColor(sw67viewer::statusColor(role)));
}
