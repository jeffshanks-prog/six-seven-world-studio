/**
 * @file sw67floaterproductphoto.h
 * @brief Product Photography Assistant (manual): plans a batch of shots of
 *        the selected object from reusable angles, previews every file it
 *        will write, then moves the camera to each angle, captures the
 *        original losslessly and writes each export size from it.
 *
 *        The camera is moved only through the viewer's own camera, after the
 *        RLV gate allows a camera change, and is put back afterwards.
 *        Nothing in the world is changed.
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
#include "lltimer.h"
#include "v3dmath.h"

#include "sw67/a11y/profile.h"
#include "sw67/photo/product.h"

#include <optional>
#include <set>
#include <string>
#include <vector>

class SW67FloaterProductPhoto final : public LLFloater
{
public:
    AL_VIEW_TYPE(SW67FloaterProductPhoto, LLFloater);

    SW67FloaterProductPhoto(const LLSD& key);
    bool postBuild() override;
    void draw() override;
    void onClose(bool app_quitting) override;

private:
    ~SW67FloaterProductPhoto() override = default;

    sw67::photo::PhotoSession sessionFromControls() const;
    std::set<std::string> existingFiles() const;
    void onBrowse();
    void refreshPlan();
    void onStart();
    void onStop();
    void step();
    bool captureShot(const sw67::photo::Shot& shot, std::string& error);
    void finish(const std::string& message, sw67::a11y::StatusRole role);
    void showStatus(sw67::a11y::StatusRole role, const std::string& text);

    // Capture state.
    std::vector<sw67::photo::Shot> mShots;
    sw67::photo::PhotoSession mSession;
    std::string mOutputDir;
    size_t mNext = 0;
    bool mRunning = false;
    bool mWaiting = false;
    LLFrameTimer mSettle;
    LLVector3d mSubjectCentre;
    double mSubjectRadius = 1.0;
    double mSubjectYawDeg = 0.0;
    LLUUID mSubjectId;
    LLVector3d mSavedEye;
    LLVector3d mSavedFocus;
    int mWritten = 0;
};
