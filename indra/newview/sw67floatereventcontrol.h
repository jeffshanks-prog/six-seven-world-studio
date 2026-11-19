/**
 * @file sw67floatereventcontrol.h
 * @brief Event Control Room (DJ & Hosting): the event schedule with
 *        countdowns, the DJ and host team, cue cards, announcement drafts
 *        the host approves before sending, and a check of the parcel's music
 *        stream. Guest lists live in Party Contacts; rehearsal stays in Event
 *        Rehearsal, which cannot send anything.
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
#include "sw67/event/control.h"

#include <string>

class SW67FloaterEventControl final : public LLFloater
{
public:
    AL_VIEW_TYPE(SW67FloaterEventControl, LLFloater);

    SW67FloaterEventControl(const LLSD& key);
    bool postBuild() override;
    void onOpen(const LLSD& key) override;
    void draw() override;

private:
    ~SW67FloaterEventControl() override = default;

    sw67::event::ScheduledEvent* current();
    void refreshEvents();
    void loadEvent();
    void refreshSection();
    void refreshTeam();
    void refreshCards();
    void refreshAnnouncements();
    void refreshCountdown();
    int selectedRow(const char* list) const;
    void commit(const std::string& message);

    void onNew();
    void onRemove();
    void onSave();
    void onAddMember();
    void onRemoveMember();
    void onCard(bool add);
    void onRemoveCard();
    void onSelectCard();
    void onNewAnnouncement();
    void onSelectAnnouncement();
    void onUpdateAnnouncement();
    void onApprove();
    void onSendChat();
    void onToParty();
    void onStreamCheck();
    void onSendToPhone();
    void onPairPhone();
    void onPhoneRsvps(bool quiet = false);
    void onRsvpLink();
    void onTakeOffPhone();
    void phoneText(const std::string& text);
    void showStatus(sw67::a11y::StatusRole role, const std::string& text);

    std::string mEventId;
    LLFrameTimer mTick;
    LLFrameTimer mRsvpTimer;      ///< Automatic RSVP checks, every kRsvpEvery seconds.
    bool mRsvpBusy = false;       ///< A check is on its way; don't start another.
    int mLastRsvpCount = -1;      ///< What the last automatic check saw, so only changes are reported.
};
