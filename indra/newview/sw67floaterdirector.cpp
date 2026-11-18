/**
 * @file sw67floaterdirector.cpp
 * @brief Director Mode floater.
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

#include "sw67floaterdirector.h"

#include "sw67accessibility.h"
#include "sw67floaterposestudio.h"
#include "sw67rlvstate.h"
#include "sw67viewerfiles.h"

#include "sw67/rlv/gate.h"

#include "llagent.h"
#include "llagentcamera.h"
#include "llcallbacklist.h"
#include "llcombobox.h"
#include "lldate.h"
#include "llfile.h"
#include "llformat.h"
#include "llimagepng.h"
#include "llscrolllistctrl.h"
#include "lltextbox.h"
#include "lluictrl.h"
#include "llviewercamera.h"
#include "llviewerwindow.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <functional>

using namespace sw67::director;
using sw67::a11y::StatusRole;
using sw67::photo::Vec3;
using sw67::pose::CameraPose;
using sw67::pose::Shot;
using sw67::pose::Subject;

namespace
{
constexpr const char* kSuffix = ".sw67director.json";

LLVector3d toGlobal(const Vec3& v) { return LLVector3d(v.x, v.y, v.z); }
Vec3 fromGlobal(const LLVector3d& v) { return {v.mdV[VX], v.mdV[VY], v.mdV[VZ]}; }

double fovDegNow()
{
    return static_cast<double>(LLViewerCamera::getInstance()->getView()) * 180.0 / F_PI;
}

void addRow(LLScrollListCtrl* list, S32 value, const std::vector<std::pair<const char*, std::string>>& cols)
{
    LLSD row;
    row["value"] = value;
    for (size_t n = 0; n < cols.size(); ++n)
    {
        row["columns"][static_cast<S32>(n)]["column"] = cols[n].first;
        row["columns"][static_cast<S32>(n)]["value"] = cols[n].second;
    }
    list->addElement(row);
}

S32 selectedIndex(LLScrollListCtrl* list, size_t size)
{
    LLScrollListItem* sel = list->getFirstSelected();
    if (!sel) return -1;
    const S32 n = sel->getValue().asInteger();
    return n >= 0 && static_cast<size_t>(n) < size ? n : -1;
}

std::string secondsText(double s)
{
    return llformat("%.1f s", s);
}
} // namespace

SW67FloaterDirector::SW67FloaterDirector(const LLSD& key)
:   LLFloater(key)
{
}

SW67FloaterDirector::~SW67FloaterDirector()
{
    if (mIdleAdded)
    {
        gIdleCallbacks.deleteFunction(&SW67FloaterDirector::onIdle, this);
    }
}

bool SW67FloaterDirector::postBuild()
{
    auto bind = [this](const char* name, std::function<void()> fn) {
        getChild<LLUICtrl>(name)->setCommitCallback([fn](LLUICtrl*, const LLSD&) { fn(); });
    };
    bind("new_btn", [this] { onNew(); });
    bind("save_btn", [this] { onSave(); });
    bind("production_combo", [this] { onLoad(); });
    bind("save_angle_btn", [this] { onSaveAngle(); });
    bind("goto_angle_btn", [this] { onGotoAngle(); });
    bind("remove_angle_btn", [this] { onRemoveAngle(); });
    bind("new_path_btn", [this] { onNewPath(); });
    bind("path_list", [this] { onSelectPath(); });
    bind("add_key_btn", [this] { onAddKey(false); });
    bind("replace_key_btn", [this] { onAddKey(true); });
    bind("remove_key_btn", [this] { onRemoveKey(); });
    bind("easing_combo", [this] { onPathOptions(); });
    bind("loop_check", [this] { onPathOptions(); });
    bind("play_btn", [this] { onPlay(); });
    bind("stop_btn", [this] { onStop(); });
    bind("shot_list", [this] { loadShot(); });
    bind("add_shot_btn", [this] { onAddShot(); });
    bind("update_shot_btn", [this] { onUpdateShot(); });
    bind("remove_shot_btn", [this] { onRemoveShot(); });
    bind("done_btn", [this] { onToggleDone(); });
    bind("frame_shot_btn", [this] { onFrameShot(); });
    bind("export_btn", [this] { onExport(); });
    getChild<LLUICtrl>("stop_btn")->setEnabled(false);
    return LLFloater::postBuild();
}

void SW67FloaterDirector::onOpen(const LLSD& key)
{
    LLFloater::onOpen(key);
    refreshProductions();
    refreshAngles();
    refreshPaths();
    refreshShots();
    if (sw67viewer::loadAccessibility().reduceMotion)
    {
        showStatus(StatusRole::Info, "Reduced motion is on, so paths play as cuts between their keys.");
    }
}

void SW67FloaterDirector::onClose(bool app_quitting)
{
    if (mRun != Run::None && !app_quitting)
    {
        finishRun(StatusRole::Info, "Stopped.");
    }
    LLFloater::onClose(app_quitting);
}

std::string SW67FloaterDirector::productionsDir()
{
    return sw67viewer::userDir("director");
}

// ---- productions ------------------------------------------------------------

void SW67FloaterDirector::refreshProductions()
{
    LLComboBox* combo = getChild<LLComboBox>("production_combo");
    combo->removeall();
    const std::string suffix(kSuffix);
    for (const std::string& file : sw67viewer::listFiles(productionsDir(), suffix))
    {
        combo->add(file.substr(0, file.size() - suffix.size()), LLSD(file));
    }
    combo->setLabel(std::string("Open a saved production"));
}

void SW67FloaterDirector::onNew()
{
    mProduction = Production{};
    mPathIndex = -1;
    getChild<LLUICtrl>("production_name")->setValue(LLSD(""));
    refreshAngles();
    refreshPaths();
    refreshShots();
    showStatus(StatusRole::Info, "New production. Save angles, build paths and plan your shot list.");
}

void SW67FloaterDirector::onSave()
{
    mProduction.name = getChild<LLUICtrl>("production_name")->getValue().asString();
    if (mProduction.name.empty())
    {
        showStatus(StatusRole::Warning, "Name the production first.");
        return;
    }
    const std::string path = sw67viewer::joinPath(productionsDir(), fileNameFor(mProduction));
    if (!sw67viewer::writeFileAtomic(path, writeProduction(mProduction)))
    {
        showStatus(StatusRole::Error, "Could not save " + path + ".");
        return;
    }
    refreshProductions();
    showStatus(StatusRole::Success, "Saved \"" + mProduction.name + "\": " + path);
}

void SW67FloaterDirector::onLoad()
{
    const std::string file = getChild<LLComboBox>("production_combo")->getValue().asString();
    if (file.empty()) return;
    const auto text = sw67viewer::readFile(sw67viewer::joinPath(productionsDir(), file), 4 * 1024 * 1024);
    ReadResult r = text ? readProduction(*text) : ReadResult{};
    if (!r.production)
    {
        showStatus(StatusRole::Error, "Could not read that production.");
        return;
    }
    mProduction = std::move(*r.production);
    mPathIndex = mProduction.paths.empty() ? -1 : 0;
    getChild<LLUICtrl>("production_name")->setValue(LLSD(mProduction.name));
    refreshAngles();
    refreshPaths();
    refreshShots();
    std::string notes;
    for (const std::string& n : r.notes) notes += "\n  - " + n;
    showStatus(r.notes.empty() ? StatusRole::Success : StatusRole::Warning,
               "Opened \"" + mProduction.name + "\"" + (notes.empty() ? "." : " with notes:" + notes));
}

// ---- camera -----------------------------------------------------------------

bool SW67FloaterDirector::cameraAllowed()
{
    sw67::rlv::ActionRequest request;
    request.kind = sw67::rlv::ActionKind::ChangeCamera;
    const sw67::rlv::Decision d = SW67RlvState::check(request);
    if (!d.allowed())
    {
        showStatus(StatusRole::Restricted, d.explanation);
        return false;
    }
    return true;
}

void SW67FloaterDirector::applyCamera(const CameraPose& pose, double fovDeg)
{
    gAgentCamera.setCameraPosAndFocusGlobal(toGlobal(pose.eye), toGlobal(pose.focus), gAgent.getID());
    if (fovDeg >= 10.0 && fovDeg <= 120.0)
    {
        LLViewerCamera::getInstance()->setDefaultFOV(static_cast<F32>(fovDeg * F_PI / 180.0));
    }
}

// ---- angles -----------------------------------------------------------------

void SW67FloaterDirector::refreshAngles()
{
    LLScrollListCtrl* list = getChild<LLScrollListCtrl>("angle_list");
    list->deleteAllItems();
    for (size_t i = 0; i < mProduction.angles.size(); ++i)
    {
        addRow(list, static_cast<S32>(i), {{"name", mProduction.angles[i].name}});
    }
}

void SW67FloaterDirector::onSaveAngle()
{
    const auto subject = SW67FloaterPoseStudio::subjectNow();
    if (!subject)
    {
        showStatus(StatusRole::Info, "Your avatar needs to be in the world.");
        return;
    }
    if (mProduction.angles.size() >= kMaxShots) return;
    std::string name = getChild<LLUICtrl>("angle_name")->getValue().asString();
    if (name.empty()) name = llformat("Angle %d", static_cast<int>(mProduction.angles.size() + 1));
    const CameraPose camera{fromGlobal(gAgentCamera.getCameraPositionGlobal()), fromGlobal(gAgentCamera.getFocusGlobal())};
    mProduction.angles.push_back(sw67::pose::shotFromCamera(name, camera, *subject, fovDegNow()));
    refreshAngles();
    showStatus(StatusRole::Success, "Saved the angle \"" + name + "\" relative to you.");
}

void SW67FloaterDirector::onGotoAngle()
{
    const S32 n = selectedIndex(getChild<LLScrollListCtrl>("angle_list"), mProduction.angles.size());
    const auto subject = SW67FloaterPoseStudio::subjectNow();
    if (n < 0 || !subject || !cameraAllowed()) return;
    const Shot& a = mProduction.angles[static_cast<size_t>(n)];
    applyCamera(sw67::pose::placeShot(a, *subject), a.fovDeg);
    showStatus(StatusRole::Success, "Camera on \"" + a.name + "\". Only your view moved.");
}

void SW67FloaterDirector::onRemoveAngle()
{
    const S32 n = selectedIndex(getChild<LLScrollListCtrl>("angle_list"), mProduction.angles.size());
    if (n < 0) return;
    mProduction.angles.erase(mProduction.angles.begin() + n);
    refreshAngles();
}

// ---- paths ------------------------------------------------------------------

CameraPath* SW67FloaterDirector::currentPath()
{
    if (mPathIndex < 0 || static_cast<size_t>(mPathIndex) >= mProduction.paths.size()) return nullptr;
    return &mProduction.paths[static_cast<size_t>(mPathIndex)];
}

void SW67FloaterDirector::refreshPaths()
{
    LLScrollListCtrl* list = getChild<LLScrollListCtrl>("path_list");
    list->deleteAllItems();
    for (size_t i = 0; i < mProduction.paths.size(); ++i)
    {
        const CameraPath& p = mProduction.paths[i];
        addRow(list, static_cast<S32>(i), {{"name", p.name}, {"length", secondsText(p.duration())}});
    }
    if (mPathIndex >= 0) list->selectNthItem(mPathIndex);
    if (const CameraPath* p = currentPath())
    {
        getChild<LLUICtrl>("easing_combo")->setValue(LLSD(p->easing == Easing::Smooth ? "smooth" : "linear"));
        getChild<LLUICtrl>("loop_check")->setValue(LLSD(p->loop));
    }
    refreshKeys();
}

void SW67FloaterDirector::refreshKeys()
{
    LLScrollListCtrl* list = getChild<LLScrollListCtrl>("key_list");
    list->deleteAllItems();
    const CameraPath* p = currentPath();
    if (!p) return;
    for (size_t i = 0; i < p->keys.size(); ++i)
    {
        addRow(list, static_cast<S32>(i),
               {{"time", secondsText(p->keys[i].atSeconds)}, {"name", llformat("Key %d", static_cast<int>(i + 1))}});
    }
    const double next = p->keys.empty() ? 0.0 : p->keys.back().atSeconds + 2.0;
    getChild<LLUICtrl>("key_time")->setValue(LLSD(llformat("%.1f", next)));
}

void SW67FloaterDirector::onNewPath()
{
    if (mProduction.paths.size() >= kMaxPaths) return;
    CameraPath p;
    p.name = getChild<LLUICtrl>("path_name")->getValue().asString();
    if (p.name.empty()) p.name = llformat("Path %d", static_cast<int>(mProduction.paths.size() + 1));
    mProduction.paths.push_back(std::move(p));
    mPathIndex = static_cast<int>(mProduction.paths.size()) - 1;
    refreshPaths();
    showStatus(StatusRole::Info, "Place the camera where the path starts and press Add key; then move it and add more.");
}

void SW67FloaterDirector::onSelectPath()
{
    mPathIndex = selectedIndex(getChild<LLScrollListCtrl>("path_list"), mProduction.paths.size());
    refreshPaths();
}

void SW67FloaterDirector::onAddKey(bool replaceSelected)
{
    CameraPath* p = currentPath();
    const auto subject = SW67FloaterPoseStudio::subjectNow();
    if (!p || !subject)
    {
        showStatus(StatusRole::Info, "Choose or create a path first. Your avatar needs to be in the world.");
        return;
    }
    const CameraPose camera{fromGlobal(gAgentCamera.getCameraPositionGlobal()), fromGlobal(gAgentCamera.getFocusGlobal())};
    Key key;
    key.shot = sw67::pose::shotFromCamera("", camera, *subject, fovDegNow());
    if (replaceSelected)
    {
        const S32 n = selectedIndex(getChild<LLScrollListCtrl>("key_list"), p->keys.size());
        if (n < 0)
        {
            showStatus(StatusRole::Info, "Select the key to replace first.");
            return;
        }
        key.atSeconds = p->keys[static_cast<size_t>(n)].atSeconds;
        p->keys[static_cast<size_t>(n)] = key;
    }
    else
    {
        if (p->keys.size() >= kMaxKeys) return;
        const double t = std::atof(getChild<LLUICtrl>("key_time")->getValue().asString().c_str());
        key.atSeconds = std::clamp(std::isfinite(t) ? t : 0.0, 0.0, kMaxPathSeconds);
        p->keys.push_back(key);
    }
    sortKeys(*p);
    refreshPaths();
    const auto problems = validate(*p);
    showStatus(problems.empty() ? StatusRole::Success : StatusRole::Info,
               problems.empty() ? "Key saved. Play to preview the path." : problems.front());
}

void SW67FloaterDirector::onRemoveKey()
{
    CameraPath* p = currentPath();
    if (!p) return;
    const S32 n = selectedIndex(getChild<LLScrollListCtrl>("key_list"), p->keys.size());
    if (n < 0) return;
    p->keys.erase(p->keys.begin() + n);
    refreshPaths();
}

void SW67FloaterDirector::onPathOptions()
{
    CameraPath* p = currentPath();
    if (!p) return;
    p->easing = getChild<LLUICtrl>("easing_combo")->getValue().asString() == "linear" ? Easing::Linear : Easing::Smooth;
    p->loop = getChild<LLUICtrl>("loop_check")->getValue().asBoolean();
}

void SW67FloaterDirector::onPlay()
{
    const CameraPath* p = currentPath();
    const auto subject = SW67FloaterPoseStudio::subjectNow();
    if (!p || !subject)
    {
        showStatus(StatusRole::Info, "Choose a path. Your avatar needs to be in the world.");
        return;
    }
    const auto problems = validate(*p);
    if (!problems.empty())
    {
        showStatus(StatusRole::Warning, problems.front());
        return;
    }
    if (mRun != Run::None || !cameraAllowed()) return;
    mRunPath = *p;
    mRunSubject = *subject;
    mRunCuts = sw67viewer::loadAccessibility().reduceMotion;
    mSavedFov = LLViewerCamera::getInstance()->getDefaultFOV();
    mRun = Run::Playing;
    mRunClock.reset();
    if (!mIdleAdded)
    {
        gIdleCallbacks.addFunction(&SW67FloaterDirector::onIdle, this);
        mIdleAdded = true;
    }
    getChild<LLUICtrl>("stop_btn")->setEnabled(true);
    showStatus(StatusRole::Live, std::string("Playing \"") + p->name + "\"" + (mRunCuts ? " as cuts (reduced motion)" : "") +
                                     ". It keeps playing with the interface hidden. Only your view moves.");
}

void SW67FloaterDirector::onStop()
{
    if (mRun != Run::None) finishRun(StatusRole::Info, "Stopped.");
}

void SW67FloaterDirector::onIdle(void* data)
{
    static_cast<SW67FloaterDirector*>(data)->tick();
}

void SW67FloaterDirector::tick()
{
    if (mRun == Run::Playing)
    {
        const double t = mRunClock.getElapsedTimeF64();
        const Shot s = sample(mRunPath, t, mRunCuts);
        applyCamera(sw67::pose::placeShot(s, mRunSubject), s.fovDeg);
        if (!mRunPath.loop && t > mRunPath.duration())
        {
            finishRun(StatusRole::Success, "Finished playing \"" + mRunPath.name + "\".");
        }
        return;
    }
    if (mRun != Run::Exporting) return;

    // One frame per two idle ticks: place the camera, let the viewer update
    // it, then snapshot. A restriction arriving mid-export stops it.
    if (mFrame >= mFrames.times.size())
    {
        finishRun(StatusRole::Success, llformat("Exported %d frame(s) to %s. Join them into a video with your "
                                                "editor or a tool such as ffmpeg.",
                                                mWritten, mExportDir.c_str()));
        return;
    }
    if (!mFramePlaced)
    {
        if (!cameraAllowed())
        {
            finishRun(StatusRole::Restricted, "Export stopped by an RLV restriction on the camera.");
            return;
        }
        const Shot s = sample(mRunPath, mFrames.times[mFrame], mRunCuts);
        applyCamera(sw67::pose::placeShot(s, mRunSubject), s.fovDeg);
        mFramePlaced = true;
        return;
    }
    LLPointer<LLImageRaw> raw = new LLImageRaw;
    const S32 w = gViewerWindow->getWindowWidthRaw();
    const S32 h = gViewerWindow->getWindowHeightRaw();
    if (!gViewerWindow->rawSnapshot(raw, w, h, false, false, false /*show_ui*/, false /*show_hud*/, true, false, false))
    {
        finishRun(StatusRole::Error, "The viewer could not take a snapshot for frame " + mFrames.stems[mFrame] + ".");
        return;
    }
    const std::string path = sw67viewer::joinPath(mExportDir, mFrames.stems[mFrame] + ".png");
    LLPointer<LLImagePNG> png = new LLImagePNG;
    if (LLFile::isfile(path) || !png->encode(raw, 0.f) || !png->save(path))
    {
        finishRun(StatusRole::Error, "Could not write " + path + " (existing frames are never overwritten).");
        return;
    }
    ++mWritten;
    ++mFrame;
    mFramePlaced = false;
    if (mFrame % 10 == 0)
    {
        showStatus(StatusRole::Live, llformat("Exporting frame %d of %d...", static_cast<int>(mFrame),
                                              static_cast<int>(mFrames.times.size())));
    }
}

