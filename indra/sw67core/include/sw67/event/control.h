/**
 * @file control.h
 * @brief Event Control Room for DJs and hosts: a schedule of events, each
 *        with its DJ and host team, cue cards, a countdown, a stream check,
 *        and announcement drafts.
 *
 *        An announcement is a draft until the host approves its exact text;
 *        editing it after approval makes it a draft again, and only an
 *        approved, unsent announcement can be sent, once. Sending is the
 *        viewer's job and asks first; nothing here sends. Rehearsal
 *        (rehearsal.h) stays send-free: it cannot reach this module.
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

#include "sw67/a11y/profile.h"
#include "sw67/io/json.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sw67::event
{

inline constexpr std::string_view kScheduleFormat = "sw67.events";
inline constexpr int kScheduleFormatVersion = 1;
inline constexpr std::size_t kMaxEvents = 200;
inline constexpr std::size_t kMaxAnnouncementBytes = 1023; ///< One nearby-chat line.

enum class Role
{
    Dj,
    Host,
    CoHost,
    Staff,
};
std::string_view roleKey(Role r);
std::string_view roleName(Role r);
std::optional<Role> parseRole(std::string_view key);

struct TeamMember
{
    std::string accountId; ///< May be empty for someone not yet looked up.
    std::string name;
    Role role = Role::Host;
};

struct CueCard
{
    std::string title;
    std::string body;
    int atMinute = -1; ///< Minutes from the start, or -1 for "any time".
};

struct Announcement
{
    enum class State
    {
        Draft,
        Approved,
        Sent,
    };
    std::string text;
    State state = State::Draft;
    std::string approvedText; ///< The exact text the host approved.
    std::string sentAt;       ///< UTC text.
};

struct ScheduledEvent
{
    std::string id;
    std::string name;
    std::int64_t startEpoch = 0; ///< Seconds since 1970, UTC.
    int durationMinutes = 120;
    std::string location;  ///< A Second Life address, for the host's reference.
    std::string streamUrl; ///< The music stream the DJ plans to use.
    std::string rehearsalPlan; ///< Name of an Event Rehearsal plan, if any.
    std::string notes;
    std::vector<TeamMember> team;
    std::vector<CueCard> cards;
    std::vector<Announcement> announcements;
    std::vector<std::string> shots; ///< The shot list the 6|7 Phone Companion ticks off.
};

struct Schedule
{
    std::vector<ScheduledEvent> events;
    ScheduledEvent* find(const std::string& id);
    const ScheduledEvent* find(const std::string& id) const;
};

inline constexpr std::size_t kMaxShots = 100;
inline constexpr std::size_t kMaxShotBytes = 200;

/// One shot a line; blank lines dropped, each line trimmed and cut to
/// kMaxShotBytes, at most kMaxShots.
std::vector<std::string> shotsFromText(std::string_view text);
std::string shotsToText(const std::vector<std::string>& shots);

/// What the viewer sends to the 6|7 Phone Companion for an event: its name,
/// start, length, location, notes, shot list and the host's display name.
/// Never the team's account IDs, the stream address or announcements.
sw67::io::Json phonePayload(const ScheduledEvent& e, const std::string& hostName);

/// "evt-<start>-<n>", unique within the schedule.
std::string newEventId(const Schedule& s, std::int64_t startEpoch);
/// Upcoming and live events first by start time, then ended ones, latest first.
std::vector<const ScheduledEvent*> ordered(const Schedule& s, std::int64_t nowEpoch);

struct Countdown
{
    enum class Phase
    {
        Upcoming,
        Live,
        Ended,
    };
    Phase phase = Phase::Upcoming;
    std::int64_t seconds = 0; ///< To the start, since the start, or since the end.
    std::string text;         ///< "Starts in 2 h 05 min", "Live: 0 h 12 min in, 1 h 48 min left", "Ended".
};
Countdown countdown(const ScheduledEvent& e, std::int64_t nowEpoch);

/// The cue cards due in the next `windowMinutes` of a live event, and any
/// "any time" cards.
std::vector<const CueCard*> cardsDue(const ScheduledEvent& e, std::int64_t nowEpoch, int windowMinutes = 10);

struct Result
{
    bool ok = false;
    std::string message;
};

/// Approval of the exact text. Fails on empty text or text past one chat line.
Result approve(Announcement& a);
/// Edits the text; an approved announcement becomes a draft again.
void editText(Announcement& a, const std::string& text);
/// True only for an approved, unsent announcement whose text is still the approved text.
bool canSend(const Announcement& a);
void markSent(Announcement& a, const std::string& nowUtc);
std::string_view stateName(Announcement::State s);

/// What the stream check looks at, as the viewer has it now.
struct StreamInputs
{
    bool onParcel = false;          ///< The agent stands on a parcel.
    std::string parcelMusicUrl;     ///< The parcel's music URL.
    bool musicEnabled = false;      ///< The listener's own "Play streaming music" setting.
    bool musicPlaying = false;      ///< This viewer is playing the parcel stream now.
    float musicVolume = 0.f;        ///< 0..1, this viewer's music volume.
    std::string plannedUrl;         ///< The event's planned stream.
    bool canSetParcelMusic = false; ///< The agent may change this parcel's music URL.
};

struct StreamFinding
{
    a11y::StatusRole role = a11y::StatusRole::Info;
    std::string text;
};

/// Checks a URL as a stream address: http or https, a host, no spaces.
bool looksLikeStreamUrl(std::string_view url);
std::vector<StreamFinding> checkStream(const StreamInputs& in);

std::string writeSchedule(const Schedule& s);
struct ScheduleReadResult
{
    std::optional<Schedule> schedule;
    std::vector<std::string> notes;
};
ScheduleReadResult readSchedule(std::string_view text);

/// "2026-11-20 21:00" <-> fields; the viewer converts with the computer's clock.
struct DateTime
{
    int year = 0, month = 0, day = 0, hour = 0, minute = 0;
};
std::optional<DateTime> parseDateTime(std::string_view text);
std::string formatDateTime(const DateTime& t);

} // namespace sw67::event
