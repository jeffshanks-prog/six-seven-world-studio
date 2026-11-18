/**
 * @file party_test.cpp
 * @brief Party Contacts: the host's workflow end to end.
 *
 * Copyright (c) 2026, 6|7 World Studio contributors.
 *
 * The source code in this file is provided to you under the terms of the
 * GNU Lesser General Public License, version 2.1, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE. Terms of the LGPL can be found in doc/LGPL-license.txt
 * in this distribution, or online at http://www.gnu.org/licenses/lgpl-2.1.txt
 */

#include "sw67/contacts/party.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdio>

using namespace sw67::contacts;

namespace
{
const std::string kHost = "11111111-1111-1111-1111-111111111111";
const std::string kFriend = "22222222-2222-2222-2222-222222222222";
const std::string kGuest = "33333333-3333-3333-3333-333333333333";
const std::string kCoHost = "44444444-4444-4444-4444-444444444444";
const std::string kOther = "55555555-5555-5555-5555-555555555555";
const std::string kNow = "2026-11-18 02:00:00";

Book starter()
{
    Book b;
    b.lists = starterLists();
    return b;
}
} // namespace

TEST_CASE("Party contacts are keyed by account ID", "[party]")
{
    CHECK(isAccountId(kHost));
    CHECK_FALSE(isAccountId("00000000-0000-0000-0000-000000000000"));
    CHECK_FALSE(isAccountId("Jeff Resident"));
    CHECK(findAccountId("secondlife:///app/agent/22222222-2222-2222-2222-22222222222A/about") == "22222222-2222-2222-2222-22222222222a");
    CHECK_FALSE(findAccountId("no id here").has_value());
    Book b = starter();
    CHECK(saveContact(b, kFriend, "DJ Nova", "nova.resident", Source::Friend, kNow).ok);
    // A display-name change updates the contact; it does not duplicate it.
    const Result again = saveContact(b, "22222222-2222-2222-2222-222222222222", "Nova Starr", "", Source::People, kNow);
    CHECK(again.ok);
    CHECK(b.contacts.size() == 1);
    CHECK(b.contact(kFriend)->displayName == "Nova Starr");
    CHECK(b.contact(kFriend)->userName == "nova.resident");
    CHECK(b.contact(kFriend)->source == Source::Friend);
    // Saving never signs anyone up.
    CHECK(b.contact(kFriend)->announcements == Announcements::NotAsked);
    CHECK_FALSE(saveContact(b, "not-an-id", "x", "", Source::Manual, kNow).ok);
}

TEST_CASE("Lists hold one contact on several", "[party]")
{
    Book b = starter();
    saveContact(b, kFriend, "Nova", "", Source::Friend, kNow);
    CHECK(setOnList(b, kFriend, "vips", true).ok);
    CHECK(setOnList(b, kFriend, "friday_night_regulars", true).ok);
    CHECK(members(b, "vips").size() == 1);
    CHECK(members(b, "friday_night_regulars").size() == 1);
    CHECK_FALSE(setOnList(b, kGuest, "vips", true).ok);
    CHECK(listIdFor("R&B Night") == "rnb_night");
    CHECK(addList(b, "Birthday Crew").ok);
    CHECK_FALSE(addList(b, "birthday crew").ok);
    CHECK(removeList(b, "vips").ok);
    CHECK(b.contact(kFriend)->lists.count("vips") == 0);
    CHECK(b.contacts.size() == 1);
}

TEST_CASE("The host's whole workflow: friend, RSVP guest, guest list, preview, remove", "[party]")
{
    Book b = starter();
    // 1. Save a friend as a party contact.
    REQUIRE(saveContact(b, kFriend, "Nova", "nova.resident", Source::Friend, kNow).ok);
    setOnList(b, kFriend, "friday_night_regulars", true);
    // 2. Accept an RSVP from someone who is not a friend; no announcement tick.
    GuestList& gl = guestListFor(b, "evt1", "Friday Night Live", kHost);
    REQUIRE(acceptRsvp(b, gl, {kGuest, "Rae", Rsvp::Going, std::nullopt}, kNow).ok);
    CHECK(b.contact(kGuest)->source == Source::Rsvp);
    CHECK(b.contact(kGuest)->announcements == Announcements::NotAsked); // An RSVP is not consent to promotions.
    // 3. Build the guest list from a reusable list.
    CHECK(addListToGuests(b, gl, kHost, "friday_night_regulars") == 1);
    CHECK(gl.guests.size() == 2);
    // 4. Preview invitations; the host removes one before sending.
    SendPreview p = previewInvitations(b, gl, kHost, {});
    CHECK(p.recipients.size() == 2);
    p = previewInvitations(b, gl, kHost, {kGuest});
    REQUIRE(p.recipients.size() == 1);
    CHECK(p.recipients[0].accountId == kFriend);
    CHECK(p.leftOut.size() == 1);
    CHECK(p.describe().find("Nothing is sent until you press Send") != std::string::npos);
    markInvited(gl, {kFriend}, kNow);
    CHECK(gl.guest(kFriend)->rsvp == Rsvp::Invited);
    CHECK(gl.guest(kGuest)->rsvp == Rsvp::Going);
    // 5. Remove the party contact: lists drop them, the event's RSVP record stays.
    REQUIRE(removeContact(b, kFriend).ok);
    CHECK(b.contact(kFriend) == nullptr);
    CHECK(members(b, "friday_night_regulars").empty());
    CHECK(b.guestLists.at("evt1").guest(kFriend) != nullptr);
    CHECK(removeContact(b, kFriend).message.find("not a party contact") != std::string::npos);
}

