/**
 * @file rehearsal.h
 * @brief Event Rehearsal Mode: an event's run sheet (timed cues: chat
 *        announcements, IMs, invitations, gifts, prizes, music and
 *        environment changes, camera moves, notes) checked and played
 *        through as a simulation that shows what would happen, and when.
 *
 *        Rehearsal cannot send, broadcast or pay. This module has no way to
 *        act on anything: rehearse() and Rehearsal only describe effects.
 *        Running an event for real is a separate, later module (Milestone 4)
 *        that will ask for the user's authorization before every message,
 *        broadcast and payment; nothing here calls it or can reach it.
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

#include "sw67/rlv/gate.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sw67::event
{

inline constexpr std::string_view kFormat = "sw67.event";
inline constexpr int kFormatVersion = 1;
inline constexpr std::size_t kMaxCues = 500;
/// Second Life's limits on one chat line and one IM, in bytes.
inline constexpr std::size_t kMaxChatBytes = 1023;
inline constexpr std::size_t kMaxIMBytes = 1023;

enum class CueKind
{
    Note,              ///< A reminder for the host; nothing happens.
    LocalChat,         ///< Say something in nearby chat (channel 0, or a channel for objects).
    SendIM,            ///< An IM to one person.
    Invite,            ///< An invitation (an IM or group invite) to one person or group.
    GiveItem,          ///< Give an inventory item to someone.
    PayPrize,          ///< Pay L$ to someone.
    StartStream,       ///< Start music or a broadcast.
    StopStream,
    ChangeEnvironment, ///< Switch the local sky and water.
    Camera,            ///< Move your own camera.
    Count
};

std::string_view cueKey(CueKind k);
std::string_view cueName(CueKind k);
std::optional<CueKind> parseCue(std::string_view key);

/// What a cue would be outside rehearsal: the RLV action it maps to, and
/// whether others would see, hear or receive it.
struct CueTraits
{
    std::optional<rlv::ActionKind> action;
    bool sendsMessage = false;
    bool spendsMoney = false;
    bool broadcasts = false; ///< Audio or video others hear or see.
    bool publishes = false;  ///< Anything that leaves this viewer.
};
CueTraits traitsOf(CueKind k);

struct Cue
{
    int atSeconds = 0;       ///< From the event's start.
    CueKind kind = CueKind::Note;
    std::string label;       ///< The host's own name for the cue.
    std::string text;        ///< Chat or IM text, stream URL, environment or camera name.
    std::string targetId;    ///< Avatar, group or item UUID, for IMs, invites, gifts and prizes.
    std::string targetName;  ///< Who or what, for display.
    std::string itemId;      ///< For GiveItem: the inventory item.
    int amount = 0;          ///< L$, for PayPrize.
    int channel = 0;         ///< For LocalChat.
};

struct EventPlan
{
    std::string name;
    int plannedMinutes = 60;
    int prizeBudget = 0; ///< L$ the host means to pay out in all.
    std::string notes;
    std::vector<Cue> cues;
};

struct Issue
{
    bool blocking = false;
    int cueIndex = -1; ///< -1 for the plan as a whole.
    std::string text;
};

/// Checks the plan: missing recipients or text, text over Second Life's
/// limits, prizes that are zero, negative or past the budget, cues after the
/// planned end, several outward cues in the same second.
std::vector<Issue> validate(const EventPlan& plan);

/// One thing that would happen. Always a description: rehearsal does
/// nothing else.
struct SimulatedEffect
{
    int atSeconds = 0;
    std::size_t cueIndex = 0;
    std::string description; ///< Starts "Rehearsal: would ..." for every outward cue.
    bool outward = false;    ///< Would leave this viewer outside rehearsal.
    bool blockedByRlv = false;
    std::string rlvReason;
};

struct RehearsalReport
{
    std::vector<SimulatedEffect> effects; ///< In time order.
    std::vector<Issue> issues;
    int messages = 0;      ///< Chat lines and IMs that would be sent.
    int invitations = 0;
    int gifts = 0;
    int prizes = 0;
    int prizeTotal = 0;    ///< L$ that would be paid.
    int broadcasts = 0;    ///< Streams that would start.
    int lastCueSeconds = 0;
    std::string summary() const;
};

/// Plays the whole plan through at once. With a gate, each outward cue is
/// also checked against the current RLV restrictions so a blocked cue shows
/// before the event.
RehearsalReport rehearse(const EventPlan& plan, const rlv::Gate* gate = nullptr);

/// Plays the plan through in time, for the viewer's live rehearsal clock.
class Rehearsal
{
public:
    explicit Rehearsal(EventPlan plan);
    /// Effects of cues due by `seconds` not yet returned. Time only moves on.
    std::vector<SimulatedEffect> advanceTo(int seconds);
    bool finished() const { return mNext >= mOrder.size(); }
    int now() const { return mNow; }
    /// The next cue's time, if any.
    std::optional<int> nextAt() const;

private:
    EventPlan mPlan;
    std::vector<std::size_t> mOrder; ///< Cue indices in time order.
    std::size_t mNext = 0;
    int mNow = 0;
};

std::string writeEventPlan(const EventPlan& plan);

struct ReadResult
{
    std::optional<EventPlan> plan;
    std::vector<std::string> notes;
};
ReadResult readEventPlan(std::string_view text);

/// "1:05:09" or "5:09" for a time from the start.
std::string formatTime(int seconds);
/// Parses "h:mm:ss", "m:ss" or seconds.
std::optional<int> parseTime(std::string_view text);

} // namespace sw67::event
