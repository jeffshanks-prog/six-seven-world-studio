/**
 * @file party.h
 * @brief Party Contacts for DJs and hosts: the host's own event contacts,
 *        reusable lists (Friday Night Regulars, VIPs, Venue Staff...), and a
 *        guest list per event with each guest's RSVP.
 *
 *        Contacts are keyed by the resident's account ID, so a display-name
 *        change never makes a duplicate. This module never touches Second
 *        Life friendships: saving, removing or listing a party contact sends
 *        no friend request and ends no friendship, and nothing here enrolls
 *        anyone in announcements. Announcement consent is tracked apart from
 *        RSVPs: an RSVP to one party is not consent to future promotions, and
 *        an opt-out holds for every later send.
 *
 *        Everything is private to the host by default. A guest list can be
 *        shared with named co-hosts, each with view, edit or invite rights
 *        the event's owner grants; other lists and the host's friends are
 *        never part of a share.
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

#include <cstddef>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace sw67::contacts
{

inline constexpr std::string_view kFormat = "sw67.partycontacts";
inline constexpr std::string_view kShareFormat = "sw67.guestlist.share";
inline constexpr int kFormatVersion = 1;
inline constexpr std::size_t kMaxContacts = 5000;
inline constexpr std::size_t kMaxLists = 100;
/// IMs one send may go to. 6|7's own cap, kept well inside what Second Life
/// accepts from one resident; sends are also paced (kSendSpacingSeconds).
inline constexpr std::size_t kMaxRecipientsPerSend = 40;
inline constexpr double kSendSpacingSeconds = 2.0;

/// True for "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx" (any case), not all zeros.
bool isAccountId(std::string_view id);
/// The first account ID in pasted text (a bare ID or a secondlife:///app/agent/<id>/about link), lower case.
std::optional<std::string> findAccountId(std::string_view text);

enum class Source
{
    Manual,
    Friend,   ///< Saved from the friends list.
    People,   ///< Saved from a resident's menu (nearby, profile).
    Rsvp,     ///< Accepted from a 6|7 event page RSVP.
};
std::string_view sourceKey(Source s);

/// Consent to announcements (promotions), apart from any RSVP.
enum class Announcements
{
    NotAsked, ///< Default: receives no announcements.
    OptedIn,  ///< Said yes to announcements.
    OptedOut, ///< Said no: receives nothing at all from later sends.
};
std::string_view announcementsKey(Announcements a);

struct Contact
{
    std::string accountId;    ///< Stable key.
    std::string displayName;  ///< As last seen; updated, never a key.
    std::string userName;
    Source source = Source::Manual;
    std::string notes;
    Announcements announcements = Announcements::NotAsked;
    std::string consentChanged; ///< UTC text of the last change to announcements.
    std::set<std::string> lists; ///< List IDs.
    std::string added;           ///< UTC text.
};

struct ContactList
{
    std::string id;   ///< "friday_night_regulars"
    std::string name; ///< "Friday Night Regulars"
};

enum class Rsvp
{
    NotInvited, ///< On the list; no invitation sent yet.
    Invited,
    Going,
    Maybe,
    NotGoing,
};
std::string_view rsvpKey(Rsvp r);
std::string_view rsvpName(Rsvp r);
std::optional<Rsvp> parseRsvp(std::string_view key);

struct Guest
{
    std::string accountId;
    std::string displayName;
    Rsvp rsvp = Rsvp::NotInvited;
    std::string invitedAt; ///< UTC text of the last invitation sent.
};

/// What a co-host may do with one guest list.
struct Share
{
    std::string accountId;
    std::string name;
    bool canEdit = false;
    bool canInvite = false; ///< Send invitations to this list's guests.
};

struct GuestList
{
    std::string eventId;
    std::string eventName;
    std::string ownerId; ///< The event's owner: only they change shares.
    std::vector<Guest> guests;
    std::vector<Share> shares;

    const Guest* guest(const std::string& accountId) const;
};

enum class Permission
{
    View,
    Edit,
    Invite,
    ManageSharing,
};
/// The owner may do anything; a co-host only what their share grants; anyone
/// else nothing.
bool allowed(const GuestList& list, const std::string& accountId, Permission p);

struct Book
{
    std::map<std::string, Contact> contacts; ///< By account ID.
    std::vector<ContactList> lists;
    std::map<std::string, GuestList> guestLists; ///< By event ID.

    const Contact* contact(const std::string& accountId) const;
    const ContactList* list(const std::string& id) const;
};

/// The lists a new book starts with; the host can rename or remove them.
std::vector<ContactList> starterLists();
/// A list ID from a name: lower case, letters, digits and underscores.
std::string listIdFor(std::string_view name);

struct Result
{
    bool ok = false;
    std::string message;
};

/// Saves or updates a contact by account ID: a known contact keeps its
/// lists, notes and consent and takes the newer display name. Never changes
/// consent: that is setAnnouncements' alone.
Result saveContact(Book& book, const std::string& accountId, const std::string& displayName,
                   const std::string& userName, Source source, const std::string& nowUtc);
/// Removes a party contact and takes them off every list. Guest lists keep
/// their RSVP records for events already planned. A friendship is not part
/// of the book and is not touched.
Result removeContact(Book& book, const std::string& accountId);
Result setAnnouncements(Book& book, const std::string& accountId, Announcements a, const std::string& nowUtc);

Result addList(Book& book, const std::string& name);
Result renameList(Book& book, const std::string& id, const std::string& name);
/// Removes the list; its contacts stay.
Result removeList(Book& book, const std::string& id);
Result setOnList(Book& book, const std::string& accountId, const std::string& listId, bool on);
std::vector<const Contact*> members(const Book& book, const std::string& listId);

/// Guest lists. `actor` must hold Edit (the owner always does).
GuestList& guestListFor(Book& book, const std::string& eventId, const std::string& eventName, const std::string& ownerId);
Result addGuest(GuestList& list, const std::string& actor, const std::string& accountId, const std::string& displayName);
/// Adds every member of a contact list; returns how many were new.
std::size_t addListToGuests(const Book& book, GuestList& list, const std::string& actor, const std::string& listId);
Result removeGuest(GuestList& list, const std::string& actor, const std::string& accountId);
Result setRsvp(GuestList& list, const std::string& actor, const std::string& accountId, Rsvp r);
Result setShare(GuestList& list, const std::string& actor, const Share& share);
Result removeShare(GuestList& list, const std::string& actor, const std::string& accountId);

/// An RSVP from a 6|7 event page, which the host chose to accept. It adds
/// the guest to the event's list with their answer and saves them as a
/// party contact (source Rsvp). Consent to announcements changes only when
/// the RSVP itself carries an explicit yes or no.
struct RsvpRecord
{
    std::string accountId;
    std::string displayName;
    Rsvp answer = Rsvp::Going;
    std::optional<bool> wantsAnnouncements; ///< Only an explicit tick counts.
};
Result acceptRsvp(Book& book, GuestList& list, const RsvpRecord& rsvp, const std::string& nowUtc);

/// Who a send would go to, before anything is sent.
enum class Purpose
{
    Invitation,   ///< For one event: guests on its list.
    Announcement, ///< A promotion: only contacts who opted in.
};

struct Recipient
{
    std::string accountId;
    std::string displayName;
};

struct Left
{
    std::string accountId;
    std::string displayName;
    std::string reason;
};

struct SendPreview
{
    Purpose purpose = Purpose::Invitation;
    std::vector<Recipient> recipients;
    std::vector<Left> leftOut; ///< With reasons: opted out, removed by host, over the cap...
    std::string describe() const;
};

/// Invitations to an event's guests. Leaves out opted-out residents, those
/// the host removed, guests who already said Not going, and anyone past the
/// cap. `actor` needs Invite.
SendPreview previewInvitations(const Book& book, const GuestList& list, const std::string& actor,
                               const std::set<std::string>& removedByHost);
/// An announcement to a contact list: only members who opted in.
SendPreview previewAnnouncement(const Book& book, const std::string& listId,
                                const std::set<std::string>& removedByHost);

/// After a send: marks the guests invited (NotInvited -> Invited).
void markInvited(GuestList& list, const std::vector<std::string>& sentTo, const std::string& nowUtc);

std::string writeBook(const Book& book);
struct ReadResult
{
    std::optional<Book> book;
    std::vector<std::string> notes;
};
ReadResult readBook(std::string_view text);

/// One guest list for a co-host: the list, its guests and the share that
/// applies to them. No other list, contact note, consent record or friend.
std::string writeShare(const GuestList& list, const std::string& forAccountId);
struct ShareReadResult
{
    std::optional<GuestList> list;
    std::vector<std::string> notes;
};
ShareReadResult readShare(std::string_view text);

} // namespace sw67::contacts
