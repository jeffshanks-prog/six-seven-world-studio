/**
 * @file event_control.cpp
 * @brief Event Control Room. See control.h.
 *
 * Copyright (c) 2026, 6|7 World Studio contributors.
 *
 * The source code in this file is provided to you under the terms of the
 * GNU Lesser General Public License, version 2.1, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE. Terms of the LGPL can be found in doc/LGPL-license.txt
 * in this distribution, or online at http://www.gnu.org/licenses/lgpl-2.1.txt
 */

#include "sw67/event/control.h"

#include "sw67/io/json.h"

#include <algorithm>
#include <cctype>
#include <cstdio>

namespace sw67::event
{
namespace
{
using a11y::StatusRole;
using io::Json;

std::string clip(std::string s, std::size_t n)
{
    if (s.size() > n) s.resize(n);
    return s;
}

std::string span(std::int64_t seconds)
{
    const std::int64_t minutes = seconds / 60;
    const std::int64_t days = minutes / (60 * 24);
    char buf[64];
    if (days >= 2) std::snprintf(buf, sizeof(buf), "%lld days", static_cast<long long>(days));
    else std::snprintf(buf, sizeof(buf), "%lld h %02lld min", static_cast<long long>(minutes / 60), static_cast<long long>(minutes % 60));
    return buf;
}

std::string lowerHost(std::string_view url)
{
    const std::size_t s = url.find("://");
    if (s == std::string_view::npos) return {};
    std::string out;
    for (std::size_t i = s + 3; i < url.size() && url[i] != '/' && url[i] != '?'; ++i)
    {
        out += static_cast<char>(std::tolower(static_cast<unsigned char>(url[i])));
    }
    return out;
}

std::string trimSlash(std::string s)
{
    while (!s.empty() && (s.back() == '/' || s.back() == ' ')) s.pop_back();
    return s;
}
} // namespace

std::string_view roleKey(Role r)
{
    switch (r)
    {
        case Role::Dj: return "dj";
        case Role::Host: return "host";
        case Role::CoHost: return "cohost";
        case Role::Staff: return "staff";
    }
    return "host";
}

std::string_view roleName(Role r)
{
    switch (r)
    {
        case Role::Dj: return "DJ";
        case Role::Host: return "Host";
        case Role::CoHost: return "Co-host";
        case Role::Staff: return "Staff";
    }
    return "Host";
}

std::optional<Role> parseRole(std::string_view key)
{
    for (Role r : {Role::Dj, Role::Host, Role::CoHost, Role::Staff})
    {
        if (roleKey(r) == key) return r;
    }
    return std::nullopt;
}

ScheduledEvent* Schedule::find(const std::string& id)
{
    for (ScheduledEvent& e : events)
    {
        if (e.id == id) return &e;
    }
    return nullptr;
}

const ScheduledEvent* Schedule::find(const std::string& id) const
{
    return const_cast<Schedule*>(this)->find(id);
}

std::string newEventId(const Schedule& s, std::int64_t startEpoch)
{
    for (int n = 1;; ++n)
    {
        const std::string id = "evt-" + std::to_string(startEpoch) + "-" + std::to_string(n);
        if (!s.find(id)) return id;
    }
}

std::vector<const ScheduledEvent*> ordered(const Schedule& s, std::int64_t nowEpoch)
{
    std::vector<const ScheduledEvent*> current, past;
    for (const ScheduledEvent& e : s.events)
    {
        (countdown(e, nowEpoch).phase == Countdown::Phase::Ended ? past : current).push_back(&e);
    }
    std::sort(current.begin(), current.end(), [](auto* a, auto* b) { return a->startEpoch < b->startEpoch; });
    std::sort(past.begin(), past.end(), [](auto* a, auto* b) { return a->startEpoch > b->startEpoch; });
    current.insert(current.end(), past.begin(), past.end());
    return current;
}

Countdown countdown(const ScheduledEvent& e, std::int64_t now)
{
    Countdown c;
    const std::int64_t end = e.startEpoch + static_cast<std::int64_t>(std::max(1, e.durationMinutes)) * 60;
    if (now < e.startEpoch)
    {
        c.phase = Countdown::Phase::Upcoming;
        c.seconds = e.startEpoch - now;
        c.text = c.seconds < 60 ? "Starts in under a minute" : "Starts in " + span(c.seconds);
    }
    else if (now < end)
    {
        c.phase = Countdown::Phase::Live;
        c.seconds = now - e.startEpoch;
        c.text = "Live: " + span(c.seconds) + " in, " + span(end - now) + " left";
    }
    else
    {
        c.phase = Countdown::Phase::Ended;
        c.seconds = now - end;
        c.text = "Ended";
    }
    return c;
}

std::vector<const CueCard*> cardsDue(const ScheduledEvent& e, std::int64_t now, int windowMinutes)
{
    std::vector<const CueCard*> out;
    const std::int64_t minuteNow = (now - e.startEpoch) / 60;
    for (const CueCard& c : e.cards)
    {
        if (c.atMinute < 0 || (c.atMinute >= minuteNow && c.atMinute <= minuteNow + windowMinutes)) out.push_back(&c);
    }
    std::stable_sort(out.begin(), out.end(), [](auto* a, auto* b) { return a->atMinute < b->atMinute; });
    return out;
}

Result approve(Announcement& a)
{
    if (a.state == Announcement::State::Sent) return {false, "This announcement was already sent."};
    if (a.text.find_first_not_of(" \t\r\n") == std::string::npos) return {false, "Write the announcement first."};
    if (a.text.size() > kMaxAnnouncementBytes) return {false, "Keep it to one chat line (1,023 bytes)."};
    a.state = Announcement::State::Approved;
    a.approvedText = a.text;
    return {true, "Approved. It is sent only when you press Send and confirm."};
}

void editText(Announcement& a, const std::string& text)
{
    if (a.state == Announcement::State::Sent || text == a.text) return;
    a.text = text;
    a.state = Announcement::State::Draft;
    a.approvedText.clear();
}

bool canSend(const Announcement& a)
{
    return a.state == Announcement::State::Approved && a.approvedText == a.text && !a.text.empty() &&
           a.text.size() <= kMaxAnnouncementBytes;
}

void markSent(Announcement& a, const std::string& nowUtc)
{
    a.state = Announcement::State::Sent;
    a.sentAt = nowUtc;
}

std::string_view stateName(Announcement::State s)
{
    switch (s)
    {
        case Announcement::State::Draft: return "Draft";
        case Announcement::State::Approved: return "Approved";
        case Announcement::State::Sent: return "Sent";
    }
    return "Draft";
}

bool looksLikeStreamUrl(std::string_view url)
{
    if (url.rfind("http://", 0) != 0 && url.rfind("https://", 0) != 0) return false;
    if (url.find_first_of(" \t\r\n\"<>") != std::string_view::npos) return false;
    const std::string host = lowerHost(url);
    return !host.empty() && host.find('.') != std::string::npos;
}

std::vector<StreamFinding> checkStream(const StreamInputs& in)
{
    std::vector<StreamFinding> f;
    if (!in.plannedUrl.empty() && !looksLikeStreamUrl(in.plannedUrl))
    {
        f.push_back({StatusRole::Error, "The event's planned stream is not an http or https address."});
    }
    if (!in.onParcel)
    {
        f.push_back({StatusRole::Warning, "You're not on a parcel, so there is no parcel music to check."});
        return f;
    }
    if (in.parcelMusicUrl.empty())
    {
        f.push_back({StatusRole::Error, "This parcel has no music stream set."});
    }
    else if (!looksLikeStreamUrl(in.parcelMusicUrl))
    {
        f.push_back({StatusRole::Error, "The parcel's music address is not an http or https address."});
    }
    else
    {
        f.push_back({StatusRole::Success, "The parcel has a music stream set."});
        if (in.parcelMusicUrl.rfind("http://", 0) == 0)
        {
            f.push_back({StatusRole::Info, "The parcel stream uses http; some listeners' viewers prefer https."});
        }
        if (!in.plannedUrl.empty() && trimSlash(in.plannedUrl) != trimSlash(in.parcelMusicUrl))
        {
            f.push_back({StatusRole::Warning, std::string("The parcel's stream is not the event's planned stream.") +
                                                  (in.canSetParcelMusic ? " Change it in About Land > Sound."
                                                                        : " Ask the parcel's owner or a manager to change it.")});
        }
    }
    if (!in.musicEnabled)
    {
        f.push_back({StatusRole::Warning, "Streaming music is off in your own sound settings, so you won't hear the stream."});
    }
    else if (!in.musicPlaying)
    {
        f.push_back({StatusRole::Warning, "Your viewer isn't playing the parcel stream right now. Press play on the music control to listen."});
    }
    else
    {
        f.push_back({StatusRole::Success, "You're hearing the parcel stream."});
        if (in.musicVolume < 0.05f) f.push_back({StatusRole::Warning, "Your music volume is nearly silent."});
    }
    f.push_back({StatusRole::Info, "This checks what this viewer plays. Listeners' own settings and the broadcast "
                                   "app you stream from are outside it."});
    return f;
}

std::optional<DateTime> parseDateTime(std::string_view text)
{
    DateTime t;
    int n = 0;
    const std::string s(text);
    if (std::sscanf(s.c_str(), "%d-%d-%d %d:%d%n", &t.year, &t.month, &t.day, &t.hour, &t.minute, &n) != 5) return std::nullopt;
    if (static_cast<std::size_t>(n) != s.size()) return std::nullopt;
    static constexpr int kDays[] = {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (t.year < 2000 || t.year > 2100 || t.month < 1 || t.month > 12 || t.day < 1 || t.day > kDays[t.month - 1] ||
        t.hour < 0 || t.hour > 23 || t.minute < 0 || t.minute > 59)
    {
        return std::nullopt;
    }
    return t;
}

std::string formatDateTime(const DateTime& t)
{
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d", t.year, t.month, t.day, t.hour, t.minute);
    return buf;
}

std::string writeSchedule(const Schedule& s)
{
    Json root = Json::object();
    root.set("format", Json(kScheduleFormat));
    root.set("version", Json(kScheduleFormatVersion));
    Json events = Json::array();
    for (const ScheduledEvent& e : s.events)
    {
        Json j = Json::object();
        j.set("id", Json(e.id));
        j.set("name", Json(e.name));
        j.set("start", Json(static_cast<long long>(e.startEpoch)));
        j.set("minutes", Json(e.durationMinutes));
        j.set("location", Json(e.location));
        j.set("stream", Json(e.streamUrl));
        j.set("rehearsal", Json(e.rehearsalPlan));
        j.set("notes", Json(e.notes));
        Json team = Json::array();
        for (const TeamMember& m : e.team)
        {
            Json t = Json::object();
            t.set("id", Json(m.accountId));
            t.set("name", Json(m.name));
            t.set("role", Json(roleKey(m.role)));
            team.push(std::move(t));
        }
        j.set("team", std::move(team));
        Json cards = Json::array();
        for (const CueCard& c : e.cards)
        {
            Json t = Json::object();
            t.set("title", Json(c.title));
            t.set("body", Json(c.body));
            t.set("at", Json(c.atMinute));
            cards.push(std::move(t));
        }
        j.set("cards", std::move(cards));
        Json anns = Json::array();
        for (const Announcement& a : e.announcements)
        {
            Json t = Json::object();
            t.set("text", Json(a.text));
            t.set("state", Json(std::string(stateName(a.state))));
            if (a.state == Announcement::State::Approved) t.set("approved_text", Json(a.approvedText));
            if (!a.sentAt.empty()) t.set("sent", Json(a.sentAt));
            anns.push(std::move(t));
        }
        j.set("announcements", std::move(anns));
        events.push(std::move(j));
    }
    root.set("events", std::move(events));
    return io::writeJson(root);
}

ScheduleReadResult readSchedule(std::string_view text)
{
    ScheduleReadResult r;
    const auto root = io::parseJson(text);
    if (!root || !root->isObject() || (*root)["format"].asString("") != kScheduleFormat)
    {
        r.notes.push_back("This is not a 6|7 event schedule.");
        return r;
    }
    const int version = (*root)["version"].asInt(0);
    if (version < 1 || version > kScheduleFormatVersion)
    {
        r.notes.push_back("This schedule was saved by a newer 6|7 World Studio.");
        return r;
    }
    Schedule s;
    for (const Json& j : (*root)["events"].items())
    {
        if (s.events.size() >= kMaxEvents) break;
        ScheduledEvent e;
        e.id = clip(j["id"].asString(""), 64);
        if (e.id.empty() || s.find(e.id)) continue;
        e.name = clip(j["name"].asString("Event"), 120);
        e.startEpoch = static_cast<std::int64_t>(j["start"].asNumber(0));
        e.durationMinutes = std::clamp(j["minutes"].asInt(120), 1, 24 * 60);
        e.location = clip(j["location"].asString(""), 300);
        e.streamUrl = clip(j["stream"].asString(""), 1000);
        e.rehearsalPlan = clip(j["rehearsal"].asString(""), 120);
        e.notes = clip(j["notes"].asString(""), 4000);
        for (const Json& t : j["team"].items())
        {
            if (e.team.size() >= 50) break;
            e.team.push_back({clip(t["id"].asString(""), 36), clip(t["name"].asString(""), 120),
                              parseRole(t["role"].asString("")).value_or(Role::Host)});
        }
        for (const Json& t : j["cards"].items())
        {
            if (e.cards.size() >= 200) break;
            e.cards.push_back({clip(t["title"].asString(""), 120), clip(t["body"].asString(""), 2000),
                               std::clamp(t["at"].asInt(-1), -1, 24 * 60)});
        }
        for (const Json& t : j["announcements"].items())
        {
            if (e.announcements.size() >= 100) break;
            Announcement a;
            a.text = clip(t["text"].asString(""), kMaxAnnouncementBytes * 2);
            const std::string st = t["state"].asString("Draft");
            a.sentAt = clip(t["sent"].asString(""), 40);
            if (st == "Sent") a.state = Announcement::State::Sent;
            else if (st == "Approved" && t["approved_text"].asString("") == a.text)
            {
                a.state = Announcement::State::Approved;
                a.approvedText = a.text;
            }
            e.announcements.push_back(std::move(a));
        }
        s.events.push_back(std::move(e));
    }
    r.schedule = std::move(s);
    return r;
}

} // namespace sw67::event