void SW67FloaterDirector::finishRun(StatusRole role, const std::string& text)
{
    mRun = Run::None;
    if (mIdleAdded)
    {
        gIdleCallbacks.deleteFunction(&SW67FloaterDirector::onIdle, this);
        mIdleAdded = false;
    }
    if (mSavedFov > 0.f)
    {
        LLViewerCamera::getInstance()->setDefaultFOV(mSavedFov);
        mSavedFov = 0.f;
    }
    getChild<LLUICtrl>("stop_btn")->setEnabled(false);
    showStatus(role, text);
}

// ---- shot list --------------------------------------------------------------

void SW67FloaterDirector::refreshShots()
{
    LLScrollListCtrl* list = getChild<LLScrollListCtrl>("shot_list");
    list->deleteAllItems();
    for (size_t i = 0; i < mProduction.shots.size(); ++i)
    {
        const ShotEntry& s = mProduction.shots[i];
        const std::string uses = !s.path.empty() ? "path: " + s.path : (!s.angle.empty() ? "angle: " + s.angle : "");
        addRow(list, static_cast<S32>(i),
               {{"done", s.done ? "Done" : "To do"}, {"title", s.title}, {"uses", uses}, {"length", secondsText(s.seconds)}});
    }
    std::string summary = llformat("%d shot(s), %d done, %.0f s planned.", static_cast<int>(mProduction.shots.size()),
                                   static_cast<int>(mProduction.doneCount()), mProduction.plannedSeconds());
    for (const std::string& p : checkShotList(mProduction)) summary += "\n" + p;
    getChild<LLUICtrl>("shot_summary")->setValue(LLSD(summary));
}