TEST_CASE("Announcements reach only those who opted in, and an opt-out holds", "[party]")
{
    Book b = starter();
    saveContact(b, kFriend, "Nova", "", Source::Friend, kNow);
    saveContact(b, kGuest, "Rae", "", Source::Manual, kNow);
    saveContact(b, kOther, "Kai", "", Source::Manual, kNow);
    for (const auto& id : {kFriend, kGuest, kOther}) setOnList(b, id, "vips", true);
    setAnnouncements(b, kFriend, Announcements::OptedIn, kNow);
    setAnnouncements(b, kGuest, Announcements::OptedOut, kNow);
    const SendPreview a = previewAnnouncement(b, "vips", {});
    REQUIRE(a.recipients.size() == 1);
    CHECK(a.recipients[0].accountId == kFriend);
    CHECK(a.leftOut.size() == 2);
    // An opt-out also holds for event invitations.
    GuestList& gl = guestListFor(b, "evt2", "R&B Night", kHost);
    addGuest(gl, kHost, kGuest, "Rae");
    addGuest(gl, kHost, kOther, "Kai");
    setRsvp(gl, kHost, kOther, Rsvp::NotGoing);
    const SendPreview inv = previewInvitations(b, gl, kHost, {});
    CHECK(inv.recipients.empty());
    CHECK(inv.leftOut.size() == 2);
    // An RSVP with an explicit no records the opt-out.
    GuestList& gl3 = guestListFor(b, "evt3", "Sunday", kHost);
    acceptRsvp(b, gl3, {kFriend, "Nova", Rsvp::Maybe, false}, kNow);
    CHECK(b.contact(kFriend)->announcements == Announcements::OptedOut);
}

TEST_CASE("Sends are capped per batch", "[party]")
{
    Book b = starter();
    GuestList& gl = guestListFor(b, "big", "Big Night", kHost);
    for (int i = 1; i <= 45; ++i)
    {
        char id[37];
        std::snprintf(id, sizeof(id), "aaaaaaaa-aaaa-aaaa-aaaa-%012d", i);
        REQUIRE(addGuest(gl, kHost, id, "Guest").ok);
    }
    const SendPreview p = previewInvitations(b, gl, kHost, {});
    CHECK(p.recipients.size() == kMaxRecipientsPerSend);
    CHECK(p.leftOut.size() == 5);
}

TEST_CASE("Guest lists are private unless the owner shares them", "[party]")
{
    Book b = starter();
    saveContact(b, kFriend, "Nova", "", Source::Friend, kNow);
    b.contacts.at(kFriend).notes = "private note";
    GuestList& gl = guestListFor(b, "evt1", "Friday", kHost);
    addGuest(gl, kHost, kGuest, "Rae");
    CHECK_FALSE(allowed(gl, kCoHost, Permission::View));
    CHECK_FALSE(addGuest(gl, kCoHost, kOther, "Kai").ok);
    CHECK_FALSE(setShare(gl, kCoHost, {kCoHost, "Co", true, true}).ok); // Only the owner shares.
    REQUIRE(setShare(gl, kHost, {kCoHost, "Co", false, true}).ok);
    CHECK(allowed(gl, kCoHost, Permission::View));
    CHECK(allowed(gl, kCoHost, Permission::Invite));
    CHECK_FALSE(allowed(gl, kCoHost, Permission::Edit));
    CHECK_FALSE(allowed(gl, kCoHost, Permission::ManageSharing));
    CHECK_FALSE(addGuest(gl, kCoHost, kOther, "Kai").ok);
    CHECK(previewInvitations(b, gl, kCoHost, {}).recipients.size() == 1);
    CHECK(previewInvitations(b, gl, kOther, {}).recipients.empty());

    // The co-host's file carries this guest list and their own share only.
    setShare(gl, kHost, {kOther, "Other", true, false});
    const std::string file = writeShare(gl, kCoHost);
    CHECK(file.find("private note") == std::string::npos);
    CHECK(file.find("Nova") == std::string::npos);
    CHECK(file.find(kOther) == std::string::npos);
    const ShareReadResult r = readShare(file);
    REQUIRE(r.list);
    CHECK(r.list->guests.size() == 1);
    CHECK(r.list->shares.size() == 1);
    CHECK(allowed(*r.list, kCoHost, Permission::Invite));
    CHECK(removeShare(gl, kHost, kCoHost).ok);
    CHECK_FALSE(allowed(gl, kCoHost, Permission::View));
}

TEST_CASE("Party Contacts round-trip through their file", "[party]")
{
    Book b = starter();
    saveContact(b, kFriend, "Nova", "nova.resident", Source::Friend, kNow);
    setOnList(b, kFriend, "vips", true);
    setAnnouncements(b, kFriend, Announcements::OptedIn, kNow);
    GuestList& gl = guestListFor(b, "evt1", "Friday", kHost);
    acceptRsvp(b, gl, {kGuest, "Rae", Rsvp::Maybe, std::nullopt}, kNow);
    setShare(gl, kHost, {kCoHost, "Co", true, false});
    const ReadResult r = readBook(writeBook(b));
    REQUIRE(r.book);
    CHECK(r.book->contacts.size() == 2);
    CHECK(r.book->contact(kFriend)->lists.count("vips") == 1);
    CHECK(r.book->contact(kFriend)->announcements == Announcements::OptedIn);
    CHECK(r.book->guestLists.at("evt1").guest(kGuest)->rsvp == Rsvp::Maybe);
    CHECK(r.book->guestLists.at("evt1").shares.size() == 1);
    CHECK_FALSE(readBook("{\"format\":\"other\"}").book);
}
