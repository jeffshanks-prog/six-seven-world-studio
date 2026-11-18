/**
 * @file sw67partycontacts.h
 * @brief Party Contacts in the viewer: the host's contact book on disk, the
 *        "Save to Party Contacts" action on residents' menus, and the paced
 *        sending of invitations and announcements the host has previewed
 *        and confirmed.
 *
 *        Nothing here touches friendships: saving or removing a party
 *        contact sends no friend request and removes no friend. Invitations
 *        and announcements are plain IMs, sent one at a time, each checked
 *        against RLV first, and only after the host confirmed the recipient
 *        list in a preview.
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

#include "lluuid.h"

#include "sw67/contacts/party.h"
#include "sw67/event/control.h"

#include <boost/signals2.hpp>
#include <functional>
#include <string>
#include <vector>

namespace sw67viewer::party
{

/// The book, loaded on first use. Main thread only.
sw67::contacts::Book& book();
/// Writes the book; false when the file could not be written.
bool save();
/// Fires after any change made through this module or after save().
boost::signals2::connection onChanged(const std::function<void()>& cb);

/// This resident's account ID, lower case.
std::string selfId();
std::string nowUtc();

/// "Save to Party Contacts" from a resident menu or the friends list. Looks
/// up each name, saves, and shows one notice. Sends nothing to anyone.
void saveResidents(const uuid_vec_t& ids, sw67::contacts::Source source);

/// Sends `text` as one IM to each recipient, kSendSpacingSeconds apart.
/// Each is checked against RLV just before it goes. `done` gets the IDs
/// that were sent and those that were not, with reasons.
struct SendOutcome
{
    std::vector<std::string> sent;
    std::vector<sw67::contacts::Left> notSent;
};
bool startSend(const std::vector<sw67::contacts::Recipient>& recipients, const std::string& text,
               std::function<void(const SendOutcome&)> done, std::string& whyNot);
bool sending();
/// Stops a send in progress; those not yet sent are reported as not sent.
void cancelSend();

} // namespace sw67viewer::party

namespace sw67viewer::events
{
/// The Event Control Room's schedule, loaded on first use. Main thread only.
sw67::event::Schedule& schedule();
bool saveSchedule();
std::int64_t nowEpoch();
/// Local-time text <-> epoch, with the computer's clock and time zone.
std::optional<std::int64_t> epochFromLocal(const std::string& text);
std::string localFromEpoch(std::int64_t epoch);
} // namespace sw67viewer::events