void SW67FloaterDirector::loadShot()
{
    const S32 n = selectedIndex(getChild<LLScrollListCtrl>("shot_list"), mProduction.shots.size());
    if (n < 0) return;
    const ShotEntry& s = mProduction.shots[static_cast<size_t>(n)];
    getChild<LLUICtrl>("shot_title")->setValue(LLSD(s.title));
    getChild<LLUICtrl>("shot_uses")->setValue(LLSD(!s.path.empty() ? s.path : s.angle));
    getChild<LLUICtrl>("shot_seconds")->setValue(LLSD(llformat("%.1f", s.seconds)));
    getChild<LLUICtrl>("shot_notes")->setValue(LLSD(s.notes));
}

bool SW67FloaterDirector::shotFromEditor(ShotEntry& entry)
{
    entry.title = getChild<LLUICtrl>("shot_title")->getValue().asString();
    if (entry.title.empty())
    {
        showStatus(StatusRole::Warning, "Give the shot a title.");
        return false;
    }
    const std::string uses = getChild<LLUICtrl>("shot_uses")->getValue().asString();
    entry.path.clear();
    entry.angle.clear();
    if (mProduction.findPath(uses)) entry.path = uses;
    else entry.angle = uses;
    const double sec = std::atof(getChild<LLUICtrl>("shot_seconds")->getValue().asString().c_str());
    entry.seconds = std::clamp(std::isfinite(sec) && sec > 0.0 ? sec : 5.0, 0.1, kMaxPathSeconds);
    entry.notes = getChild<LLUICtrl>("shot_notes")->getValue().asString();
    return true;
}

