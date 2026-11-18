/**
 * @file sw67floaterpartycontacts.cpp
 * @brief Party Contacts floater.
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

#include "sw67floaterpartycontacts.h"

#include "sw67accessibility.h"
#include "sw67partycontacts.h"
#include "sw67viewerfiles.h"

#include "llavatarnamecache.h"
#include "llcallingcard.h"
#include "llcombobox.h"
#include "llfilepicker.h"
#include "llnotificationsutil.h"
#include "llscrolllistctrl.h"
#include "lltexteditor.h"
#include "lltextbox.h"
#include "lluictrl.h"
#include "llviewermenufile.h"

#include "sw67/io/json.h"

#include <algorithm>
#include <functional>

using namespace sw67::contacts;
using sw67::a11y::StatusRole;
namespace party = sw67viewer::party;

namespace
{
const char* kFriendsOnly[] = {"save_btn"};
const char* kContactsOnly[] = {"on_list_btn", "off_list_btn", "to_guests_btn", "remove_contact_btn", "consent_combo",
                               "set_consent_btn", "add_id", "add_btn"};
const char* kGuestsOnly[] = {"rsvp_combo", "set_rsvp_btn", "list_to_guests_btn", "remove_guest_btn", "rsvp_file_btn",
                             "export_share_btn", "import_share_btn", "share_id", "share_edit_check",
                             "share_invite_check", "share_btn", "unshare_btn"};
const char* kListControls[] = {"new_list_name", "new_list_btn", "remove_list_btn"};

std::string lower(std::string s)
{
    LLStringUtil::toLower(s);
    return s;
}

void addRow(LLScrollListCtrl* list, const std::string& id, const std::string& a, const std::string& b,
            const std::string& c)
{
    LLSD row;
    row["value"] = id;
    const char* cols[] = {"name", "detail", "extra"};
    const std::string vals[] = {a, b, c};
    for (S32 n = 0; n < 3; ++n)
    {
        row["columns"][n]["column"] = cols[n];
        row["columns"][n]["value"] = vals[n];
    }
    list->addElement(row);
}

std::string consentName(Announcements a)
{
    switch (a)
    {
        case Announcements::OptedIn: return "Opted in";
        case Announcements::OptedOut: return "Opted out";
        default: return "Not asked";
    }
}

std::string sourceName(Source s)
{
    switch (s)
    {
        case Source::Friend: return "from friends";
        case Source::People: return "from People";
        case Source::Rsvp: return "from an RSVP";
        default: return "added by hand";
    }
}
} // namespace

SW67FloaterPartyContacts::SW67FloaterPartyContacts(const LLSD& key)
:   LLFloater(key)
{
}

SW67FloaterPartyContacts::~SW67FloaterPartyContacts() = default;

bool SW67FloaterPartyContacts::postBuild()
{
    auto bind = [this](const char* name, std::function<void()> fn) {
        getChild<LLUICtrl>(name)->setCommitCallback([fn](LLUICtrl*, const LLSD&) { fn(); });
    };
    bind("view_combo", [this] { onViewChanged(); });
    bind("list_combo", [this] { invalidatePreview(); refreshList(); });
    bind("event_combo", [this] { mLeftOut.clear(); invalidatePreview(); refreshList(); });
    getChild<LLUICtrl>("filter")->setCommitCallback([this](LLUICtrl*, const LLSD&) { refreshList(); });
    bind("save_btn", [this] { onSaveFriends(); });
    bind("on_list_btn", [this] { onListMembership(true); });
    bind("off_list_btn", [this] { onListMembership(false); });
    bind("to_guests_btn", [this] { onToGuests(); });
    bind("remove_contact_btn", [this] { onRemoveContact(); });
    bind("set_consent_btn", [this] { onSetConsent(); });
    bind("add_btn", [this] { onAddManual(); });
    bind("new_list_btn", [this] { onNewList(); });
    bind("remove_list_btn", [this] { onRemoveList(); });
    bind("set_rsvp_btn", [this] { onSetRsvp(); });
    bind("list_to_guests_btn", [this] { onListToGuests(); });
    bind("remove_guest_btn", [this] { onRemoveGuest(); });
    bind("rsvp_file_btn", [this] { onRsvpFile(); });
    bind("share_btn", [this] { onShare(true); });
    bind("unshare_btn", [this] { onShare(false); });
    bind("export_share_btn", [this] { onExportShare(); });
    bind("import_share_btn", [this] { onImportShare(); });
    bind("purpose_combo", [this] { invalidatePreview(); });
    bind("preview_btn", [this] { onPreview(); });
    bind("leave_out_btn", [this] { onLeaveOut(); });
    bind("send_btn", [this] { onSend(); });
    getChild<LLUICtrl>("send_text")->setCommitCallback([this](LLUICtrl*, const LLSD&) { invalidatePreview(); });
    getChild<LLTextEditor>("send_text")->setKeystrokeCallback([this](LLTextEditor*) { invalidatePreview(); });
    mChanged = party::onChanged([this] { refreshCombos(); refreshList(); });
    return LLFloater::postBuild();
}

void SW67FloaterPartyContacts::onOpen(const LLSD& key)
{
    LLFloater::onOpen(key);
    refreshCombos();
    if (key.has("view")) getChild<LLComboBox>("view_combo")->setValue(key["view"]);
    if (key.has("event")) getChild<LLComboBox>("event_combo")->setValue(key["event"]);
    onViewChanged();
    if (key.has("text")) getChild<LLUICtrl>("send_text")->setValue(LLSD(key["text"].asString().substr(0, 1023)));
    if (key.has("purpose")) getChild<LLComboBox>("purpose_combo")->setValue(key["purpose"]);
    if (key.has("text") || key.has("purpose"))
    {
        showStatus(StatusRole::Info, "Choose the list above, then Preview recipients. Announcements only go to contacts who opted in.");
    }
}

SW67FloaterPartyContacts::View SW67FloaterPartyContacts::view() const
{
    const std::string v = getChild<LLComboBox>("view_combo")->getValue().asString();
    return v == "contacts" ? View::Contacts : v == "guests" ? View::Guests : View::Friends;
}

void SW67FloaterPartyContacts::onViewChanged()
{
    const View v = view();
    for (const char* n : kFriendsOnly) getChild<LLView>(n)->setVisible(v == View::Friends);
    for (const char* n : kContactsOnly) getChild<LLView>(n)->setVisible(v == View::Contacts);
    for (const char* n : kGuestsOnly) getChild<LLView>(n)->setVisible(v == View::Guests);
    for (const char* n : kListControls) getChild<LLView>(n)->setVisible(v == View::Contacts);
    getChild<LLView>("list_combo")->setVisible(v != View::Friends);
    getChild<LLView>("event_combo")->setVisible(v != View::Friends);
    for (const char* n : {"send_label", "purpose_combo", "send_text", "preview_btn", "leave_out_btn", "send_btn", "preview_text"})
    {
        getChild<LLView>(n)->setVisible(v != View::Friends);
    }
    mLeftOut.clear();
    invalidatePreview();
    refreshList();
}

void SW67FloaterPartyContacts::refreshCombos()
{
    const Book& b = party::book();
    LLComboBox* lists = getChild<LLComboBox>("list_combo");
    const LLSD keepList = lists->getValue();
    lists->removeall();
    lists->add("All party contacts", LLSD(""));
    for (const ContactList& l : b.lists) lists->add(l.name, LLSD(l.id));
    if (!lists->setSelectedByValue(keepList, true)) lists->selectFirstItem();

    LLComboBox* events = getChild<LLComboBox>("event_combo");
    const LLSD keepEvent = events->getValue();
    events->removeall();
    std::set<std::string> seen;
    for (const auto* e : sw67::event::ordered(sw67viewer::events::schedule(), sw67viewer::events::nowEpoch()))
    {
        events->add(e->name + "  (" + sw67viewer::events::localFromEpoch(e->startEpoch) + ")", LLSD(e->id));
        seen.insert(e->id);
    }
    for (const auto& [id, gl] : b.guestLists)
    {
        if (!seen.count(id)) events->add(gl.eventName + (gl.ownerId == party::selfId() ? "" : "  (shared with you)"), LLSD(id));
    }
    if (events->getItemCount() == 0) events->add("No events yet: add one in the Event Control Room", LLSD(""));
    if (!events->setSelectedByValue(keepEvent, true)) events->selectFirstItem();
}

std::string SW67FloaterPartyContacts::listId() const
{
    return getChild<LLComboBox>("list_combo")->getValue().asString();
}

std::string SW67FloaterPartyContacts::eventId() const
{
    return getChild<LLComboBox>("event_combo")->getValue().asString();
}

GuestList* SW67FloaterPartyContacts::guestList(bool create)
{
    Book& b = party::book();
    const std::string id = eventId();
    if (id.empty()) return nullptr;
    auto it = b.guestLists.find(id);
    if (it != b.guestLists.end()) return &it->second;
    if (!create) return nullptr;
    const auto* e = sw67viewer::events::schedule().find(id);
    if (!e) return nullptr;
    return &guestListFor(b, id, e->name, party::selfId());
}

void SW67FloaterPartyContacts::refreshList()
{
    LLScrollListCtrl* list = getChild<LLScrollListCtrl>("people_list");
    list->deleteAllItems();
    const std::string filter = lower(getChild<LLUICtrl>("filter")->getValue().asString());
    auto passes = [&](const std::string& name) { return filter.empty() || lower(name).find(filter) != std::string::npos; };
    const Book& b = party::book();
    const View v = view();
    if (v == View::Friends)
    {
        LLAvatarTracker::buddy_map_t buddies;
        LLAvatarTracker::instance().copyBuddyList(buddies);
        for (const auto& [id, rel] : buddies)
        {
            LLAvatarName name;
            const std::string display = LLAvatarNameCache::get(id, &name) ? name.getCompleteName() : id.asString();
            if (!passes(display)) continue;
            addRow(list, id.asString(), display, b.contact(id.asString()) ? "Party contact" : "",
                   rel && rel->isOnline() ? "Online" : "");
        }
        list->sortByColumn("name", true);
        showStatus(StatusRole::Info, "Your Second Life friends, read only. Save to Party Contacts sends nothing to them.");
        return;
    }
    if (v == View::Contacts)
    {
        const std::string lid = listId();
        for (const auto& [id, c] : b.contacts)
        {
            if ((!lid.empty() && !c.lists.count(lid)) || !passes(c.displayName)) continue;
            std::string lists;
            for (const std::string& l : c.lists)
            {
                if (const ContactList* cl = b.list(l)) lists += (lists.empty() ? "" : ", ") + cl->name;
            }
            addRow(list, id, c.displayName, lists, consentName(c.announcements) + ", " + sourceName(c.source));
        }
        list->sortByColumn("name", true);
        return;
    }
    const GuestList* gl = guestList(false);
    if (!gl)
    {
        showStatus(StatusRole::Info, eventId().empty() ? "Add an event in the Event Control Room first."
                                                       : "No guests yet. Add contacts from the Party Contacts view, a whole list, or an RSVP file.");
        return;
    }
    if (!allowed(*gl, party::selfId(), Permission::View))
    {
        showStatus(StatusRole::Restricted, "This guest list was not shared with you.");
        return;
    }
    for (const Guest& g : gl->guests)
    {
        if (!passes(g.displayName)) continue;
        const Contact* c = b.contact(g.accountId);
        addRow(list, g.accountId, g.displayName, std::string(rsvpName(g.rsvp)) + (mLeftOut.count(g.accountId) ? "  (left out of this send)" : ""),
               c ? consentName(c->announcements) : "Not a party contact");
    }
    list->sortByColumn("name", true);
    std::string shares;
    for (const Share& s : gl->shares)
    {
        shares += (shares.empty() ? "" : "; ") + s.name + (s.canEdit ? " edits" : " views") + (s.canInvite ? ", invites" : "");
    }
    showStatus(StatusRole::Info, std::to_string(gl->guests.size()) + " guest(s). " +
                                     (shares.empty() ? std::string("Private: shared with nobody.") : "Shared with: " + shares + "."));
}

std::vector<std::string> SW67FloaterPartyContacts::selectedIds() const
{
    std::vector<std::string> out;
    for (LLScrollListItem* item : getChild<LLScrollListCtrl>("people_list")->getAllSelected())
    {
        out.push_back(item->getValue().asString());
    }
    return out;
}

void SW67FloaterPartyContacts::onSaveFriends()
{
    uuid_vec_t ids;
    for (const std::string& s : selectedIds()) ids.emplace_back(s);
    if (ids.empty())
    {
        showStatus(StatusRole::Info, "Select one or more friends first.");
        return;
    }
    party::saveResidents(ids, Source::Friend);
    showStatus(StatusRole::Success, "Saving " + std::to_string(ids.size()) + " friend(s) to Party Contacts. No message or friend request is sent.");
}

void SW67FloaterPartyContacts::onListMembership(bool on)
{
    const std::string lid = listId();
    if (lid.empty())
    {
        showStatus(StatusRole::Info, "Choose a list above first.");
        return;
    }
    int n = 0;
    for (const std::string& id : selectedIds()) n += setOnList(party::book(), id, lid, on).ok ? 1 : 0;
    party::save();
    showStatus(StatusRole::Success, std::to_string(n) + (on ? " added to " : " taken off ") + party::book().list(lid)->name + ".");
}

void SW67FloaterPartyContacts::onToGuests()
{
    GuestList* gl = guestList(true);
    if (!gl)
    {
        showStatus(StatusRole::Info, "Choose an event above first.");
        return;
    }
    int n = 0;
    std::string why;
    for (const std::string& id : selectedIds())
    {
        const Contact* c = party::book().contact(id);
        const Result r = addGuest(*gl, party::selfId(), id, c ? c->displayName : "");
        n += r.ok ? 1 : 0;
        if (!r.ok) why = r.message;
    }
    party::save();
    showStatus(n ? StatusRole::Success : StatusRole::Warning,
               std::to_string(n) + " added to " + gl->eventName + "'s guest list." + (why.empty() ? "" : " " + why));
}

void SW67FloaterPartyContacts::onRemoveContact()
{
    const std::vector<std::string> ids = selectedIds();
    if (ids.empty()) return;
    LLSD args;
    args["MESSAGE"] = "Remove " + std::to_string(ids.size()) +
                      " resident(s) from Party Contacts and its lists? Second Life friendships are not changed, and "
                      "guest lists keep their RSVPs.";
    const LLHandle<LLFloater> handle = getHandle();
    LLNotificationsUtil::add("GenericAlertYesCancel", args, LLSD(), [handle, ids](const LLSD& n, const LLSD& r) {
        if (LLNotificationsUtil::getSelectedOption(n, r) != 0) return;
        int removed = 0;
        for (const std::string& id : ids) removed += removeContact(party::book(), id).ok ? 1 : 0;
        party::save();
        if (auto* self = ALViewType::as<SW67FloaterPartyContacts>(handle.get()))
        {
            self->showStatus(StatusRole::Success, std::to_string(removed) + " removed from Party Contacts. Friendships are unchanged.");
        }
    });
}

void SW67FloaterPartyContacts::onSetConsent()
{
    const std::string v = getChild<LLComboBox>("consent_combo")->getValue().asString();
    const Announcements a = v == "opted_in" ? Announcements::OptedIn : v == "opted_out" ? Announcements::OptedOut : Announcements::NotAsked;
    int n = 0;
    for (const std::string& id : selectedIds()) n += setAnnouncements(party::book(), id, a, party::nowUtc()).ok ? 1 : 0;
    party::save();
    showStatus(StatusRole::Success, std::to_string(n) + " marked " + consentName(a) + ". " +
                                        (a == Announcements::OptedOut ? "They are left out of every later send." : ""));
}

void SW67FloaterPartyContacts::onAddManual()
{
    const auto id = findAccountId(getChild<LLUICtrl>("add_id")->getValue().asString());
    if (!id)
    {
        showStatus(StatusRole::Warning, "Paste an account ID or a profile link (secondlife:///app/agent/.../about).");
        return;
    }
    party::saveResidents({LLUUID(*id)}, Source::Manual);
    getChild<LLUICtrl>("add_id")->setValue(LLSD(""));
}

void SW67FloaterPartyContacts::onNewList()
{
    const Result r = addList(party::book(), getChild<LLUICtrl>("new_list_name")->getValue().asString());
    if (r.ok)
    {
        party::save();
        getChild<LLUICtrl>("new_list_name")->setValue(LLSD(""));
    }
    showStatus(r.ok ? StatusRole::Success : StatusRole::Warning, r.ok ? "List made." : r.message);
}

void SW67FloaterPartyContacts::onRemoveList()
{
    const std::string lid = listId();
    if (lid.empty()) return;
    const Result r = removeList(party::book(), lid);
    if (r.ok) party::save();
    showStatus(r.ok ? StatusRole::Success : StatusRole::Warning, r.ok ? "List removed; its contacts stay." : r.message);
}

void SW67FloaterPartyContacts::onSetRsvp()
{
    GuestList* gl = guestList(false);
    if (!gl) return;
    const auto r = parseRsvp(getChild<LLComboBox>("rsvp_combo")->getValue().asString());
    int n = 0;
    for (const std::string& id : selectedIds()) n += setRsvp(*gl, party::selfId(), id, r.value_or(Rsvp::NotInvited)).ok ? 1 : 0;
    party::save();
    showStatus(n ? StatusRole::Success : StatusRole::Restricted, n ? std::to_string(n) + " updated." : "You can't change this guest list.");
}

void SW67FloaterPartyContacts::onListToGuests()
{
    GuestList* gl = guestList(true);
    if (!gl || listId().empty())
    {
        showStatus(StatusRole::Info, "Choose a list and an event above first.");
        return;
    }
    const std::size_t n = addListToGuests(party::book(), *gl, party::selfId(), listId());
    party::save();
    showStatus(StatusRole::Success, std::to_string(n) + " new guest(s) from " + party::book().list(listId())->name + ".");
}

void SW67FloaterPartyContacts::onRemoveGuest()
{
    GuestList* gl = guestList(false);
    if (!gl) return;
    int n = 0;
    for (const std::string& id : selectedIds()) n += removeGuest(*gl, party::selfId(), id).ok ? 1 : 0;
    party::save();
    showStatus(n ? StatusRole::Success : StatusRole::Restricted, n ? std::to_string(n) + " taken off the guest list." : "You can't change this guest list.");
}

void SW67FloaterPartyContacts::onRsvpFile()
{
    if (!guestList(true))
    {
        showStatus(StatusRole::Info, "Choose one of your events first.");
        return;
    }
    const LLHandle<LLFloater> handle = getHandle();
    LLFilePickerReplyThread::startPicker(
        [handle](const std::vector<std::string>& files, LLFilePicker::ELoadFilter, LLFilePicker::ESaveFilter) {
            auto* self = ALViewType::as<SW67FloaterPartyContacts>(handle.get());
            if (!self || files.empty()) return;
            const auto text = sw67viewer::readFile(files.front(), 2u * 1024u * 1024u);
            const auto root = text ? sw67::io::parseJson(*text) : std::nullopt;
            if (!root || (*root)["format"].asString("") != "sw67.rsvps")
            {
                self->showStatus(StatusRole::Error, "That is not a 6|7 RSVP file.");
                return;
            }
            GuestList* gl = self->guestList(true);
            if (!gl) return;
            int accepted = 0;
            int skipped = 0;
            for (const sw67::io::Json& j : (*root)["rsvps"].items())
            {
                RsvpRecord rec;
                rec.accountId = j["id"].asString("");
                rec.displayName = j["name"].asString("").substr(0, 120);
                rec.answer = parseRsvp(j["answer"].asString("")).value_or(Rsvp::Going);
                if (j["announcements"].isBool()) rec.wantsAnnouncements = j["announcements"].asBool();
                (acceptRsvp(party::book(), *gl, rec, party::nowUtc()).ok ? accepted : skipped)++;
            }
            party::save();
            self->showStatus(StatusRole::Success, std::to_string(accepted) + " RSVP(s) accepted" +
                                                      (skipped ? ", " + std::to_string(skipped) + " skipped" : std::string()) +
                                                      ". Only an explicit tick on the RSVP signs anyone up for announcements.");
        },
        LLFilePicker::FFLOAD_ALL, false);
}

void SW67FloaterPartyContacts::onShare(bool on)
{
    GuestList* gl = guestList(true);
    const auto id = findAccountId(getChild<LLUICtrl>("share_id")->getValue().asString());
    if (!gl || !id)
    {
        showStatus(StatusRole::Info, "Choose an event and paste the co-host's account ID or profile link.");
        return;
    }
    Result r;
    if (on)
    {
        Share s;
        s.accountId = *id;
        LLAvatarName name;
        s.name = LLAvatarNameCache::get(LLUUID(*id), &name) ? name.getDisplayName() : *id;
        s.canEdit = getChild<LLUICtrl>("share_edit_check")->getValue().asBoolean();
        s.canInvite = getChild<LLUICtrl>("share_invite_check")->getValue().asBoolean();
        r = setShare(*gl, party::selfId(), s);
    }
    else
    {
        r = removeShare(*gl, party::selfId(), *id);
    }
    if (r.ok) party::save();
    showStatus(r.ok ? StatusRole::Success : StatusRole::Restricted,
               r.ok ? (on ? "Shared. Use \"Share with co-host...\" to save the file for them; only this guest list goes in it."
                          : "Sharing stopped. A file you already gave them still holds the list as it was.")
                    : r.message);
}

void SW67FloaterPartyContacts::onExportShare()
{
    GuestList* gl = guestList(false);
    const auto id = findAccountId(getChild<LLUICtrl>("share_id")->getValue().asString());
    if (!gl || !id || !allowed(*gl, *id, Permission::View) || gl->ownerId != party::selfId())
    {
        showStatus(StatusRole::Info, "Share the list with that co-host first (paste their ID and press Share).");
        return;
    }
    const std::string text = writeShare(*gl, *id);
    const LLHandle<LLFloater> handle = getHandle();
    LLFilePickerReplyThread::startPicker(
        [handle, text](const std::vector<std::string>& files, LLFilePicker::ELoadFilter, LLFilePicker::ESaveFilter) {
            auto* self = ALViewType::as<SW67FloaterPartyContacts>(handle.get());
            if (!self || files.empty()) return;
            const bool ok = sw67viewer::writeFileAtomic(files.front(), text);
            self->showStatus(ok ? StatusRole::Success : StatusRole::Error,
                             ok ? "Saved. It holds this guest list and that co-host's rights only." : "Could not write the file.");
        },
        LLFilePicker::FFSAVE_ALL, "guest list.sw67guests.json");
}

void SW67FloaterPartyContacts::onImportShare()
{
    const LLHandle<LLFloater> handle = getHandle();
    LLFilePickerReplyThread::startPicker(
        [handle](const std::vector<std::string>& files, LLFilePicker::ELoadFilter, LLFilePicker::ESaveFilter) {
            auto* self = ALViewType::as<SW67FloaterPartyContacts>(handle.get());
            if (!self || files.empty()) return;
            const auto text = sw67viewer::readFile(files.front(), 4u * 1024u * 1024u);
            ShareReadResult r = text ? readShare(*text) : ShareReadResult{};
            if (!r.list)
            {
                self->showStatus(StatusRole::Error, r.notes.empty() ? std::string("Could not read that file.") : r.notes.front());
                return;
            }
            if (!allowed(*r.list, party::selfId(), Permission::View))
            {
                self->showStatus(StatusRole::Restricted, "This guest list was not shared with you.");
                return;
            }
            Book& b = party::book();
            auto it = b.guestLists.find(r.list->eventId);
            if (it != b.guestLists.end() && it->second.ownerId == party::selfId())
            {
                self->showStatus(StatusRole::Warning, "You own an event with the same ID; the shared copy was not opened.");
                return;
            }
            const std::string id = r.list->eventId;
            b.guestLists[id] = std::move(*r.list);
            party::save();
            self->getChild<LLComboBox>("event_combo")->setSelectedByValue(LLSD(id), true);
            self->refreshList();
        },
        LLFilePicker::FFLOAD_ALL, false);
}

void SW67FloaterPartyContacts::invalidatePreview()
{
    mPreview.reset();
    mPreviewKey.clear();
    getChild<LLUICtrl>("send_btn")->setEnabled(false);
}

void SW67FloaterPartyContacts::onPreview()
{
    const std::string text = getChild<LLUICtrl>("send_text")->getValue().asString();
    const bool announcement = getChild<LLComboBox>("purpose_combo")->getValue().asString() == "announcement";
    SendPreview p;
    if (announcement)
    {
        if (listId().empty())
        {
            showStatus(StatusRole::Info, "Choose the list to announce to above.");
            return;
        }
        p = previewAnnouncement(party::book(), listId(), mLeftOut);
    }
    else
    {
        const GuestList* gl = guestList(false);
        if (!gl)
        {
            showStatus(StatusRole::Info, "Choose an event with guests above.");
            return;
        }
        p = previewInvitations(party::book(), *gl, party::selfId(), mLeftOut);
    }
    getChild<LLUICtrl>("preview_text")->setValue(LLSD("Message: " + text + "\n\n" + p.describe()));
    const bool okText = text.find_first_not_of(" \t\r\n") != std::string::npos && text.size() <= 1023;
    mPreview = p;
    mPreviewKey = std::string(announcement ? "a:" : "i:") + listId() + "|" + eventId() + "|" + text;
    getChild<LLUICtrl>("send_btn")->setEnabled(okText && !p.recipients.empty() && !party::sending());
    showStatus(okText ? StatusRole::Info : StatusRole::Warning,
               okText ? "Check the list. Select anyone to leave out and press \"Leave selected out\", then Send."
                      : "Write the message first: one IM, up to 1,023 bytes.");
}

void SW67FloaterPartyContacts::onLeaveOut()
{
    for (const std::string& id : selectedIds()) mLeftOut.insert(id);
    refreshList();
    onPreview();
}

void SW67FloaterPartyContacts::onSend()
{
    const std::string text = getChild<LLUICtrl>("send_text")->getValue().asString();
    const bool announcement = getChild<LLComboBox>("purpose_combo")->getValue().asString() == "announcement";
    const std::string key = std::string(announcement ? "a:" : "i:") + listId() + "|" + eventId() + "|" + text;
    if (!mPreview || key != mPreviewKey)
    {
        invalidatePreview();
        showStatus(StatusRole::Warning, "Something changed since the preview. Preview again before sending.");
        return;
    }
    LLSD args;
    args["MESSAGE"] = "Send this message as an IM to " + std::to_string(mPreview->recipients.size()) +
                      " resident(s), one about every two seconds?\n\n" + text;
    const LLHandle<LLFloater> handle = getHandle();
    LLNotificationsUtil::add("GenericAlertYesCancel", args, LLSD(), [handle](const LLSD& n, const LLSD& r) {
        if (LLNotificationsUtil::getSelectedOption(n, r) != 0) return;
        if (auto* self = ALViewType::as<SW67FloaterPartyContacts>(handle.get())) self->doSend();
    });
}

void SW67FloaterPartyContacts::doSend()
{
    if (!mPreview) return;
    const bool invitation = mPreview->purpose == Purpose::Invitation;
    const std::string evt = eventId();
    const std::string text = getChild<LLUICtrl>("send_text")->getValue().asString();
    const LLHandle<LLFloater> handle = getHandle();
    std::string whyNot;
    const bool started = party::startSend(
        mPreview->recipients, text,
        [handle, invitation, evt](const party::SendOutcome& o) {
            if (invitation)
            {
                auto it = party::book().guestLists.find(evt);
                if (it != party::book().guestLists.end()) markInvited(it->second, o.sent, party::nowUtc());
                party::save();
            }
            auto* self = ALViewType::as<SW67FloaterPartyContacts>(handle.get());
            if (!self) return;
            std::string report = std::to_string(o.sent.size()) + " sent.";
            for (const Left& l : o.notSent) report += "\nNot sent to " + l.displayName + ": " + l.reason;
            self->getChild<LLUICtrl>("preview_text")->setValue(LLSD(report));
            self->showStatus(o.notSent.empty() ? StatusRole::Success : StatusRole::Warning,
                             std::to_string(o.sent.size()) + " sent, " + std::to_string(o.notSent.size()) + " not sent. Details above.");
            self->refreshList();
        },
        whyNot);
    invalidatePreview();
    showStatus(started ? StatusRole::Live : StatusRole::Warning, started ? "Sending, one IM about every two seconds..." : whyNot);
}

void SW67FloaterPartyContacts::showStatus(StatusRole role, const std::string& text)
{
    LLTextBox* box = getChild<LLTextBox>("status_text");
    box->setText(sw67viewer::statusText(role) + "  " + text);
    box->setColor(LLUIColor(sw67viewer::statusColor(role)));
}
