/**
 * @file event_rehearsal.cpp
 * @brief Event Rehearsal Mode. Nothing in this file acts on anything: every
 *        function returns descriptions.
 *
 * Copyright (c) 2026, 6|7 World Studio contributors.
 *
 * The source code in this file is provided to you under the terms of the
 * GNU Lesser General Public License, version 2.1, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE. Terms of the LGPL can be found in doc/LGPL-license.txt
 * in this distribution, or online at http://www.gnu.org/licenses/lgpl-2.1.txt
 */

#include "sw67/event/rehearsal.h"

#include "sw67/creator/project.h"
#include "sw67/io/json.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <map>
#include <numeric>

namespace sw67::event
{
namespace
{
using io::Json;

struct CueInfo
{
    CueKind kind;
    std::string_view key;
    std::string_view name;
};

constexpr CueInfo kCues[] = {
    {CueKind::Note, "note", "Note for the host"},
    {CueKind::LocalChat, "chat", "Nearby chat"},
    {CueKind::SendIM, "im", "Instant message"},
    {CueKind::Invite, "invite", "Invitation"},
    {CueKind::GiveItem, "give", "Give an item"},
    {CueKind::PayPrize, "prize", "Pay a prize"},
    {CueKind::StartStream, "stream_start", "Start music or broadcast"},
    {CueKind::StopStream, "stream_stop", "Stop music or broadcast"},
    {CueKind::ChangeEnvironment, "environment", "Change your sky and water"},
    {CueKind::Camera, "camera", "Move your camera"},
};

std::string quoted(const std::string& s)
{
    return "\"" + (s.size() > 80 ? s.substr(0, 77) + "..." : s) + "\"";
}

std::string who(const Cue& c)
{
    if (!c.targetName.empty()) return c.targetName;
    if (!c.targetId.empty()) return c.targetId;
    return "(no one chosen)";
}

std::string describe(const Cue& c)
{
    const std::string r = "Rehearsal: would ";
    switch (c.kind)
    {
    case CueKind::Note: return "Note: " + c.text;
    case CueKind::LocalChat:
        return r + (c.channel == 0 ? std::string("say in nearby chat: ")
                                   : "say on channel " + std::to_string(c.channel) + ": ") +
               quoted(c.text);
    case CueKind::SendIM: return r + "send an IM to " + who(c) + ": " + quoted(c.text);
    case CueKind::Invite: return r + "send an invitation to " + who(c) + (c.text.empty() ? "" : ": " + quoted(c.text));
    case CueKind::GiveItem:
        return r + "give " + (c.label.empty() ? std::string("an item") : quoted(c.label)) + " to " + who(c);
    case CueKind::PayPrize: return r + "pay L$" + std::to_string(c.amount) + " to " + who(c);
    case CueKind::StartStream: return r + "start the stream " + (c.text.empty() ? std::string("(none chosen)") : c.text);
    case CueKind::StopStream: return r + "stop the stream";
    case CueKind::ChangeEnvironment:
        return r + "switch your sky and water to " + (c.text.empty() ? std::string("(none chosen)") : quoted(c.text)) +
               " (only you see this)";
    case CueKind::Camera:
        return r + "move your camera to " + (c.text.empty() ? std::string("(none chosen)") : quoted(c.text));
    case CueKind::Count: break;
    }
    return r + "do nothing";
}

std::string lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return s;
}

std::vector<std::size_t> timeOrder(const EventPlan& plan)
{
    std::vector<std::size_t> order(plan.cues.size());
    std::iota(order.begin(), order.end(), std::size_t{0});
    std::stable_sort(order.begin(), order.end(),
                     [&](std::size_t a, std::size_t b) { return plan.cues[a].atSeconds < plan.cues[b].atSeconds; });
    return order;
}

SimulatedEffect effectOf(const EventPlan& plan, std::size_t i)
{
    const Cue& c = plan.cues[i];
    SimulatedEffect e;
    e.atSeconds = c.atSeconds;
    e.cueIndex = i;
    e.description = describe(c);
    e.outward = traitsOf(c.kind).publishes;
    return e;
}
} // namespace