void SW67FloaterDirector::onAddShot()
{
    ShotEntry e;
    if (mProduction.shots.size() >= kMaxShots || !shotFromEditor(e)) return;
    mProduction.shots.push_back(std::move(e));
    refreshShots();
}

void SW67FloaterDirector::onUpdateShot()
{
    const S32 n = selectedIndex(getChild<LLScrollListCtrl>("shot_list"), mProduction.shots.size());
    if (n < 0) return;
    ShotEntry e = mProduction.shots[static_cast<size_t>(n)];
    if (!shotFromEditor(e)) return;
    mProduction.shots[static_cast<size_t>(n)] = std::move(e);
    refreshShots();
}

void SW67FloaterDirector::onRemoveShot()
{
    const S32 n = selectedIndex(getChild<LLScrollListCtrl>("shot_list"), mProduction.shots.size());
    if (n < 0) return;
    mProduction.shots.erase(mProduction.shots.begin() + n);
    refreshShots();
}

void SW67FloaterDirector::onToggleDone()
{
    const S32 n = selectedIndex(getChild<LLScrollListCtrl>("shot_list"), mProduction.shots.size());
    if (n < 0) return;
    ShotEntry& e = mProduction.shots[static_cast<size_t>(n)];
    e.done = !e.done;
    refreshShots();
}

