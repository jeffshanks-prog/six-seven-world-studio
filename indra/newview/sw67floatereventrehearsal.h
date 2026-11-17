/**
 * @file sw67floatereventrehearsal.h
 * @brief Event Rehearsal Mode: edit an event's run sheet, check it, and play
 *        it through as a rehearsal that shows what would happen and when.
 *
 *        This window cannot send, broadcast, invite or pay: it uses only
 *        sw67::event, which describes effects and has no way to act. It does
 *        not include the viewer's chat, IM, payment or media code.
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

#include "sw67/a11y/profile.h"
#include "sw67/event/rehearsal.h"

#include <memory>
#include <string>

class SW67FloaterEventRehearsal final : public LLFloater
{
public:
    AL_VIEW_TYPE(SW67FloaterEventRehearsal, LLFloater);

    SW67FloaterEventRehearsal(const LLSD& key);
    bool postBuild() override;
    void onOpen(const LLSD& key) override;
    void draw() override;

private:
    ~SW67FloaterEventRehearsal() override = default;

    void planFromControls();
    void controlsFromPlan();
    void refreshCues();
    void loadCue();
    bool cueFromEditor(sw67::event::Cue& cue);
    void onAddCue();
    void onUpdateCue();
    void onRemoveCue();
    void onPickPerson();
    void onCheck();
    void onRun();
    void onStopRun();
    void onSave();
    void onLoad();
    void onNew();
    void refreshPlans();
    void showStatus(sw67::a11y::StatusRole role, const std::string& text);
    static std::string eventsDir();

    sw67::event::EventPlan mPlan;
    std::unique_ptr<sw67::event::Rehearsal> mRun;
    LLFrameTimer mClock;
    float mSpeed = 1.f;
    std::string mRunLog;
};
