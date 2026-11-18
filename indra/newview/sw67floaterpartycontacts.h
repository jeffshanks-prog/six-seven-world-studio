/**
 * @file sw67floaterpartycontacts.h
 * @brief Party Contacts (DJ & Hosting): three views, Personal Friends (the
 *        resident's Second Life friends, read only), Party Contacts (the
 *        host's own event contacts and their lists) and Event Guest Lists
 *        (guests chosen for one event, with RSVPs and sharing). Invitations
 *        and announcements are previewed, can have people left out, and go
 *        only after the host confirms.
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
#include "sw67/contacts/party.h"

#include <boost/signals2.hpp>
#include <optional>
#include <set>
#include <string>
#include <vector>

class SW67FloaterPartyContacts final : public LLFloater
{
public:
    AL_VIEW_TYPE(SW67FloaterPartyContacts, LLFloater);

    SW67FloaterPartyContacts(const LLSD& key);
    bool postBuild() override;
    /// key: {"view": "guests", "event": "<event id>"} to open on an event's guest list.
    void onOpen(const LLSD& key) override;

private:
    ~SW67FloaterPartyContacts() override;

    enum class View
    {
        Friends,
        Contacts,
        Guests,
    };
    View view() const;
    void onViewChanged();
    void refreshCombos();
    void refreshList();
    std::vector<std::string> selectedIds() const;
    std::string listId() const;
    std::string eventId() const;
    sw67::contacts::GuestList* guestList(bool create);

    void onSaveFriends();
    void onListMembership(bool on);
    void onToGuests();
    void onRemoveContact();
    void onSetConsent();
    void onAddManual();
    void onNewList();
    void onRemoveList();
    void onSetRsvp();
    void onListToGuests();
    void onRemoveGuest();
    void onRsvpFile();
    void onShare(bool on);
    void onExportShare();
    void onImportShare();
    void onPreview();
    void onLeaveOut();
    void onSend();
    void doSend();
    void invalidatePreview();
    void showStatus(sw67::a11y::StatusRole role, const std::string& text);

    boost::signals2::scoped_connection mChanged;
    std::set<std::string> mLeftOut;  ///< Removed by the host from the current send.
    std::optional<sw67::contacts::SendPreview> mPreview;
    std::string mPreviewKey;         ///< What the preview was made for.
};