std::string_view cueKey(CueKind k)
{
    for (const CueInfo& i : kCues)
        if (i.kind == k) return i.key;
    return "";
}

std::string_view cueName(CueKind k)
{
    for (const CueInfo& i : kCues)
        if (i.kind == k) return i.name;
    return "";
}

std::optional<CueKind> parseCue(std::string_view key)
{
    for (const CueInfo& i : kCues)
        if (i.key == key) return i.kind;
    return std::nullopt;
}

CueTraits traitsOf(CueKind k)
{
    CueTraits t;
    switch (k)
    {
    case CueKind::LocalChat:
        t.action = rlv::ActionKind::SendChat;
        t.sendsMessage = t.publishes = true;
        break;
    case CueKind::SendIM:
    case CueKind::Invite:
        t.action = rlv::ActionKind::SendIM;
        t.sendsMessage = t.publishes = true;
        break;
    case CueKind::GiveItem:
        t.action = rlv::ActionKind::GiveInventory;
        t.publishes = true;
        break;
    case CueKind::PayPrize:
        t.action = rlv::ActionKind::PayAvatar;
        t.spendsMoney = t.publishes = true;
        break;
    case CueKind::StartStream:
        t.broadcasts = t.publishes = true;
        break;
    case CueKind::StopStream: t.publishes = true; break;
    case CueKind::ChangeEnvironment: t.action = rlv::ActionKind::ChangeEnvironment; break;
    case CueKind::Camera: t.action = rlv::ActionKind::ChangeCamera; break;
    case CueKind::Note:
    case CueKind::Count: break;
    }
    return t;
}

std::vector<Issue> validate(const EventPlan& plan)
{
    std::vector<Issue> issues;
    if (plan.name.empty()) issues.push_back({false, -1, "The event has no name."});
    if (plan.plannedMinutes <= 0 || plan.plannedMinutes > 24 * 60)
        issues.push_back({true, -1, "The planned length must be between 1 minute and 24 hours."});
    if (plan.prizeBudget < 0) issues.push_back({true, -1, "The prize budget cannot be negative."});
    if (plan.cues.size() > kMaxCues)
        issues.push_back({true, -1, "An event can have at most " + std::to_string(kMaxCues) + " cues."});

    const int end = plan.plannedMinutes * 60;
    long long prizes = 0;
    std::map<int, int> outwardAt;
    for (std::size_t n = 0; n < plan.cues.size(); ++n)
    {
        const Cue& c = plan.cues[n];
        const int i = static_cast<int>(n);
        const std::string at = "At " + formatTime(c.atSeconds) + ", " + std::string(cueName(c.kind)) + ": ";
        if (c.atSeconds < 0) issues.push_back({true, i, at + "the time is before the start."});
        else if (c.atSeconds > end) issues.push_back({false, i, at + "this is after the planned end."});
        if (traitsOf(c.kind).publishes) ++outwardAt[c.atSeconds];
        const bool needsTarget = c.kind == CueKind::SendIM || c.kind == CueKind::Invite ||
                                 c.kind == CueKind::GiveItem || c.kind == CueKind::PayPrize;
        if (needsTarget && !creator::isUuid(c.targetId))
            issues.push_back({true, i, at + "choose who it goes to."});
        switch (c.kind)
        {
        case CueKind::LocalChat:
            if (c.text.empty()) issues.push_back({true, i, at + "there is nothing to say."});
            if (c.text.size() > kMaxChatBytes) issues.push_back({true, i, at + "the text is longer than chat allows."});
            if (c.channel < 0) issues.push_back({true, i, at + "the channel cannot be negative."});
            break;
        case CueKind::SendIM:
            if (c.text.empty()) issues.push_back({true, i, at + "the message is empty."});
            [[fallthrough]];
        case CueKind::Invite:
            if (c.text.size() > kMaxIMBytes) issues.push_back({true, i, at + "the text is longer than an IM allows."});
            break;
        case CueKind::GiveItem:
            if (!creator::isUuid(c.itemId)) issues.push_back({true, i, at + "choose the item to give."});
            break;
        case CueKind::PayPrize:
            if (c.amount <= 0) issues.push_back({true, i, at + "the prize must be at least L$1."});
            else prizes += c.amount;
            break;
        case CueKind::StartStream:
        {
            const std::string l = lower(c.text);
            if (l.rfind("http://", 0) != 0 && l.rfind("https://", 0) != 0)
                issues.push_back({false, i, at + "the stream is not an http or https address."});
            break;
        }
        case CueKind::ChangeEnvironment:
        case CueKind::Camera:
            if (c.text.empty()) issues.push_back({false, i, at + "nothing is chosen."});
            break;
        case CueKind::Note:
        case CueKind::StopStream:
        case CueKind::Count: break;
        }
    }
    if (prizes > plan.prizeBudget)
    {
        issues.push_back({true, -1, "Prizes add up to L$" + std::to_string(prizes) + ", over the budget of L$" +
                                        std::to_string(plan.prizeBudget) + "."});
    }
    for (const auto& [at, count] : outwardAt)
    {
        if (count > 1)
            issues.push_back({false, -1, std::to_string(count) + " cues reach other people at " + formatTime(at) +
                                             "; spacing them out is easier to follow."});
    }
    return issues;
}

