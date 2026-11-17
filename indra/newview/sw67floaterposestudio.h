/**
 * @file sw67floaterposestudio.h
 * @brief Pose and Photo Studio (manual): frames your own avatar from
 *        ready-made framings, saves your own camera shots relative to you,
 *        and steps through poses from your inventory on your own avatar.
 *        The camera moves only after the RLV gate allows it.
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

#include "llfloater.h"

#include "sw67/a11y/profile.h"
#include "sw67/pose/studio.h"

#include <optional>
#include <string>

class SW67FloaterPoseStudio final : public LLFloater
{
public:
    AL_VIEW_TYPE(SW67FloaterPoseStudio, LLFloater);

    SW67FloaterPoseStudio(const LLSD& key);
    bool postBuild() override;
    void onOpen(const LLSD& key) override;
    void onClose(bool app_quitting) override;

    /// Where your avatar stands and faces now, if it is in the world.
    static std::optional<sw67::pose::Subject> subjectNow();

private:
    ~SW67FloaterPoseStudio() override = default;

    bool moveCamera(const sw67::pose::CameraPose& pose);
    void refreshShoots();
    void refreshShots();
    void refreshPoses();
    void onFrame();
    void onSaveShot();
    void onGotoShot();
    void onRemoveShot();
    void onAddPoses();
    void onRemovePose();
    void playPose(std::optional<size_t> index);
    void stopPose();
    void onSaveShoot();
    void onLoadShoot();
    void onNewShoot();
    void showStatus(sw67::a11y::StatusRole role, const std::string& text);
    static std::string shootsDir();

    sw67::pose::Photoshoot mShoot;
    std::optional<size_t> mPoseIndex;
    LLUUID mPlayingAsset;
};
