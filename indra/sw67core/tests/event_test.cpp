/**
 * @file event_test.cpp
 * @brief Event Rehearsal Mode: a rehearsal only describes, never acts;
 *        invitations, broadcasts and prizes are counted, not made; the plan
 *        is checked; RLV blocks show before the event.
 *
 * Copyright (c) 2026, 6|7 World Studio contributors. LGPL-2.1.
 */

#include "sw67/event/rehearsal.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <set>

using namespace sw67::event;
using sw67::rlv::ActionKind;

namespace
{
const std::string kGuest = "11111111-2222-4333-8444-555555555555";
const std::string kItem = "aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee";

EventPlan party()
{
    EventPlan p;
    p.name = "Friday dance";
    p.plannedMinutes = 90;
    p.prizeBudget = 1000;
    p.cues = {
        {0, CueKind::StartStream, "Music on", "https://radio.example/stream", "", "", "", 0, 0},
        {5, CueKind::LocalChat, "Welcome", "Welcome to Friday dance!", "", "", "", 0, 0},
        {60, CueKind::Invite, "Invite Sam", "Come dance with us", kGuest, "Sam", "", 0, 0},
        {120, CueKind::SendIM, "Thank Sam", "Thanks for coming", kGuest, "Sam", "", 0, 0},
        {1800, CueKind::GiveItem, "Gift", "", kGuest, "Sam", kItem, 0, 0},
        {3600, CueKind::PayPrize, "Best dancer", "", kGuest, "Sam", "", 500, 0},
        {30, CueKind::ChangeEnvironment, "Sunset", "Sunset", "", "", "", 0, 0},
        {40, CueKind::Note, "", "Check the lights", "", "", "", 0, 0},
        {5400, CueKind::StopStream, "Music off", "", "", "", "", 0, 0},
    };
    return p;
}

class FakeRlv : public sw67::rlv::IRlvState
{
public:
    std::set<ActionKind> blocked;
    bool isEnabled() const override { return true; }
    bool isReady() const override { return true; }
    bool allows(const sw67::rlv::ActionRequest& r) const override { return !blocked.count(r.kind); }
    std::vector<sw67::rlv::Restriction> restrictionsFor(const sw67::rlv::ActionRequest&) const override
    {
        return {{"sendim", "", "", "Collar"}};
    }
    std::vector<sw67::rlv::Restriction> activeRestrictions() const override { return {}; }
};
} // namespace

TEST_CASE("A rehearsal describes every outward cue as something it would do", "[event]")
{
    const RehearsalReport r = rehearse(party());
    REQUIRE(r.effects.size() == party().cues.size());
    for (const SimulatedEffect& e : r.effects)
    {
        if (e.outward)
        {
            CHECK(e.description.rfind("Rehearsal: would ", 0) == 0);
        }
    }
    CHECK(r.summary().find("nothing was sent, broadcast or paid") != std::string::npos);
    CHECK(r.messages == 2);
    CHECK(r.invitations == 1);
    CHECK(r.gifts == 1);
    CHECK(r.prizes == 1);
    CHECK(r.prizeTotal == 500);
    CHECK(r.broadcasts == 1);
    CHECK(r.lastCueSeconds == 5400);
    CHECK(std::is_sorted(r.effects.begin(), r.effects.end(),
                         [](const auto& a, const auto& b) { return a.atSeconds < b.atSeconds; }));
}

TEST_CASE("Invitations, broadcasts and prizes are outward; local changes are not", "[event]")
{
    CHECK(traitsOf(CueKind::Invite).sendsMessage);
    CHECK(traitsOf(CueKind::StartStream).broadcasts);
    CHECK(traitsOf(CueKind::PayPrize).spendsMoney);
    CHECK(traitsOf(CueKind::PayPrize).action == ActionKind::PayAvatar);
    CHECK_FALSE(traitsOf(CueKind::ChangeEnvironment).publishes);
    CHECK_FALSE(traitsOf(CueKind::Camera).publishes);
    CHECK_FALSE(traitsOf(CueKind::Note).publishes);
    for (int k = 0; k < static_cast<int>(CueKind::Count); ++k)
    {
        const CueKind kind = static_cast<CueKind>(k);
        const CueTraits t = traitsOf(kind);
        if (t.sendsMessage || t.spendsMoney || t.broadcasts) CHECK(t.publishes);
        CHECK(parseCue(cueKey(kind)) == kind);
    }
}