std::string RehearsalReport::summary() const
{
    std::string s = "Rehearsal only: nothing was sent, broadcast or paid. ";
    s += "In the event this would send " + std::to_string(messages) + " message(s), " + std::to_string(invitations) +
         " invitation(s), give " + std::to_string(gifts) + " item(s), pay " + std::to_string(prizes) +
         " prize(s) totalling L$" + std::to_string(prizeTotal) + " and start " + std::to_string(broadcasts) +
         " stream(s). The last cue is at " + formatTime(lastCueSeconds) + ".";
    return s;
}

RehearsalReport rehearse(const EventPlan& plan, const rlv::Gate* gate)
{
    RehearsalReport r;
    r.issues = validate(plan);
    for (const std::size_t i : timeOrder(plan))
    {
        const Cue& c = plan.cues[i];
        SimulatedEffect e = effectOf(plan, i);
        const CueTraits t = traitsOf(c.kind);
        if (gate && t.action)
        {
            rlv::ActionRequest req;
            req.kind = *t.action;
            req.targetId = c.targetId;
            req.channel = c.channel;
            const rlv::Decision d = gate->evaluate(req);
            if (!d.allowed())
            {
                e.blockedByRlv = true;
                e.rlvReason = d.explanation;
            }
        }
        switch (c.kind)
        {
        case CueKind::LocalChat:
        case CueKind::SendIM: ++r.messages; break;
        case CueKind::Invite: ++r.invitations; break;
        case CueKind::GiveItem: ++r.gifts; break;
        case CueKind::PayPrize:
            ++r.prizes;
            r.prizeTotal += std::max(0, c.amount);
            break;
        case CueKind::StartStream: ++r.broadcasts; break;
        default: break;
        }
        r.lastCueSeconds = std::max(r.lastCueSeconds, c.atSeconds);
        r.effects.push_back(std::move(e));
    }
    return r;
}

Rehearsal::Rehearsal(EventPlan plan)
:   mPlan(std::move(plan)),
    mOrder(timeOrder(mPlan))
{
}

std::vector<SimulatedEffect> Rehearsal::advanceTo(int seconds)
{
    std::vector<SimulatedEffect> out;
    mNow = std::max(mNow, seconds);
    while (mNext < mOrder.size() && mPlan.cues[mOrder[mNext]].atSeconds <= mNow)
    {
        out.push_back(effectOf(mPlan, mOrder[mNext]));
        ++mNext;
    }
    return out;
}

std::optional<int> Rehearsal::nextAt() const
{
    if (mNext >= mOrder.size()) return std::nullopt;
    return mPlan.cues[mOrder[mNext]].atSeconds;
}

