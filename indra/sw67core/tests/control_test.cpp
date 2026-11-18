/**
 * @file control_test.cpp
 * @brief Event Control Room: schedule, countdowns, cue cards, announcement
 *        approval and the stream check.
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

#include <catch2/catch_test_macros.hpp>

#include <algorithm>

using namespace sw67::event;
using sw67::a11y::StatusRole;

namespace
{
bool has(const std::vector<StreamFinding>& f, StatusRole role, const std::string& part)
{
    return std::any_of(f.begin(), f.end(), [&](const StreamFinding& x) { return x.role == role && x.text.find(part) != std::string::npos; });
}
} // namespace

TEST_CASE("Countdowns move from upcoming to live to ended", "[control]")
{
    ScheduledEvent e;
    e.startEpoch = 100000;
    e.durationMinutes = 120;
    Countdown c = countdown(e, 100000 - 7500);
    CHECK(c.phase == Countdown::Phase::Upcoming);
    CHECK(c.text == "Starts in 2 h 05 min");
    CHECK(countdown(e, 100000 - 30).text == "Starts in under a minute");
    c = countdown(e, 100000 + 720);
    CHECK(c.phase == Countdown::Phase::Live);
    CHECK(c.text == "Live: 0 h 12 min in, 1 h 48 min left");
    CHECK(countdown(e, 100000 + 7200).phase == Countdown::Phase::Ended);
    CHECK(countdown(e, 100000 - 3 * 86400).text == "Starts in 3 days");
}

TEST_CASE("Schedule orders events and keeps IDs unique", "[control]")
{
    Schedule s;
    ScheduledEvent a;
    a.id = newEventId(s, 5000);
    a.startEpoch = 5000;
    s.events.push_back(a);
    ScheduledEvent b;
    b.id = newEventId(s, 5000);
    b.startEpoch = 1000;
    b.durationMinutes = 10;
    s.events.push_back(b);
    CHECK(a.id != b.id);
    const auto o = ordered(s, 2000); // b has ended
    REQUIRE(o.size() == 2);
    CHECK(o[0]->id == a.id);
    CHECK(o[1]->id == b.id);
}

TEST_CASE("Cue cards come due in a window", "[control]")
{
    ScheduledEvent e;
    e.startEpoch = 0;
    e.cards = {{"Welcome", "", 0}, {"Raffle", "", 30}, {"Thanks", "", 110}, {"House rules", "", -1}};
    auto due = cardsDue(e, 25 * 60, 10);
    REQUIRE(due.size() == 2);
    CHECK(due[0]->title == "House rules");
    CHECK(due[1]->title == "Raffle");
}

TEST_CASE("Announcements need approval of their exact text and send once", "[control]")
{
    Announcement a;
    CHECK_FALSE(approve(a).ok);
    editText(a, "Doors open at 9 SLT at the Lounge!");
    CHECK_FALSE(canSend(a));
    REQUIRE(approve(a).ok);
    CHECK(canSend(a));
    editText(a, "Doors open at 10 SLT at the Lounge!");
    CHECK(a.state == Announcement::State::Draft);
    CHECK_FALSE(canSend(a));
    REQUIRE(approve(a).ok);
    markSent(a, "2026-11-18 03:00:00");
    CHECK_FALSE(canSend(a));
    CHECK_FALSE(approve(a).ok);
    editText(a, "changed");
    CHECK(a.text == "Doors open at 10 SLT at the Lounge!");
    Announcement big;
    editText(big, std::string(1100, 'x'));
    CHECK_FALSE(approve(big).ok);
}

TEST_CASE("Stream check reports each problem in words", "[control]")
{
    StreamInputs in;
    CHECK(has(checkStream(in), StatusRole::Warning, "not on a parcel"));
    in.onParcel = true;
    CHECK(has(checkStream(in), StatusRole::Error, "no music stream"));
    in.parcelMusicUrl = "https://radio.example.com/live";
    in.plannedUrl = "https://radio.example.com/live/";
    in.musicEnabled = true;
    in.musicPlaying = true;
    in.musicVolume = 0.6f;
    auto f = checkStream(in);
    CHECK(has(f, StatusRole::Success, "hearing the parcel stream"));
    CHECK_FALSE(has(f, StatusRole::Warning, "not the event's planned"));
    in.plannedUrl = "https://other.example.com/x";
    CHECK(has(checkStream(in), StatusRole::Warning, "Ask the parcel's owner"));
    in.canSetParcelMusic = true;
    CHECK(has(checkStream(in), StatusRole::Warning, "About Land > Sound"));
    in.musicEnabled = false;
    CHECK(has(checkStream(in), StatusRole::Warning, "off in your own sound settings"));
    CHECK_FALSE(looksLikeStreamUrl("ftp://x.com"));
    CHECK_FALSE(looksLikeStreamUrl("https://local"));
    CHECK(looksLikeStreamUrl("http://123.45.67.89:8000/stream"));
}

TEST_CASE("Schedule round-trips, and an approval does not survive a changed text", "[control]")
{
    Schedule s;
    ScheduledEvent e;
    e.id = "evt-1";
    e.name = "Friday Night Live";
    e.startEpoch = 1795000000;
    e.team = {{"11111111-1111-1111-1111-111111111111", "Nova", Role::Dj}, {"", "Rae", Role::Host}};
    e.cards = {{"Welcome", "Say hi", 0}};
    Announcement a;
    editText(a, "Doors open!");
    approve(a);
    e.announcements.push_back(a);
    s.events.push_back(e);
    std::string text = writeSchedule(s);
    auto r = readSchedule(text);
    REQUIRE(r.schedule);
    const ScheduledEvent& back = r.schedule->events.at(0);
    CHECK(back.startEpoch == 1795000000);
    CHECK(back.team.at(0).role == Role::Dj);
    CHECK(canSend(back.announcements.at(0)));
    // Someone edits the file's text: the approval is dropped.
    const auto pos = text.find("\"text\": \"Doors open!\"");
    REQUIRE(pos != std::string::npos);
    text.replace(pos, 21, "\"text\": \"Free L$!!!\"");
    r = readSchedule(text);
    REQUIRE(r.schedule);
    CHECK_FALSE(canSend(r.schedule->events.at(0).announcements.at(0)));
    CHECK(parseDateTime("2026-11-20 21:00").has_value());
    CHECK_FALSE(parseDateTime("2026-13-20 21:00").has_value());
    CHECK(formatDateTime(*parseDateTime("2026-11-20 21:05")) == "2026-11-20 21:05");
}

TEST_CASE("Shot lists and what goes to the phone", "[control]")
{
    using namespace sw67::event;
    const auto shots = shotsFromText("  Wide of the stage \r\n\n DJ close-up\n" + std::string(300, 'x'));
    REQUIRE(shots.size() == 3);
    CHECK(shots[0] == "Wide of the stage");
    CHECK(shots[1] == "DJ close-up");
    CHECK(shots[2].size() == kMaxShotBytes);
    CHECK(shotsToText({"a", "b"}) == "a\nb");
    std::string many;
    for (int i = 0; i < 150; ++i) many += "shot " + std::to_string(i) + "\n";
    CHECK(shotsFromText(many).size() == kMaxShots);

    ScheduledEvent e;
    e.id = "evt-1-1";
    e.name = "Friday Night Live";
    e.startEpoch = 1800000000;
    e.durationMinutes = 90;
    e.location = "https://maps.secondlife.com/secondlife/Club/1/2/3";
    e.streamUrl = "http://stream.example/secret";
    e.notes = "Bring glow sticks";
    e.team.push_back({"11111111-2222-3333-4444-555555555555", "Mia", Role::Dj});
    e.shots = {"Wide", "Close"};
    Announcement a;
    a.text = "Doors at 8";
    e.announcements.push_back(a);
    const auto j = phonePayload(e, "Jeff");
    CHECK(j["id"].asString("") == "evt-1-1");
    CHECK(j["title"].asString("") == "Friday Night Live");
    CHECK(j["starts"].asNumber(0) == 1800000000.0);
    CHECK(j["minutes"].asInt(0) == 90);
    CHECK(j["details"].asString("") == "Bring glow sticks");
    CHECK(j["host_name"].asString("") == "Jeff");
    CHECK(j["shots"].items().size() == 2);
    const std::string text = sw67::io::writeJson(j);
    CHECK(text.find("stream.example") == std::string::npos);
    CHECK(text.find("11111111-2222") == std::string::npos);
    CHECK(text.find("Doors at 8") == std::string::npos);

    Schedule s;
    s.events.push_back(e);
    const auto back = readSchedule(writeSchedule(s));
    REQUIRE(back.schedule);
    REQUIRE(back.schedule->events.size() == 1);
    CHECK(back.schedule->events[0].shots == e.shots);
}