TEST_CASE("The plan is checked before the event", "[event]")
{
    const auto clean = validate(party());
    CHECK(std::none_of(clean.begin(), clean.end(), [](const Issue& i) { return i.blocking; }));

    EventPlan p = party();
    p.cues[2].targetId.clear();                  // invite to no one
    p.cues[5].amount = 0;                        // zero prize
    p.cues[1].text = std::string(1100, 'a');     // too long for chat
    p.cues.push_back({7200, CueKind::Note, "", "late", "", "", "", 0, 0}); // after the end
    p.cues.push_back({100, CueKind::PayPrize, "", "", kGuest, "Sam", "", 2000, 0}); // past the budget
    const auto issues = validate(p);
    auto has = [&](const std::string& words) {
        return std::any_of(issues.begin(), issues.end(), [&](const Issue& i) { return i.text.find(words) != std::string::npos; });
    };
    CHECK(has("choose who it goes to"));
    CHECK(has("at least L$1"));
    CHECK(has("longer than chat allows"));
    CHECK(has("after the planned end"));
    CHECK(has("over the budget"));
}

TEST_CASE("RLV blocks show in the rehearsal before the event", "[event]")
{
    FakeRlv rlv;
    rlv.blocked = {ActionKind::SendIM};
    const sw67::rlv::Gate gate(rlv);
    const RehearsalReport r = rehearse(party(), &gate);
    int blocked = 0;
    for (const SimulatedEffect& e : r.effects)
    {
        if (e.blockedByRlv)
        {
            ++blocked;
            CHECK_FALSE(e.rlvReason.empty());
        }
    }
    CHECK(blocked == 2); // the invitation and the IM
}

TEST_CASE("The live rehearsal clock releases cues in time, once each", "[event]")
{
    Rehearsal run(party());
    CHECK(run.nextAt() == 0);
    CHECK(run.advanceTo(0).size() == 1);
    CHECK(run.advanceTo(30).size() == 2);  // chat at 5, environment at 30
    CHECK(run.advanceTo(10).empty());      // time does not go back
    CHECK(run.advanceTo(60).size() == 2);  // note at 40, invite at 60
    CHECK(run.advanceTo(100000).size() == 4);
    CHECK(run.finished());
    CHECK(run.advanceTo(200000).empty());
}

TEST_CASE("An event plan round-trips and unknown cues are dropped", "[event]")
{
    const EventPlan p = party();
    const ReadResult back = readEventPlan(writeEventPlan(p));
    REQUIRE(back.plan);
    REQUIRE(back.plan->cues.size() == p.cues.size());
    CHECK(back.plan->cues[5].amount == 500);
    CHECK(back.plan->cues[2].targetId == kGuest);
    CHECK(back.plan->prizeBudget == 1000);
    const ReadResult bad = readEventPlan(R"({"format":"sw67.event","version":1,"cues":[{"kind":"fireworks"}]})");
    REQUIRE(bad.plan);
    CHECK(bad.plan->cues.empty());
    CHECK(bad.notes.size() == 1);
    CHECK_FALSE(readEventPlan(R"({"format":"sw67.event","version":2})").plan);
}

TEST_CASE("Times read and print as the host writes them", "[event]")
{
    CHECK(formatTime(0) == "0:00");
    CHECK(formatTime(309) == "5:09");
    CHECK(formatTime(3909) == "1:05:09");
    CHECK(parseTime("5:09") == 309);
    CHECK(parseTime("1:05:09") == 3909);
    CHECK(parseTime("90") == 90);
    CHECK_FALSE(parseTime("5:61"));
    CHECK_FALSE(parseTime("a:00"));
    CHECK_FALSE(parseTime(":30"));
    CHECK_FALSE(parseTime("1:2:3:4"));
}
