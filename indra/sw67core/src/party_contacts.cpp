/**
 * @file party_contacts.cpp
 * @brief Party Contacts. See party.h.
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

#include "sw67/io/json.h"

#include <algorithm>
#include <cctype>

namespace sw67::contacts
{
namespace
{
using io::Json;

std::string lower(std::string_view s)
{
    std::string out(s);
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

std::string clip(std::string s, std::size_t n)
{
    if (s.size() > n) s.resize(n);
    return s;
}

Result ok(std::string m = {}) { return {true, std::move(m)}; }
Result no(std::string m) { return {false, std::move(m)}; }

template <typename E>
std::optional<E> parseKey(std::string_view key, std::string_view (*fn)(E), int count)
{
    for (int i = 0; i < count; ++i)
    {
        if (fn(static_cast<E>(i)) == key) return static_cast<E>(i);
    }
    return std::nullopt;
}

Guest* findGuest(GuestList& list, const std::string& id)
{
    for (Guest& g : list.guests)
    {
        if (g.accountId == id) return &g;
    }
    return nullptr;
}

Json guestListJson(const GuestList& gl, bool withShares)
{
    Json e = Json::object();
    e.set("event", Json(gl.eventId));
    e.set("event_name", Json(gl.eventName));
    e.set("owner", Json(gl.ownerId));
    Json guests = Json::array();
    for (const Guest& g : gl.guests)
    {
        Json j = Json::object();
        j.set("id", Json(g.accountId));
        j.set("name", Json(g.displayName));
        j.set("rsvp", Json(rsvpKey(g.rsvp)));
        if (!g.invitedAt.empty()) j.set("invited", Json(g.invitedAt));
        guests.push(std::move(j));
    }
    e.set("guests", std::move(guests));
    if (withShares)
    {
        Json shares = Json::array();
        for (const Share& s : gl.shares)
        {
            Json j = Json::object();
            j.set("id", Json(s.accountId));
            j.set("name", Json(s.name));
            j.set("edit", Json(s.canEdit));
            j.set("invite", Json(s.canInvite));
            shares.push(std::move(j));
        }
        e.set("shares", std::move(shares));
    }
    return e;
}

std::optional<GuestList> guestListFrom(const Json& e, std::vector<std::string>& notes)
{
    GuestList gl;
    gl.eventId = clip(e["event"].asString(""), 80);
    gl.eventName = clip(e["event_name"].asString(""), 120);
    gl.ownerId = lower(e["owner"].asString(""));
    if (gl.eventId.empty() || !isAccountId(gl.ownerId))
    {
        notes.push_back("A guest list without an event or owner was left out.");
        return std::nullopt;
    }
    std::set<std::string> seen;
    for (const Json& g : e["guests"].items())
    {
        Guest guest;
        guest.accountId = lower(g["id"].asString(""));
        if (!isAccountId(guest.accountId) || !seen.insert(guest.accountId).second) continue;
        guest.displayName = clip(g["name"].asString(""), 120);
        guest.rsvp = parseRsvp(g["rsvp"].asString("")).value_or(Rsvp::NotInvited);
        guest.invitedAt = clip(g["invited"].asString(""), 40);
        gl.guests.push_back(std::move(guest));
        if (gl.guests.size() >= kMaxContacts) break;
    }
    for (const Json& s : e["shares"].items())
    {
        Share share;
        share.accountId = lower(s["id"].asString(""));
        if (!isAccountId(share.accountId) || share.accountId == gl.ownerId) continue;
        share.name = clip(s["name"].asString(""), 120);
        share.canEdit = s["edit"].asBool(false);
        share.canInvite = s["invite"].asBool(false);
        gl.shares.push_back(std::move(share));
        if (gl.shares.size() >= 20) break;
    }
    return gl;
}
} // namespace

bool isAccountId(std::string_view id)
{
    if (id.size() != 36) return false;
    bool nonZero = false;
    for (std::size_t i = 0; i < id.size(); ++i)
    {
        const char c = id[i];
        if (i == 8 || i == 13 || i == 18 || i == 23)
        {
            if (c != '-') return false;
            continue;
        }
        if (!std::isxdigit(static_cast<unsigned char>(c))) return false;
        if (c != '0') nonZero = true;
    }
    return nonZero;
}

std::optional<std::string> findAccountId(std::string_view text)
{
    for (std::size_t i = 0; i + 36 <= text.size(); ++i)
    {
        if (isAccountId(text.substr(i, 36)))
        {
            const bool startOk = i == 0 || !std::isxdigit(static_cast<unsigned char>(text[i - 1]));
            const bool endOk = i + 36 == text.size() || !std::isxdigit(static_cast<unsigned char>(text[i + 36]));
            if (startOk && endOk) return lower(text.substr(i, 36));
        }
    }
    return std::nullopt;
}

std::string_view sourceKey(Source s)
{
    switch (s)
    {
        case Source::Manual: return "manual";
        case Source::Friend: return "friend";
        case Source::People: return "people";
        case Source::Rsvp: return "rsvp";
    }
    return "manual";
}

std::string_view announcementsKey(Announcements a)
{
    switch (a)
    {
        case Announcements::NotAsked: return "not_asked";
        case Announcements::OptedIn: return "opted_in";
        case Announcements::OptedOut: return "opted_out";
    }
    return "not_asked";
}

std::string_view rsvpKey(Rsvp r)
{
    switch (r)
    {
        case Rsvp::NotInvited: return "not_invited";
        case Rsvp::Invited: return "invited";
        case Rsvp::Going: return "going";
        case Rsvp::Maybe: return "maybe";
        case Rsvp::NotGoing: return "not_going";
    }
    return "not_invited";
}

std::string_view rsvpName(Rsvp r)
{
    switch (r)
    {
        case Rsvp::NotInvited: return "Not invited yet";
        case Rsvp::Invited: return "Invited, no answer";
        case Rsvp::Going: return "Going";
        case Rsvp::Maybe: return "Maybe";
        case Rsvp::NotGoing: return "Not going";
    }
    return "";
}

std::optional<Rsvp> parseRsvp(std::string_view key) { return parseKey<Rsvp>(key, &rsvpKey, 5); }

const Guest* GuestList::guest(const std::string& accountId) const
{
    for (const Guest& g : guests)
    {
        if (g.accountId == accountId) return &g;
    }
    return nullptr;
}

bool allowed(const GuestList& list, const std::string& accountId, Permission p)
{
    const std::string id = lower(accountId);
    if (!isAccountId(id)) return false;
    if (id == list.ownerId) return true;
    for (const Share& s : list.shares)
    {
        if (s.accountId != id) continue;
        switch (p)
        {
            case Permission::View: return true;
            case Permission::Edit: return s.canEdit;
            case Permission::Invite: return s.canInvite;
            case Permission::ManageSharing: return false;
        }
    }
    return false;
}

const Contact* Book::contact(const std::string& accountId) const
{
    auto it = contacts.find(lower(accountId));
    return it == contacts.end() ? nullptr : &it->second;
}

const ContactList* Book::list(const std::string& id) const
{
    for (const ContactList& l : lists)
    {
        if (l.id == id) return &l;
    }
    return nullptr;
}

std::vector<ContactList> starterLists()
{
    return {{"friday_night_regulars", "Friday Night Regulars"},
            {"rnb_night", "R&B Night"},
            {"vips", "VIPs"},
            {"venue_staff", "Venue Staff"}};
}

std::string listIdFor(std::string_view name)
{
    std::string out;
    for (char c : name)
    {
        const unsigned char u = static_cast<unsigned char>(c);
        if (std::isalnum(u)) out += static_cast<char>(std::tolower(u));
        else if (c == '&') out += "n";
        else if (!out.empty() && out.back() != '_') out += '_';
        if (out.size() >= 48) break;
    }
    while (!out.empty() && out.back() == '_') out.pop_back();
    return out;
}

Result saveContact(Book& book, const std::string& accountIdIn, const std::string& displayName,
                   const std::string& userName, Source source, const std::string& nowUtc)
{
    const std::string id = lower(accountIdIn);
    if (!isAccountId(id)) return no("That is not a Second Life account ID.");
    auto it = book.contacts.find(id);
    if (it != book.contacts.end())
    {
        if (!displayName.empty()) it->second.displayName = clip(displayName, 120);
        if (!userName.empty()) it->second.userName = clip(userName, 64);
        return ok(it->second.displayName + " is already a party contact; their name is up to date.");
    }
    if (book.contacts.size() >= kMaxContacts) return no("Party Contacts holds up to 5,000 people.");
    Contact c;
    c.accountId = id;
    c.displayName = clip(displayName.empty() ? userName : displayName, 120);
    c.userName = clip(userName, 64);
    c.source = source;
    c.added = nowUtc;
    book.contacts.emplace(id, std::move(c));
    return ok("Saved " + (displayName.empty() ? userName : displayName) +
              " to Party Contacts. No friend request was sent and they were not signed up for announcements.");
}

Result removeContact(Book& book, const std::string& accountId)
{
    auto it = book.contacts.find(lower(accountId));
    if (it == book.contacts.end()) return no("They are not a party contact.");
    const std::string name = it->second.displayName;
    book.contacts.erase(it);
    return ok("Removed " + name + " from Party Contacts and its lists. Any Second Life friendship is unchanged.");
}

Result setAnnouncements(Book& book, const std::string& accountId, Announcements a, const std::string& nowUtc)
{
    auto it = book.contacts.find(lower(accountId));
    if (it == book.contacts.end()) return no("They are not a party contact.");
    it->second.announcements = a;
    it->second.consentChanged = nowUtc;
    return ok();
}

Result addList(Book& book, const std::string& name)
{
    std::string id = listIdFor(name);
    if (id.empty()) return no("Give the list a name.");
    if (book.list(id)) return no("A list with that name already exists.");
    if (book.lists.size() >= kMaxLists) return no("Party Contacts holds up to 100 lists.");
    book.lists.push_back({id, clip(name, 80)});
    return ok(id);
}

Result renameList(Book& book, const std::string& id, const std::string& name)
{
    for (ContactList& l : book.lists)
    {
        if (l.id == id)
        {
            if (name.find_first_not_of(" \t") == std::string::npos) return no("Give the list a name.");
            l.name = clip(name, 80);
            return ok();
        }
    }
    return no("No such list.");
}

Result removeList(Book& book, const std::string& id)
{
    const auto before = book.lists.size();
    book.lists.erase(std::remove_if(book.lists.begin(), book.lists.end(), [&](const ContactList& l) { return l.id == id; }),
                     book.lists.end());
    if (book.lists.size() == before) return no("No such list.");
    for (auto& [_, c] : book.contacts) c.lists.erase(id);
    return ok();
}

Result setOnList(Book& book, const std::string& accountId, const std::string& listId, bool on)
{
    auto it = book.contacts.find(lower(accountId));
    if (it == book.contacts.end()) return no("Save them as a party contact first.");
    if (!book.list(listId)) return no("No such list.");
    if (on) it->second.lists.insert(listId);
    else it->second.lists.erase(listId);
    return ok();
}

std::vector<const Contact*> members(const Book& book, const std::string& listId)
{
    std::vector<const Contact*> out;
    for (const auto& [_, c] : book.contacts)
    {
        if (c.lists.count(listId)) out.push_back(&c);
    }
    std::sort(out.begin(), out.end(), [](const Contact* a, const Contact* b) { return lower(a->displayName) < lower(b->displayName); });
    return out;
}

GuestList& guestListFor(Book& book, const std::string& eventId, const std::string& eventName, const std::string& ownerId)
{
    GuestList& gl = book.guestLists[eventId];
    if (gl.eventId.empty())
    {
        gl.eventId = eventId;
        gl.ownerId = lower(ownerId);
    }
    if (!eventName.empty()) gl.eventName = eventName;
    return gl;
}

Result addGuest(GuestList& list, const std::string& actor, const std::string& accountId, const std::string& displayName)
{
    if (!allowed(list, actor, Permission::Edit)) return no("You can't change this guest list.");
    const std::string id = lower(accountId);
    if (!isAccountId(id)) return no("That is not a Second Life account ID.");
    if (Guest* g = findGuest(list, id))
    {
        if (!displayName.empty()) g->displayName = clip(displayName, 120);
        return ok("Already on the guest list.");
    }
    if (list.guests.size() >= kMaxContacts) return no("The guest list is full.");
    list.guests.push_back({id, clip(displayName, 120), Rsvp::NotInvited, {}});
    return ok();
}

std::size_t addListToGuests(const Book& book, GuestList& list, const std::string& actor, const std::string& listId)
{
    std::size_t added = 0;
    for (const Contact* c : members(book, listId))
    {
        if (!findGuest(list, c->accountId) && addGuest(list, actor, c->accountId, c->displayName).ok) ++added;
    }
    return added;
}

Result removeGuest(GuestList& list, const std::string& actor, const std::string& accountId)
{
    if (!allowed(list, actor, Permission::Edit)) return no("You can't change this guest list.");
    const std::string id = lower(accountId);
    const auto before = list.guests.size();
    list.guests.erase(std::remove_if(list.guests.begin(), list.guests.end(), [&](const Guest& g) { return g.accountId == id; }),
                      list.guests.end());
    return list.guests.size() == before ? no("Not on the guest list.") : ok();
}

Result setRsvp(GuestList& list, const std::string& actor, const std::string& accountId, Rsvp r)
{
    if (!allowed(list, actor, Permission::Edit)) return no("You can't change this guest list.");
    Guest* g = findGuest(list, lower(accountId));
    if (!g) return no("Not on the guest list.");
    g->rsvp = r;
    return ok();
}

Result setShare(GuestList& list, const std::string& actor, const Share& shareIn)
{
    if (!allowed(list, actor, Permission::ManageSharing)) return no("Only the event's owner can share its guest list.");
    Share share = shareIn;
    share.accountId = lower(share.accountId);
    if (!isAccountId(share.accountId)) return no("That is not a Second Life account ID.");
    if (share.accountId == list.ownerId) return no("The owner already has every right.");
    for (Share& s : list.shares)
    {
        if (s.accountId == share.accountId)
        {
            s = share;
            return ok();
        }
    }
    if (list.shares.size() >= 20) return no("A guest list can be shared with up to 20 co-hosts.");
    list.shares.push_back(share);
    return ok();
}

Result removeShare(GuestList& list, const std::string& actor, const std::string& accountId)
{
    if (!allowed(list, actor, Permission::ManageSharing)) return no("Only the event's owner can change sharing.");
    const std::string id = lower(accountId);
    const auto before = list.shares.size();
    list.shares.erase(std::remove_if(list.shares.begin(), list.shares.end(), [&](const Share& s) { return s.accountId == id; }),
                      list.shares.end());
    return list.shares.size() == before ? no("Not shared with them.") : ok();
}

Result acceptRsvp(Book& book, GuestList& list, const RsvpRecord& rsvp, const std::string& nowUtc)
{
    const std::string id = lower(rsvp.accountId);
    if (!isAccountId(id)) return no("The RSVP has no valid account ID.");
    Result added = addGuest(list, list.ownerId, id, rsvp.displayName);
    if (!added.ok) return added;
    setRsvp(list, list.ownerId, id, rsvp.answer);
    saveContact(book, id, rsvp.displayName, {}, Source::Rsvp, nowUtc);
    if (rsvp.wantsAnnouncements.has_value())
    {
        setAnnouncements(book, id, *rsvp.wantsAnnouncements ? Announcements::OptedIn : Announcements::OptedOut, nowUtc);
    }
    return ok("Added " + rsvp.displayName + " to the guest list as " + std::string(rsvpName(rsvp.answer)) + ".");
}

std::string SendPreview::describe() const
{
    std::string out = purpose == Purpose::Invitation ? "Invitations would go to " : "The announcement would go to ";
    out += std::to_string(recipients.size()) + " resident(s), one IM each, about " +
           std::to_string(static_cast<int>(kSendSpacingSeconds)) + " seconds apart:\n";
    for (const Recipient& r : recipients) out += "  - " + r.displayName + "\n";
    if (!leftOut.empty())
    {
        out += "\nLeft out (" + std::to_string(leftOut.size()) + "):\n";
        for (const Left& l : leftOut) out += "  - " + l.displayName + ": " + l.reason + "\n";
    }
    out += "\nNothing is sent until you press Send and confirm.";
    return out;
}

SendPreview previewInvitations(const Book& book, const GuestList& list, const std::string& actor,
                               const std::set<std::string>& removedByHost)
{
    SendPreview p;
    p.purpose = Purpose::Invitation;
    if (!allowed(list, actor, Permission::Invite))
    {
        for (const Guest& g : list.guests) p.leftOut.push_back({g.accountId, g.displayName, "you can't send invitations for this event"});
        return p;
    }
    for (const Guest& g : list.guests)
    {
        const Contact* c = book.contact(g.accountId);
        const std::string name = g.displayName.empty() && c ? c->displayName : g.displayName;
        if (removedByHost.count(g.accountId)) p.leftOut.push_back({g.accountId, name, "you removed them from this send"});
        else if (c && c->announcements == Announcements::OptedOut) p.leftOut.push_back({g.accountId, name, "opted out of messages"});
        else if (g.rsvp == Rsvp::NotGoing) p.leftOut.push_back({g.accountId, name, "already said Not going"});
        else if (p.recipients.size() >= kMaxRecipientsPerSend) p.leftOut.push_back({g.accountId, name, "over the 40-per-send limit; send again for the rest"});
        else p.recipients.push_back({g.accountId, name});
    }
    return p;
}

SendPreview previewAnnouncement(const Book& book, const std::string& listId, const std::set<std::string>& removedByHost)
{
    SendPreview p;
    p.purpose = Purpose::Announcement;
    for (const Contact* c : members(book, listId))
    {
        if (removedByHost.count(c->accountId)) p.leftOut.push_back({c->accountId, c->displayName, "you removed them from this send"});
        else if (c->announcements == Announcements::OptedOut) p.leftOut.push_back({c->accountId, c->displayName, "opted out of messages"});
        else if (c->announcements == Announcements::NotAsked) p.leftOut.push_back({c->accountId, c->displayName, "has not opted in to announcements"});
        else if (p.recipients.size() >= kMaxRecipientsPerSend) p.leftOut.push_back({c->accountId, c->displayName, "over the 40-per-send limit; send again for the rest"});
        else p.recipients.push_back({c->accountId, c->displayName});
    }
    return p;
}

void markInvited(GuestList& list, const std::vector<std::string>& sentTo, const std::string& nowUtc)
{
    for (const std::string& id : sentTo)
    {
        if (Guest* g = findGuest(list, lower(id)))
        {
            if (g->rsvp == Rsvp::NotInvited) g->rsvp = Rsvp::Invited;
            g->invitedAt = nowUtc;
        }
    }
}

std::string writeBook(const Book& book)
{
    Json root = Json::object();
    root.set("format", Json(kFormat));
    root.set("version", Json(kFormatVersion));
    Json lists = Json::array();
    for (const ContactList& l : book.lists)
    {
        Json e = Json::object();
        e.set("id", Json(l.id));
        e.set("name", Json(l.name));
        lists.push(std::move(e));
    }
    root.set("lists", std::move(lists));
    Json contacts = Json::array();
    for (const auto& [id, c] : book.contacts)
    {
        Json e = Json::object();
        e.set("id", Json(id));
        e.set("name", Json(c.displayName));
        e.set("user", Json(c.userName));
        e.set("source", Json(sourceKey(c.source)));
        e.set("notes", Json(c.notes));
        e.set("announcements", Json(announcementsKey(c.announcements)));
        if (!c.consentChanged.empty()) e.set("consent_changed", Json(c.consentChanged));
        e.set("added", Json(c.added));
        Json ls = Json::array();
        for (const std::string& l : c.lists) ls.push(Json(l));
        e.set("lists", std::move(ls));
        contacts.push(std::move(e));
    }
    root.set("contacts", std::move(contacts));
    Json guests = Json::array();
    for (const auto& [_, gl] : book.guestLists) guests.push(guestListJson(gl, true));
    root.set("guest_lists", std::move(guests));
    return io::writeJson(root);
}

ReadResult readBook(std::string_view text)
{
    ReadResult r;
    const auto root = io::parseJson(text);
    if (!root || !root->isObject() || (*root)["format"].asString("") != kFormat)
    {
        r.notes.push_back("This is not a 6|7 Party Contacts file.");
        return r;
    }
    const int version = (*root)["version"].asInt(0);
    if (version < 1 || version > kFormatVersion)
    {
        r.notes.push_back("This Party Contacts file was saved by a newer 6|7 World Studio.");
        return r;
    }
    Book b;
    for (const Json& e : (*root)["lists"].items())
    {
        const std::string id = listIdFor(e["id"].asString(""));
        if (id.empty() || b.list(id) || b.lists.size() >= kMaxLists) continue;
        b.lists.push_back({id, clip(e["name"].asString(id), 80)});
    }
    for (const Json& e : (*root)["contacts"].items())
    {
        Contact c;
        c.accountId = lower(e["id"].asString(""));
        if (!isAccountId(c.accountId) || b.contacts.count(c.accountId)) continue;
        c.displayName = clip(e["name"].asString(""), 120);
        c.userName = clip(e["user"].asString(""), 64);
        c.source = parseKey<Source>(e["source"].asString(""), &sourceKey, 4).value_or(Source::Manual);
        c.notes = clip(e["notes"].asString(""), 2000);
        c.announcements = parseKey<Announcements>(e["announcements"].asString(""), &announcementsKey, 3).value_or(Announcements::NotAsked);
        c.consentChanged = clip(e["consent_changed"].asString(""), 40);
        c.added = clip(e["added"].asString(""), 40);
        for (const Json& l : e["lists"].items())
        {
            if (b.list(l.asString(""))) c.lists.insert(l.asString(""));
        }
        b.contacts.emplace(c.accountId, std::move(c));
        if (b.contacts.size() >= kMaxContacts) break;
    }
    for (const Json& e : (*root)["guest_lists"].items())
    {
        if (auto gl = guestListFrom(e, r.notes)) b.guestLists[gl->eventId] = std::move(*gl);
    }
    r.book = std::move(b);
    return r;
}

std::string writeShare(const GuestList& list, const std::string& forAccountId)
{
    Json root = Json::object();
    root.set("format", Json(kShareFormat));
    root.set("version", Json(kFormatVersion));
    GuestList only = list;
    only.shares.clear();
    for (const Share& s : list.shares)
    {
        if (s.accountId == lower(forAccountId)) only.shares.push_back(s);
    }
    root.set("guest_list", guestListJson(only, true));
    return io::writeJson(root);
}

ShareReadResult readShare(std::string_view text)
{
    ShareReadResult r;
    const auto root = io::parseJson(text);
    if (!root || !root->isObject() || (*root)["format"].asString("") != kShareFormat)
    {
        r.notes.push_back("This is not a shared 6|7 guest list.");
        return r;
    }
    if ((*root)["version"].asInt(0) != kFormatVersion)
    {
        r.notes.push_back("This guest list was shared from a newer 6|7 World Studio.");
        return r;
    }
    r.list = guestListFrom((*root)["guest_list"], r.notes);
    return r;
}

} // namespace sw67::contacts
