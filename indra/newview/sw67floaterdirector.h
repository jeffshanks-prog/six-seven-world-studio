/**
 * @file sw67floaterdirector.h
 * @brief Director Mode: saved camera angles, editable camera paths, shot
 *        lists, and export of a path as an image sequence through the
 *        viewer's snapshot capture. The camera moves are this viewer's own:
 *        nothing is sent to or shown to anyone else. With reduced motion on,
 *        paths play as cuts between keys.
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

#include "llfile.h"
#include "llfloater.h"
#include "lltimer.h"

#include "sw67/a11y/profile.h"
#include "sw67/director/avi.h"
#include "sw67/director/director.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

class LLImageRaw;

class SW67FloaterDirector final : public LLFloater
{
public:
    AL_VIEW_TYPE(SW67FloaterDirector, LLFloater);

    SW67FloaterDirector(const LLSD& key);
    bool postBuild() override;
    void onOpen(const LLSD& key) override;
    void onClose(bool app_quitting) override;

private:
    ~SW67FloaterDirector() override;

    // Productions
    void refreshProductions();
    void onNew();
    void onSave();
    void onLoad();
    // Angles
    void refreshAngles();
    void onSaveAngle();
    void onGotoAngle();
    void onRemoveAngle();
    // Paths
    void refreshPaths();
    void refreshKeys();
    sw67::director::CameraPath* currentPath();
    void onNewPath();
    void onSelectPath();
    void onAddKey(bool replaceSelected);
    void onRemoveKey();
    void onPathOptions();
    void onPlay();
    void onStop();
    // Shot list
    void refreshShots();
    void loadShot();
    bool shotFromEditor(sw67::director::ShotEntry& entry);
    void onAddShot();
    void onUpdateShot();
    void onRemoveShot();
    void onToggleDone();
    void onFrameShot();
    // Export
    void onExport();

    static void onIdle(void* data);
    void tick();
    void finishRun(sw67::a11y::StatusRole role, const std::string& text);
    bool cameraAllowed();
    void applyCamera(const sw67::pose::CameraPose& pose, double fovDeg);
    void showStatus(sw67::a11y::StatusRole role, const std::string& text);
    static std::string productionsDir();

    sw67::director::Production mProduction;
    int mPathIndex = -1;

    enum class Run
    {
        None,
        Playing,
        Exporting,
    };
    Run mRun = Run::None;
    sw67::director::CameraPath mRunPath;
    sw67::pose::Subject mRunSubject;
    bool mRunCuts = false;
    LLFrameTimer mRunClock;
    F32 mSavedFov = 0.f;
    bool mIdleAdded = false;
    // Export state
    sw67::director::FramePlan mFrames;
    size_t mFrame = 0;
    bool mFramePlaced = false;
    std::string mExportDir;
    int mWritten = 0;
    // Video export: frames go into Motion JPEG AVI files, a new numbered
    // part each time one reaches its size cap.
    bool writeVideoFrame(const LLImageRaw* raw, std::string& why);
    bool openVideoPart(std::string& why);
    bool closeVideo(std::string* why = nullptr);
    bool mAsVideo = false;
    int mExportFps = 30;
    std::string mVideoStem;
    std::vector<std::string> mVideoFiles;
    std::unique_ptr<llofstream> mVideoOut;
    sw67::director::MjpegAviWriter mVideo;
    S32 mShotW = 0;
    S32 mShotH = 0;
};
