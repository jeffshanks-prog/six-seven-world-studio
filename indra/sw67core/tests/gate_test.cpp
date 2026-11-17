/**
 * @file gate_test.cpp
 * @brief Tests for the RLV gate: automation respects restrictions, fails
 *        closed before RLV is ready, and stopping never claims a release.
 *
 * Copyright (c) 2026, 6|7 World Studio contributors. LGPL-2.1.
 */

#include "sw67/rlv/gate.h"

#include <catch2/catch_test_macros.hpp>

#include <set>

using namespace sw67::rlv;

namespace
{
class FakeRlv : public IRlvState
{
public:
    bool enabled = true;
    bool ready = true;
    std::set<ActionKind> blocked;
    std::vector<Restriction> active;

    bool isEnabled() const override { return enabled; }
    bool isReady() const override { return ready; }
    bool allows(const ActionRequest& r) const override { return !blocked.count(r.kind); }
    std::vector<Restriction> restrictionsFor(const ActionRequest& r) const override
    {
        std::vector<Restriction> out;
        for (const auto& x : active)
        {
            for (auto b : behavioursFor(r.kind))
            {
                if (x.behaviour == b) out.push_back(x);
            }
        }
        return out;
    }
    std::vector<Restriction> activeRestrictions() const override { return active; }
};
} // namespace

TEST_CASE("With RLV off, the gate allows everything", "[rlv]")
{
    FakeRlv rlv;
    rlv.enabled = false;
    rlv.ready = false;
    rlv.blocked = {ActionKind::Teleport};
    Gate gate(rlv);
    CHECK(gate.evaluate({ActionKind::Teleport}).allowed());
}

TEST_CASE("With RLV on but not ready, the gate fails closed", "[rlv]")
{
    FakeRlv rlv;
    rlv.ready = false;
    Gate gate(rlv);
    const Decision d = gate.evaluate({ActionKind::WearItem, "item"});
    CHECK(d.verdict == Verdict::NotReady);
    CHECK_FALSE(d.allowed());
    CHECK_FALSE(d.explanation.empty());
}

TEST_CASE("A restricted action is blocked and the restriction is named", "[rlv]")
{
    FakeRlv rlv;
    rlv.blocked = {ActionKind::Teleport};
    rlv.active = {{"tploc", "", "obj-1", "Steel Collar"}};
    Gate gate(rlv);
    const Decision d = gate.evaluate({ActionKind::Teleport, "", "", 0, "Ahern/128/128/30"});
    CHECK(d.verdict == Verdict::Blocked);
    REQUIRE(d.because.size() == 1);
    CHECK(d.explanation.find("@tploc") != std::string::npos);
    CHECK(d.explanation.find("Steel Collar") != std::string::npos);
    CHECK(gate.evaluate({ActionKind::Sit}).allowed());
}

TEST_CASE("A batch preview evaluates each item independently and in order", "[rlv]")
{
    FakeRlv rlv;
    rlv.blocked = {ActionKind::RemoveItem};
    Gate gate(rlv);
    const auto ds = gate.evaluateBatch({{ActionKind::WearItem, "a"}, {ActionKind::RemoveItem, "b"}, {ActionKind::WearItem, "c"}});
    REQUIRE(ds.size() == 3);
    CHECK(ds[0].allowed());
    CHECK(ds[1].verdict == Verdict::Blocked);
    CHECK(ds[2].allowed());
}

TEST_CASE("Unknown actions never run", "[rlv]")
{
    FakeRlv rlv;
    rlv.enabled = false;
    Gate gate(rlv);
    CHECK_FALSE(gate.evaluate({ActionKind::Count}).allowed());
}

TEST_CASE("Stopping a workflow never claims to release RLV restrictions", "[rlv]")
{
    const std::string none = describeStop("Photo session", 0);
    CHECK(none.find("Stopped") != std::string::npos);
    CHECK(none.find("released") == std::string::npos);

    const std::string some = describeStop("Photo session", 2);
    CHECK(some.find("2 RLV restrictions are still active") != std::string::npos);
    CHECK(some.find("does not release") != std::string::npos);
    CHECK(describeStop("x", 1).find("1 RLV restriction is still active") != std::string::npos);
}

TEST_CASE("Money, messages, publishing and destructive actions need explicit authorization", "[rlv]")
{
    for (ActionKind k : {ActionKind::PayAvatar, ActionKind::PayObject, ActionKind::SendIM, ActionKind::StartIM,
                         ActionKind::SendChat, ActionKind::Rez, ActionKind::GiveInventory, ActionKind::DeleteInventory})
    {
        CHECK(requiresExplicitAuthorization(k));
    }
    for (ActionKind k : {ActionKind::Sit, ActionKind::ChangeCamera, ActionKind::WearItem, ActionKind::MoveInventory,
                         ActionKind::ChangeEnvironment})
    {
        CHECK_FALSE(requiresExplicitAuthorization(k));
    }
}

TEST_CASE("Every action names itself and RLV-governed ones name behaviours", "[rlv]")
{
    for (int i = 0; i < static_cast<int>(ActionKind::Count); ++i)
    {
        const auto k = static_cast<ActionKind>(i);
        CHECK(actionName(k) != "Unknown action");
        if (k != ActionKind::TakeSnapshot)
        {
            CHECK_FALSE(behavioursFor(k).empty());
        }
    }
}