void SW67FloaterDirector::onFrameShot()
{
    const S32 n = selectedIndex(getChild<LLScrollListCtrl>("shot_list"), mProduction.shots.size());
    if (n < 0) return;
    const ShotEntry& e = mProduction.shots[static_cast<size_t>(n)];
    for (size_t i = 0; i < mProduction.paths.size(); ++i)
    {
        if (mProduction.paths[i].name == e.path)
        {
            mPathIndex = static_cast<int>(i);
            refreshPaths();
            onPlay();
            return;
        }
    }
    const auto subject = SW67FloaterPoseStudio::subjectNow();
    if (const Shot* a = mProduction.findAngle(e.angle))
    {
        if (subject && cameraAllowed())
        {
            applyCamera(sw67::pose::placeShot(*a, *subject), a->fovDeg);
            showStatus(StatusRole::Success, "Camera on \"" + a->name + "\" for \"" + e.title + "\".");
        }
        return;
    }
    showStatus(StatusRole::Info, "This shot names no saved angle or path.");
}

// ---- export -----------------------------------------------------------------

void SW67FloaterDirector::onExport()
{
    const CameraPath* p = currentPath();
    const auto subject = SW67FloaterPoseStudio::subjectNow();
    if (!p || !subject)
    {
        showStatus(StatusRole::Info, "Choose a path to export. Your avatar needs to be in the world.");
        return;
    }
    const auto problems = validate(*p);
    if (!problems.empty())
    {
        showStatus(StatusRole::Warning, problems.front());
        return;
    }
    if (mRun != Run::None || !cameraAllowed()) return;
    const int fps = getChild<LLUICtrl>("fps_combo")->getValue().asInteger();
    const std::string stem = (mProduction.name.empty() ? std::string("production") : mProduction.name) + "_" + p->name;
    mFrames = planFrames(*p, fps > 0 ? fps : 30, stem);
    if (mFrames.times.empty())
    {
        showStatus(StatusRole::Warning, "Nothing to export.");
        return;
    }
    const std::string date = LLDate::now().toHTTPDateString(std::string("%Y%m%d-%H%M%S"));
    mExportDir = sw67viewer::joinPath(sw67viewer::userDir("director"), "exports");
    mExportDir = sw67viewer::joinPath(mExportDir, sw67::photo::safeFileStem(stem) + "-" + date);
    if (!sw67viewer::ensureDir(mExportDir))
    {
        showStatus(StatusRole::Error, "Could not create " + mExportDir + ".");
        return;
    }
    mRunPath = *p;
    mRunSubject = *subject;
    mRunCuts = false; // the file gets the smooth path; reduced motion is for live viewing
    mSavedFov = LLViewerCamera::getInstance()->getDefaultFOV();
    mFrame = 0;
    mFramePlaced = false;
    mWritten = 0;
    mRun = Run::Exporting;
    if (!mIdleAdded)
    {
        gIdleCallbacks.addFunction(&SW67FloaterDirector::onIdle, this);
        mIdleAdded = true;
    }
    getChild<LLUICtrl>("stop_btn")->setEnabled(true);
    showStatus(StatusRole::Live, llformat("Exporting %d frame(s) at %d fps to %s%s", static_cast<int>(mFrames.times.size()),
                                          fps > 0 ? fps : 30, mExportDir.c_str(),
                                          mFrames.capped ? " (capped at ten minutes)." : "."));
}

void SW67FloaterDirector::showStatus(StatusRole role, const std::string& text)
{
    LLTextBox* box = getChild<LLTextBox>("status_text");
    box->setText(sw67viewer::statusText(role) + "  " + text);
    box->setColor(LLUIColor(sw67viewer::statusColor(role)));
}
