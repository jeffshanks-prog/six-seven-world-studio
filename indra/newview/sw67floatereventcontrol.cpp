/**
 * @file sw67floatereventcontrol.cpp
 * @brief Event Control Room floater.
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

#include "sw67floatereventcontrol.h"

#include "sw67accessibility.h"
#include "sw67kingjahmir.h"
#include "sw67partycontacts.h"
#include "sw67rlvstate.h"

#include "llagent.h"
#include "llaudioengine.h"
#include "llavatarnamecache.h"
#include "llcombobox.h"
#include "llfloaterimnearbychat.h"
#include "llfloaterreg.h"
#include "llnotificationsutil.h"
#include "llparcel.h"
#include "llscrolllistctrl.h"
#include "lltextbox.h"
#include "lluictrl.h"
#include "llviewercontrol.h"
#include "llviewerparcelmgr.h"
#include "roles_constants.h"

#include "sw67/contacts/party.h"
#include "sw67/io/json.h"

#include <algorithm>
#include <functional>
#include <initializer_list>
#include <utility>

using namespace sw67::event;
using sw67::a11y::StatusRole;
namespace events = sw67viewer::events;
namespace party = sw67viewer::party;
namespace kingjahmir = sw67viewer::kingjahmir;

namespace
{
const char* kDetails[] = {"name_label", "event_name", "start_label", "start_time", "minutes_label", "duration",
                          "location_label", "location", "stream_label", "stream_url", "notes_label", "notes", "save_btn"};
const char* kTeam[] = {"team_list", "member_name", "member_id", "role_combo", "add_member_btn", "remove_member_btn"};
const char* kCards[] = {"card_list", "card_title", "card_minute", "card_body", "add_card_btn", "update_card_btn",
                        "remove_card_btn", "due_text"};
const char* kAnn[] = {"ann_list", "ann_text", "new_ann_btn", "update_ann_btn", "approve_btn", "send_chat_btn", "to_party_btn"};
const char* kStream[] = {"stream_check_btn", "about_land_btn", "stream_report"};
const char* kPhone[] = {"phone_intro", "shots_label", "shots_editor", "send_phone_btn", "pair_btn", "rsvp_btn",
                        "unphone_btn", "phone_text"};

void addRow(LLScrollListCtrl* list, const LLSD& value, std::initializer_list<std::pair<const char*, std::string>> cols)
{
    LLSD row;
    row["value"] = value;
    S32 n = 0;
    for (const auto& [col, val] : cols)
    {
        row["columns"][n]["column"] = col;
        row["columns"][n]["value"] = val;
        ++n;
    }
    list->addElement(row);
}

std::string oneLine(std::string s, size_t n = 80)
{
    for (char& c : s)
    {
        if (c == '\n' || c == '\r') c = ' ';
    }
    if (s.size() > n) s = s.substr(0, n) + "...";
    return s;
}
} // namespace

SW67FloaterEventControl::SW67FloaterEventControl(const LLSD& key)
:   LLFloater(key)
{
}

bool SW67FloaterEventControl::postBuild()
{
    auto bind = [this](const char* name, std::function<void()> fn) {
        getChild<LLUICtrl>(name)->setCommitCallback([fn](LLUICtrl*, const LLSD&) { fn(); });
    };
    bind("event_list", [this] {
        if (LLScrollListItem* it = getChild<LLScrollListCtrl>("event_list")->getFirstSelected())
        {
            mEventId = it->getValue().asString();
            loadEvent();
        }
    });
    bind("new_btn", [this] { onNew(); });
    bind("remove_btn", [this] { onRemove(); });
    bind("save_btn", [this] { onSave(); });
    bind("guests_btn", [this] {
        if (!current()) return;
        LLSD key;
        key["view"] = "guests";
        key["event"] = mEventId;
        LLFloaterReg::showInstance("sw67_party_contacts", key);
    });
    bind("rehearse_btn", [] { LLFloaterReg::showInstance("sw67_event_rehearsal"); });
    bind("sound_check_btn", [] { LLFloaterReg::showInstance("sw67_sound_check"); });
    bind("section_combo", [this] { refreshSection(); });
    bind("add_member_btn", [this] { onAddMember(); });
    bind("remove_member_btn", [this] { onRemoveMember(); });
    bind("card_list", [this] { onSelectCard(); });
    bind("add_card_btn", [this] { onCard(true); });
    bind("update_card_btn", [this] { onCard(false); });
    bind("remove_card_btn", [this] { onRemoveCard(); });
    bind("ann_list", [this] { onSelectAnnouncement(); });
    bind("new_ann_btn", [this] { onNewAnnouncement(); });
    bind("update_ann_btn", [this] { onUpdateAnnouncement(); });
    bind("approve_btn", [this] { onApprove(); });
    bind("send_chat_btn", [this] { onSendChat(); });
    bind("to_party_btn", [this] { onToParty(); });
    bind("stream_check_btn", [this] { onStreamCheck(); });
    bind("about_land_btn", [] { LLFloaterReg::showInstance("about_land"); });
    bind("send_phone_btn", [this] { onSendToPhone(); });
    bind("pair_btn", [this] { onPairPhone(); });
    bind("rsvp_btn", [this] { onPhoneRsvps(); });
    bind("unphone_btn", [this] { onTakeOffPhone(); });
    return LLFloater::postBuild();
}

void SW67FloaterEventControl::onOpen(const LLSD& key)
{
    LLFloater::onOpen(key);
    refreshEvents();
    if (mEventId.empty())
    {
        const auto order = ordered(events::schedule(), events::nowEpoch());
        if (!order.empty()) mEventId = order.front()->id;
    }
    loadEvent();
    refreshSection();
}

void SW67FloaterEventControl::draw()
{
    if (mTick.getElapsedTimeF32() >= 1.f)
    {
        mTick.reset();
        refreshCountdown();
    }
    LLFloater::draw();
}

ScheduledEvent* SW67FloaterEventControl::current()
{
    return events::schedule().find(mEventId);
}

int SW67FloaterEventControl::selectedRow(const char* list) const
{
    LLScrollListItem* it = getChild<LLScrollListCtrl>(list)->getFirstSelected();
    return it ? it->getValue().asInteger() : -1;
}

void SW67FloaterEventControl::commit(const std::string& message)
{
    if (events::saveSchedule()) showStatus(StatusRole::Success, message);
    else showStatus(StatusRole::Error, "Could not save the schedule file.");
}

void SW67FloaterEventControl::refreshEvents()
{
    LLScrollListCtrl* list = getChild<LLScrollListCtrl>("event_list");
    list->deleteAllItems();
    const std::int64_t now = events::nowEpoch();
    for (const ScheduledEvent* e : ordered(events::schedule(), now))
    {
        addRow(list, LLSD(e->id), {{"name", e->name}, {"when", events::localFromEpoch(e->startEpoch)}, {"countdown", countdown(*e, now).text}});
    }
    list->selectByValue(LLSD(mEventId));
}

void SW67FloaterEventControl::loadEvent()
{
    const ScheduledEvent* e = current();
    const bool have = e != nullptr;
    for (const char* n : {"remove_btn", "guests_btn", "save_btn", "add_member_btn", "add_card_btn", "new_ann_btn"})
    {
        getChild<LLUICtrl>(n)->setEnabled(have);
    }
    getChild<LLUICtrl>("event_name")->setValue(LLSD(have ? e->name : std::string()));
    getChild<LLUICtrl>("start_time")->setValue(LLSD(have ? events::localFromEpoch(e->startEpoch) : std::string()));
    getChild<LLUICtrl>("duration")->setValue(LLSD(have ? std::to_string(e->durationMinutes) : std::string()));
    getChild<LLUICtrl>("location")->setValue(LLSD(have ? e->location : std::string()));
    getChild<LLUICtrl>("stream_url")->setValue(LLSD(have ? e->streamUrl : std::string()));
    getChild<LLUICtrl>("notes")->setValue(LLSD(have ? e->notes : std::string()));
    getChild<LLUICtrl>("shots_editor")->setValue(LLSD(have ? shotsToText(e->shots) : std::string()));
    for (const char* n : {"send_phone_btn", "rsvp_btn", "unphone_btn", "shots_editor"})
    {
        getChild<LLUICtrl>(n)->setEnabled(have);
    }
    refreshTeam();
    refreshCards();
    refreshAnnouncements();
    refreshCountdown();
}

void SW67FloaterEventControl::refreshSection()
{
    const std::string s = getChild<LLComboBox>("section_combo")->getValue().asString();
    for (const char* n : kDetails) getChild<LLView>(n)->setVisible(s == "details");
    for (const char* n : kTeam) getChild<LLView>(n)->setVisible(s == "team");
    for (const char* n : kCards) getChild<LLView>(n)->setVisible(s == "cards");
    for (const char* n : kAnn) getChild<LLView>(n)->setVisible(s == "announcements");
    for (const char* n : kStream) getChild<LLView>(n)->setVisible(s == "stream");
    for (const char* n : kPhone) getChild<LLView>(n)->setVisible(s == "phone");
}

void SW67FloaterEventControl::refreshTeam()
{
    LLScrollListCtrl* list = getChild<LLScrollListCtrl>("team_list");
    list->deleteAllItems();
    if (const ScheduledEvent* e = current())
    {
        for (size_t i = 0; i < e->team.size(); ++i)
        {
            const TeamMember& m = e->team[i];
            addRow(list, LLSD(static_cast<S32>(i)), {{"name", m.name}, {"role", std::string(roleName(m.role))}, {"id", m.accountId}});
        }
    }
}

void SW67FloaterEventControl::refreshCards()
{
    LLScrollListCtrl* list = getChild<LLScrollListCtrl>("card_list");
    list->deleteAllItems();
    const ScheduledEvent* e = current();
    if (!e) return;
    for (size_t i = 0; i < e->cards.size(); ++i)
    {
        const CueCard& c = e->cards[i];
        addRow(list, LLSD(static_cast<S32>(i)),
               {{"minute", c.atMinute < 0 ? std::string("any") : std::to_string(c.atMinute)}, {"title", c.title}, {"body", oneLine(c.body)}});
    }
}

void SW67FloaterEventControl::refreshAnnouncements()
{
    LLScrollListCtrl* list = getChild<LLScrollListCtrl>("ann_list");
    const int keep = selectedRow("ann_list");
    list->deleteAllItems();
    const ScheduledEvent* e = current();
    bool canSendSel = false;
    if (e)
    {
        for (size_t i = 0; i < e->announcements.size(); ++i)
        {
            const Announcement& a = e->announcements[i];
            addRow(list, LLSD(static_cast<S32>(i)), {{"state", std::string(stateName(a.state))}, {"text", oneLine(a.text, 120)}});
        }
        list->selectByValue(LLSD(keep));
        if (keep >= 0 && static_cast<size_t>(keep) < e->announcements.size()) canSendSel = canSend(e->announcements[static_cast<size_t>(keep)]);
    }
    getChild<LLUICtrl>("send_chat_btn")->setEnabled(canSendSel);
}

void SW67FloaterEventControl::refreshCountdown()
{
    const ScheduledEvent* e = current();
    const std::int64_t now = events::nowEpoch();
    if (!e)
    {
        getChild<LLUICtrl>("countdown_text")->setValue(LLSD("No event chosen. Press New event to plan one."));
        return;
    }
    const Countdown c = countdown(*e, now);
    const char* tag = c.phase == Countdown::Phase::Live ? "[LIVE] " : c.phase == Countdown::Phase::Ended ? "[ENDED] " : "[UPCOMING] ";
    getChild<LLUICtrl>("countdown_text")->setValue(LLSD(std::string(tag) + e->name + ": " + c.text));
    std::string due;
    if (c.phase == Countdown::Phase::Live)
    {
        for (const CueCard* card : cardsDue(*e, now))
        {
            due += (due.empty() ? "Coming up: " : "; ") + (card->atMinute < 0 ? std::string("any time") : "minute " + std::to_string(card->atMinute)) +
                   " - " + card->title;
        }
    }
    getChild<LLUICtrl>("due_text")->setValue(LLSD(due));
}

void SW67FloaterEventControl::onNew()
{
    Schedule& s = events::schedule();
    if (s.events.size() >= kMaxEvents)
    {
        showStatus(StatusRole::Warning, "The schedule holds up to 200 events. Remove old ones first.");
        return;
    }
    ScheduledEvent e;
    // Next whole hour, a day from now.
    e.startEpoch = (events::nowEpoch() / 3600 + 24) * 3600;
    e.id = newEventId(s, e.startEpoch);
    e.name = "New event";
    s.events.push_back(e);
    mEventId = e.id;
    refreshEvents();
    loadEvent();
    getChild<LLComboBox>("section_combo")->setValue(LLSD("details"));
    refreshSection();
    commit("Added. Fill in the details and press Save event.");
}

void SW67FloaterEventControl::onRemove()
{
    const ScheduledEvent* e = current();
    if (!e) return;
    LLSD args;
    args["MESSAGE"] = "Remove \"" + e->name + "\" from your schedule? Its team, cue cards and announcement drafts go "
                      "with it. Its guest list stays in Party Contacts.";
    const LLHandle<LLFloater> handle = getHandle();
    const std::string id = mEventId;
    LLNotificationsUtil::add("GenericAlertYesCancel", args, LLSD(), [handle, id](const LLSD& n, const LLSD& r) {
        if (LLNotificationsUtil::getSelectedOption(n, r) != 0) return;
        auto& ev = events::schedule().events;
        ev.erase(std::remove_if(ev.begin(), ev.end(), [&](const ScheduledEvent& x) { return x.id == id; }), ev.end());
        events::saveSchedule();
        if (auto* self = ALViewType::as<SW67FloaterEventControl>(handle.get()))
        {
            self->mEventId.clear();
            self->refreshEvents();
            self->loadEvent();
            self->showStatus(StatusRole::Success, "Removed from the schedule.");
        }
    });
}

void SW67FloaterEventControl::onSave()
{
    ScheduledEvent* e = current();
    if (!e) return;
    const std::string name = getChild<LLUICtrl>("event_name")->getValue().asString();
    const auto start = events::epochFromLocal(getChild<LLUICtrl>("start_time")->getValue().asString());
    int minutes = 0;
    const std::string m = getChild<LLUICtrl>("duration")->getValue().asString();
    if (!m.empty() && m.find_first_not_of("0123456789") == std::string::npos) minutes = std::stoi(m);
    if (name.find_first_not_of(" \t") == std::string::npos || !start || minutes < 1 || minutes > 24 * 60)
    {
        showStatus(StatusRole::Warning, "Give the event a name, a start like 2026-11-20 21:00 (your time) and 1 to 1440 minutes.");
        return;
    }
    const std::string stream = getChild<LLUICtrl>("stream_url")->getValue().asString();
    if (!stream.empty() && !looksLikeStreamUrl(stream))
    {
        showStatus(StatusRole::Warning, "The planned stream must be an http or https address.");
        return;
    }
    e->name = name.substr(0, 120);
    e->startEpoch = *start;
    e->durationMinutes = minutes;
    e->location = getChild<LLUICtrl>("location")->getValue().asString().substr(0, 300);
    e->streamUrl = stream;
    e->notes = getChild<LLUICtrl>("notes")->getValue().asString().substr(0, 4000);
    auto& gl = sw67viewer::party::book().guestLists;
    if (auto it = gl.find(e->id); it != gl.end() && it->second.ownerId == sw67viewer::party::selfId())
    {
        it->second.eventName = e->name;
        sw67viewer::party::save();
    }
    refreshEvents();
    commit("Saved.");
}

void SW67FloaterEventControl::onAddMember()
{
    ScheduledEvent* e = current();
    if (!e) return;
    const std::string name = getChild<LLUICtrl>("member_name")->getValue().asString();
    const auto id = sw67::contacts::findAccountId(getChild<LLUICtrl>("member_id")->getValue().asString());
    if (name.find_first_not_of(" \t") == std::string::npos)
    {
        showStatus(StatusRole::Info, "Type the team member's name.");
        return;
    }
    if (e->team.size() >= 50) return;
    e->team.push_back({id.value_or(""), name.substr(0, 120),
                       parseRole(getChild<LLComboBox>("role_combo")->getValue().asString()).value_or(Role::Host)});
    getChild<LLUICtrl>("member_name")->setValue(LLSD(""));
    getChild<LLUICtrl>("member_id")->setValue(LLSD(""));
    refreshTeam();
    commit("Added to the team. Nobody is messaged; tell them yourself or share the guest list from Party Contacts.");
}

void SW67FloaterEventControl::onRemoveMember()
{
    ScheduledEvent* e = current();
    const int i = selectedRow("team_list");
    if (!e || i < 0 || static_cast<size_t>(i) >= e->team.size()) return;
    e->team.erase(e->team.begin() + i);
    refreshTeam();
    commit("Removed from the team.");
}

void SW67FloaterEventControl::onSelectCard()
{
    const ScheduledEvent* e = current();
    const int i = selectedRow("card_list");
    if (!e || i < 0 || static_cast<size_t>(i) >= e->cards.size()) return;
    const CueCard& c = e->cards[static_cast<size_t>(i)];
    getChild<LLUICtrl>("card_title")->setValue(LLSD(c.title));
    getChild<LLUICtrl>("card_minute")->setValue(LLSD(c.atMinute < 0 ? std::string() : std::to_string(c.atMinute)));
    getChild<LLUICtrl>("card_body")->setValue(LLSD(c.body));
}

void SW67FloaterEventControl::onCard(bool add)
{
    ScheduledEvent* e = current();
    if (!e) return;
    CueCard c;
    c.title = getChild<LLUICtrl>("card_title")->getValue().asString().substr(0, 120);
    c.body = getChild<LLUICtrl>("card_body")->getValue().asString().substr(0, 2000);
    const std::string m = getChild<LLUICtrl>("card_minute")->getValue().asString();
    if (!m.empty())
    {
        if (m.size() > 4 || m.find_first_not_of("0123456789") != std::string::npos)
        {
            showStatus(StatusRole::Warning, "The minute is a whole number from the start, or blank for any time.");
            return;
        }
        c.atMinute = std::stoi(m);
    }
    if (c.title.find_first_not_of(" \t") == std::string::npos)
    {
        showStatus(StatusRole::Info, "Give the card a title.");
        return;
    }
    if (add)
    {
        if (e->cards.size() >= 200) return;
        e->cards.push_back(c);
    }
    else
    {
        const int i = selectedRow("card_list");
        if (i < 0 || static_cast<size_t>(i) >= e->cards.size()) return;
        e->cards[static_cast<size_t>(i)] = c;
    }
    refreshCards();
    commit(add ? "Card added." : "Card updated.");
}

void SW67FloaterEventControl::onRemoveCard()
{
    ScheduledEvent* e = current();
    const int i = selectedRow("card_list");
    if (!e || i < 0 || static_cast<size_t>(i) >= e->cards.size()) return;
    e->cards.erase(e->cards.begin() + i);
    refreshCards();
    commit("Card removed.");
}

void SW67FloaterEventControl::onSelectAnnouncement()
{
    const ScheduledEvent* e = current();
    const int i = selectedRow("ann_list");
    if (!e || i < 0 || static_cast<size_t>(i) >= e->announcements.size()) return;
    const Announcement& a = e->announcements[static_cast<size_t>(i)];
    getChild<LLUICtrl>("ann_text")->setValue(LLSD(a.text));
    getChild<LLUICtrl>("send_chat_btn")->setEnabled(canSend(a));
    getChild<LLUICtrl>("approve_btn")->setEnabled(a.state != Announcement::State::Sent);
    getChild<LLUICtrl>("update_ann_btn")->setEnabled(a.state != Announcement::State::Sent);
}

void SW67FloaterEventControl::onNewAnnouncement()
{
    ScheduledEvent* e = current();
    if (!e || e->announcements.size() >= 100) return;
    Announcement a;
    editText(a, getChild<LLUICtrl>("ann_text")->getValue().asString());
    e->announcements.push_back(a);
    refreshAnnouncements();
    getChild<LLScrollListCtrl>("ann_list")->selectByValue(LLSD(static_cast<S32>(e->announcements.size() - 1)));
    onSelectAnnouncement();
    commit("Draft saved. Approve it when the text is final.");
}

void SW67FloaterEventControl::onUpdateAnnouncement()
{
    ScheduledEvent* e = current();
    const int i = selectedRow("ann_list");
    if (!e || i < 0 || static_cast<size_t>(i) >= e->announcements.size()) return;
    Announcement& a = e->announcements[static_cast<size_t>(i)];
    const bool wasApproved = a.state == Announcement::State::Approved;
    editText(a, getChild<LLUICtrl>("ann_text")->getValue().asString());
    refreshAnnouncements();
    onSelectAnnouncement();
    commit(wasApproved && a.state == Announcement::State::Draft ? "Text changed, so it is a draft again. Approve it to send."
                                                                : "Saved.");
}

void SW67FloaterEventControl::onApprove()
{
    ScheduledEvent* e = current();
    const int i = selectedRow("ann_list");
    if (!e || i < 0 || static_cast<size_t>(i) >= e->announcements.size()) return;
    Announcement& a = e->announcements[static_cast<size_t>(i)];
    editText(a, getChild<LLUICtrl>("ann_text")->getValue().asString());
    const sw67::event::Result r = approve(a);
    refreshAnnouncements();
    onSelectAnnouncement();
    if (r.ok) commit(r.message);
    else showStatus(StatusRole::Warning, r.message);
}

void SW67FloaterEventControl::onSendChat()
{
    ScheduledEvent* e = current();
    const int i = selectedRow("ann_list");
    if (!e || i < 0 || static_cast<size_t>(i) >= e->announcements.size()) return;
    const Announcement& a = e->announcements[static_cast<size_t>(i)];
    if (!canSend(a))
    {
        showStatus(StatusRole::Warning, "Approve this exact text first.");
        return;
    }
    LLSD args;
    args["MESSAGE"] = "Say this once in nearby chat now?\n\n" + a.text;
    const LLHandle<LLFloater> handle = getHandle();
    const std::string id = mEventId;
    const size_t index = static_cast<size_t>(i);
    const std::string text = a.text;
    LLNotificationsUtil::add("GenericAlertYesCancel", args, LLSD(), [handle, id, index, text](const LLSD& n, const LLSD& r) {
        if (LLNotificationsUtil::getSelectedOption(n, r) != 0) return;
        ScheduledEvent* ev = events::schedule().find(id);
        if (!ev || index >= ev->announcements.size() || !canSend(ev->announcements[index]) || ev->announcements[index].text != text)
        {
            return; // Changed while the question was open: send nothing.
        }
        sw67::rlv::ActionRequest req;
        req.kind = sw67::rlv::ActionKind::SendChat;
        req.channel = 0;
        const sw67::rlv::Decision d = SW67RlvState::check(req);
        auto* self = ALViewType::as<SW67FloaterEventControl>(handle.get());
        if (!d.allowed())
        {
            if (self) self->showStatus(StatusRole::Restricted, "Not sent: " + d.explanation);
            return;
        }
        LLFloaterIMNearbyChat::sendChatFromViewer(text, CHAT_TYPE_NORMAL, false);
        markSent(ev->announcements[index], sw67viewer::party::nowUtc());
        events::saveSchedule();
        if (self)
        {
            self->refreshAnnouncements();
            self->showStatus(StatusRole::Success, "Said in nearby chat. It is marked Sent and can't be sent again.");
        }
    });
}

void SW67FloaterEventControl::onToParty()
{
    LLSD key;
    key["view"] = "contacts";
    key["text"] = getChild<LLUICtrl>("ann_text")->getValue().asString();
    key["purpose"] = "announcement";
    LLFloaterReg::showInstance("sw67_party_contacts", key);
}

void SW67FloaterEventControl::onStreamCheck()
{
    StreamInputs in;
    LLParcel* parcel = LLViewerParcelMgr::getInstance()->getAgentParcel();
    in.onParcel = parcel != nullptr;
    if (parcel)
    {
        in.parcelMusicUrl = parcel->getMusicURL();
        in.canSetParcelMusic = LLViewerParcelMgr::isParcelModifiableByAgent(parcel, GP_LAND_CHANGE_MEDIA);
    }
    in.musicEnabled = gSavedSettings.getBOOL("AudioStreamingMusic");
    in.musicPlaying = gAudiop && gAudiop->isInternetStreamPlaying() == LLAudioEngine::AUDIO_PLAYING;
    in.musicVolume = gSavedSettings.getF32("AudioLevelMusic");
    if (const ScheduledEvent* e = current()) in.plannedUrl = e->streamUrl;
    std::string report;
    for (const StreamFinding& f : checkStream(in))
    {
        report += sw67viewer::statusText(f.role) + "  " + f.text + "\n";
    }
    getChild<LLUICtrl>("stream_report")->setValue(LLSD(report));
    showStatus(StatusRole::Info, "Checked. Nothing was changed.");
}

void SW67FloaterEventControl::showStatus(StatusRole role, const std::string& text)
{
    LLTextBox* box = getChild<LLTextBox>("status_text");
    box->setText(sw67viewer::statusText(role) + "  " + text);
    box->setColor(LLUIColor(sw67viewer::statusColor(role)));
}

// ---- Phone Companion ------------------------------------------------------
namespace
{
std::string hostName()
{
    LLAvatarName name;
    return LLAvatarNameCache::get(gAgentID, &name) ? name.getDisplayName() : std::string();
}

std::string phonePath(const std::string& eventId, const char* tail)
{
    // Event ids are "evt-<start>-<n>": safe in a path as they are.
    return "/v1/phone/events/" + eventId + tail;
}
} // namespace

void SW67FloaterEventControl::phoneText(const std::string& text)
{
    getChild<LLTextBox>("phone_text")->setText(text);
}

void SW67FloaterEventControl::onSendToPhone()
{
    ScheduledEvent* e = current();
    if (!e) return;
    e->shots = shotsFromText(getChild<LLUICtrl>("shots_editor")->getValue().asString());
    getChild<LLUICtrl>("shots_editor")->setValue(LLSD(shotsToText(e->shots)));
    if (!events::saveSchedule())
    {
        showStatus(StatusRole::Error, "Could not save the schedule file.");
        return;
    }
    const LLHandle<LLFloater> handle = getHandle();
    const std::string name = e->name;
    std::string whyNot;
    const bool started = kingjahmir::callGateway(
        "POST", "/v1/phone/events", sw67::io::writeJson(phonePayload(*e, hostName())),
        [handle, name](const kingjahmir::GatewayReply& r) {
            auto* self = ALViewType::as<SW67FloaterEventControl>(handle.get());
            if (!self) return;
            if (!r.ok)
            {
                self->showStatus(StatusRole::Error, r.error);
                return;
            }
            const auto j = sw67::io::parseJson(r.body);
            const int shots = j ? (*j)["shots"].asInt(0) : 0;
            self->showStatus(StatusRole::Success, "\"" + name + "\" is on the phone with " + std::to_string(shots) +
                                                      " shot(s). Pair a phone if you haven't yet.");
        },
        whyNot);
    showStatus(started ? StatusRole::Info : StatusRole::Warning,
               started ? std::string("Shot list saved. Sending to the phone...") : "Shot list saved. " + whyNot);
}

void SW67FloaterEventControl::onPairPhone()
{
    const LLHandle<LLFloater> handle = getHandle();
    sw67::io::Json body = sw67::io::Json::object();
    body.set("host_name", sw67::io::Json(hostName()));
    std::string whyNot;
    const bool started = kingjahmir::callGateway(
        "POST", "/v1/phone/pair", sw67::io::writeJson(body),
        [handle](const kingjahmir::GatewayReply& r) {
            auto* self = ALViewType::as<SW67FloaterEventControl>(handle.get());
            if (!self) return;
            if (!r.ok)
            {
                self->showStatus(StatusRole::Error, r.error);
                return;
            }
            const auto j = sw67::io::parseJson(r.body);
            const std::string code = j ? (*j)["code"].asString("") : std::string();
            const std::string url = j ? (*j)["url"].asString("") : std::string();
            if (code.empty())
            {
                self->showStatus(StatusRole::Error, "The gateway sent an answer this viewer could not read.");
                return;
            }
            self->phoneText("On your phone, open " + url + " and type " + code +
                            ". It works once, for 10 minutes. Don't share it.");
            self->showStatus(StatusRole::Success, "Pairing code ready.");
        },
        whyNot);
    if (!started) showStatus(StatusRole::Warning, whyNot);
}

void SW67FloaterEventControl::onPhoneRsvps()
{
    const ScheduledEvent* e = current();
    if (!e) return;
    const LLHandle<LLFloater> handle = getHandle();
    const std::string id = e->id;
    const std::string name = e->name;
    std::string whyNot;
    const bool started = kingjahmir::callGateway(
        "GET", phonePath(id, "/rsvps"), std::string(),
        [handle, id, name](const kingjahmir::GatewayReply& r) {
            auto* self = ALViewType::as<SW67FloaterEventControl>(handle.get());
            if (!self) return;
            if (!r.ok)
            {
                self->showStatus(StatusRole::Error, r.status == 404 ? std::string("This event isn't on the phone yet: press "
                                                                                 "Save and send to phone first.")
                                                                     : r.error);
                return;
            }
            const auto root = sw67::io::parseJson(r.body);
            if (!root || (*root)["format"].asString("") != "sw67.rsvps")
            {
                self->showStatus(StatusRole::Error, "The gateway sent an answer this viewer could not read.");
                return;
            }
            sw67::contacts::Book& book = party::book();
            sw67::contacts::GuestList& gl = sw67::contacts::guestListFor(book, id, name, party::selfId());
            int accepted = 0;
            int skipped = 0;
            for (const sw67::io::Json& j : (*root)["rsvps"].items())
            {
                sw67::contacts::RsvpRecord rec;
                rec.accountId = j["id"].asString("");
                rec.displayName = j["name"].asString("").substr(0, 120);
                rec.answer = sw67::contacts::parseRsvp(j["answer"].asString("")).value_or(sw67::contacts::Rsvp::Going);
                if (j["announcements"].isBool()) rec.wantsAnnouncements = j["announcements"].asBool();
                (sw67::contacts::acceptRsvp(book, gl, rec, party::nowUtc()).ok ? accepted : skipped)++;
            }
            party::save();
            self->showStatus(StatusRole::Success,
                             std::to_string(accepted) + " RSVP(s) added to the guest list" +
                                 (skipped ? ", " + std::to_string(skipped) + " skipped" : std::string()) +
                                 ". Guests typed these themselves; they aren't verified. Nothing was sent to anyone.");
        },
        whyNot);
    if (!started) showStatus(StatusRole::Warning, whyNot);
}

void SW67FloaterEventControl::onTakeOffPhone()
{
    const ScheduledEvent* e = current();
    if (!e) return;
    LLSD args;
    args["MESSAGE"] = "Take \"" + e->name + "\" off the Phone Companion? Its team chat, team invites and the RSVPs "
                      "on the phone are deleted. Get the RSVPs first if you want them. Your schedule here doesn't change.";
    const LLHandle<LLFloater> handle = getHandle();
    const std::string id = e->id;
    LLNotificationsUtil::add("GenericAlertYesCancel", args, LLSD(), [handle, id](const LLSD& n, const LLSD& r) {
        if (LLNotificationsUtil::getSelectedOption(n, r) != 0) return;
        std::string whyNot;
        const bool started = kingjahmir::callGateway(
            "POST", phonePath(id, "/delete"), "{}",
            [handle](const kingjahmir::GatewayReply& reply) {
                auto* self = ALViewType::as<SW67FloaterEventControl>(handle.get());
                if (!self) return;
                if (reply.ok) self->showStatus(StatusRole::Success, "Taken off the phone.");
                else self->showStatus(StatusRole::Error, reply.status == 404 ? std::string("It wasn't on the phone.") : reply.error);
            },
            whyNot);
        auto* self = ALViewType::as<SW67FloaterEventControl>(handle.get());
        if (!started && self) self->showStatus(StatusRole::Warning, whyNot);
    });
}