std::string writeEventPlan(const EventPlan& plan)
{
    Json root = Json::object();
    root.set("format", Json(kFormat));
    root.set("version", Json(kFormatVersion));
    root.set("name", Json(plan.name));
    root.set("plannedMinutes", Json(plan.plannedMinutes));
    root.set("prizeBudget", Json(plan.prizeBudget));
    root.set("notes", Json(plan.notes));
    Json cues = Json::array();
    for (const Cue& c : plan.cues)
    {
        Json e = Json::object();
        e.set("at", Json(c.atSeconds));
        e.set("kind", Json(cueKey(c.kind)));
        e.set("label", Json(c.label));
        e.set("text", Json(c.text));
        if (!c.targetId.empty()) e.set("targetId", Json(lower(c.targetId)));
        if (!c.targetName.empty()) e.set("targetName", Json(c.targetName));
        if (!c.itemId.empty()) e.set("itemId", Json(lower(c.itemId)));
        if (c.amount != 0) e.set("amount", Json(c.amount));
        if (c.channel != 0) e.set("channel", Json(c.channel));
        cues.push(std::move(e));
    }
    root.set("cues", std::move(cues));
    return io::writeJson(root);
}

ReadResult readEventPlan(std::string_view text)
{
    ReadResult r;
    const auto root = io::parseJson(text);
    if (!root || !root->isObject() || (*root)["format"].asString("") != kFormat)
    {
        r.notes.push_back("This is not a 6|7 event plan.");
        return r;
    }
    const int version = (*root)["version"].asInt(0);
    if (version < 1 || version > kFormatVersion)
    {
        r.notes.push_back("This event plan was saved by a newer 6|7 World Studio.");
        return r;
    }
    EventPlan p;
    p.name = (*root)["name"].asString("").substr(0, 120);
    p.plannedMinutes = (*root)["plannedMinutes"].asInt(60);
    p.prizeBudget = (*root)["prizeBudget"].asInt(0);
    p.notes = (*root)["notes"].asString("").substr(0, 8000);
    for (const Json& e : (*root)["cues"].items())
    {
        if (p.cues.size() >= kMaxCues)
        {
            r.notes.push_back("Only the first " + std::to_string(kMaxCues) + " cues were read.");
            break;
        }
        const auto kind = parseCue(e["kind"].asString(""));
        if (!e.isObject() || !kind)
        {
            r.notes.push_back("Left out a cue of an unknown kind.");
            continue;
        }
        Cue c;
        c.kind = *kind;
        c.atSeconds = e["at"].asInt(0);
        c.label = e["label"].asString("").substr(0, 120);
        c.text = e["text"].asString("").substr(0, 4096);
        c.targetId = lower(e["targetId"].asString(""));
        c.targetName = e["targetName"].asString("").substr(0, 120);
        c.itemId = lower(e["itemId"].asString(""));
        c.amount = e["amount"].asInt(0);
        c.channel = e["channel"].asInt(0);
        p.cues.push_back(std::move(c));
    }
    r.plan = std::move(p);
    return r;
}

std::string formatTime(int seconds)
{
    const bool negative = seconds < 0;
    const long long s = negative ? -static_cast<long long>(seconds) : seconds;
    char buf[32];
    if (s >= 3600)
        std::snprintf(buf, sizeof(buf), "%s%lld:%02lld:%02lld", negative ? "-" : "", s / 3600, (s / 60) % 60, s % 60);
    else
        std::snprintf(buf, sizeof(buf), "%s%lld:%02lld", negative ? "-" : "", s / 60, s % 60);
    return buf;
}

std::optional<int> parseTime(std::string_view text)
{
    if (text.empty() || text.size() > 12) return std::nullopt;
    std::vector<long long> parts{0};
    bool digit = false;
    for (const char ch : text)
    {
        if (ch == ':')
        {
            if (!digit || parts.size() == 3) return std::nullopt;
            parts.push_back(0);
            digit = false;
        }
        else if (ch >= '0' && ch <= '9')
        {
            parts.back() = parts.back() * 10 + (ch - '0');
            digit = true;
        }
        else
        {
            return std::nullopt;
        }
    }
    if (!digit) return std::nullopt;
    for (std::size_t i = 1; i < parts.size(); ++i)
        if (parts[i] > 59) return std::nullopt;
    long long total = 0;
    for (const long long p : parts) total = total * 60 + p;
    if (total > 7 * 24 * 3600) return std::nullopt;
    return static_cast<int>(total);
}

} // namespace sw67::event
