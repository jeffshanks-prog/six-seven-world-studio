/**
 * @file entitlements_test.cpp
 * @brief Plan entitlements: the shipped catalogue matches the proposed plan
 *        assignment, local data and accessibility are never gated, AI is
 *        never unlimited, and captioning carries no recurring charge.
 *
 * Copyright (c) 2026, 6|7 World Studio contributors. LGPL-2.1.
 */

#include "sw67/plan/entitlements.h"

#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <sstream>
#include <string>

using namespace sw67::plan;

namespace
{
Catalogue shipped()
{
    std::ifstream in(SW67_PLANS_FILE, std::ios::binary);
    REQUIRE(in);
    std::stringstream ss;
    ss << in.rdbuf();
    const CatalogueResult r = readCatalogue(ss.str());
    for (const std::string& p : r.problems) UNSCOPED_INFO(p);
    REQUIRE(r.catalogue);
    return *r.catalogue;
}
} // namespace

TEST_CASE("The shipped catalogue has the proposed prices and is a sandbox", "[plan]")
{
    const Catalogue c = shipped();
    CHECK(c.sandbox);
    CHECK_FALSE(c.trialDays); // No trial length decided yet.
    REQUIRE(c.find("basic"));
    REQUIRE(c.find("dj_hosting"));
    REQUIRE(c.find("ai_creator"));
    CHECK(formatPrice(c.find("basic")->priceCentsPerMonth) == "$19.99");
    CHECK(formatPrice(c.find("dj_hosting")->priceCentsPerMonth) == "$29.99");
    CHECK(formatPrice(c.find("ai_creator")->priceCentsPerMonth) == "$14.99");
    CHECK(c.find("ai_creator")->isAddOn);
    REQUIRE_FALSE(c.creditPacks.empty());
    int cheapest = c.creditPacks.front().priceCents;
    for (const CreditPack& p : c.creditPacks) cheapest = std::min(cheapest, p.priceCents);
    CHECK(cheapest == 500);
}

TEST_CASE("Plan assignment follows the spec", "[plan]")
{
    const Catalogue c = shipped();
    const Subscription basic{"basic", {}, true};
    const Subscription dj{"dj_hosting", {}, true};
    const Subscription creator{"basic", {"ai_creator"}, true};

    for (auto f : {feature::WorkspaceProfiles, feature::PerformanceAssistant, feature::CreatorProjects,
                   feature::ProductPhotoManual, feature::OutfitPlanner, feature::RecordingPrivacy, feature::SettingsBackup})
    {
        CAPTURE(f);
        CHECK(hasFeature(c, basic, f));
        CHECK(hasFeature(c, dj, f)); // DJ & Hosting includes Basic
    }
    for (auto f : {feature::EventRehearsal, feature::StageLighting, feature::PerformanceSequencer})
    {
        CHECK_FALSE(hasFeature(c, basic, f));
        CHECK(hasFeature(c, dj, f));
    }
    for (auto f : {feature::AIScenePlanning, feature::AIProductPhoto, feature::AIListingDrafts, feature::AIMesh})
    {
        CHECK_FALSE(hasFeature(c, basic, f));
        CHECK_FALSE(hasFeature(c, dj, f)); // Event modules do not bring AI with them.
        CHECK(hasFeature(c, creator, f));
    }
    // Captioning has no paid plan until costs are measured.
    CHECK_FALSE(hasFeature(c, {"dj_hosting", {"ai_creator"}, true}, feature::Captioning));
}

TEST_CASE("Local data and accessibility are never gated", "[plan]")
{
    const Catalogue c = shipped();
    for (const Subscription& s : {Subscription{}, Subscription{"basic", {}, false}, Subscription{"nonexistent", {}, true}})
    {
        CHECK(hasFeature(c, s, feature::LocalData));
        CHECK(hasFeature(c, s, feature::Accessibility));
        CHECK_FALSE(hasFeature(c, s, feature::CreatorProjects)); // the tool's extras, not the files
        CHECK(monthlyAICredits(c, s) == 0);
    }
}

TEST_CASE("AI allowances are metered and add up, never unlimited", "[plan]")
{
    const Catalogue c = shipped();
    const int basic = monthlyAICredits(c, {"basic", {}, true});
    const int dj = monthlyAICredits(c, {"dj_hosting", {}, true});
    const int both = monthlyAICredits(c, {"basic", {"ai_creator"}, true});
    CHECK(basic > 0);
    CHECK(dj == c.find("dj_hosting")->aiCreditsPerMonth); // not Basic's counted twice
    CHECK(both == basic + c.find("ai_creator")->aiCreditsPerMonth);
    // An add-on cannot be a base plan.
    CHECK(monthlyAICredits(c, {"ai_creator", {}, true}) == 0);
    CHECK_FALSE(hasFeature(c, {"ai_creator", {}, true}, feature::AIMesh));
}

TEST_CASE("Bad catalogues are refused", "[plan]")
{
    CHECK_FALSE(readCatalogue("{}").catalogue);
    CHECK_FALSE(readCatalogue(R"({"format":"sw67.plans","plans":[{"id":"a","priceCentsPerMonth":100,"unlimitedAI":true}]})").catalogue);
    CHECK_FALSE(readCatalogue(R"({"format":"sw67.plans","plans":[{"id":"a","priceCentsPerMonth":100,"aiCreditsPerMonth":"unlimited"}]})").catalogue);
    CHECK_FALSE(readCatalogue(R"({"format":"sw67.plans","plans":[{"id":"a","priceCentsPerMonth":-1}]})").catalogue);
    CHECK_FALSE(readCatalogue(R"({"format":"sw67.plans","plans":[
        {"id":"a","priceCentsPerMonth":1,"includes":["b"]},{"id":"b","priceCentsPerMonth":1,"includes":["a"]}]})").catalogue);
    CHECK_FALSE(readCatalogue(R"({"format":"sw67.plans","plans":[{"id":"a","priceCentsPerMonth":1,"includes":["zzz"]}]})").catalogue);
    CHECK_FALSE(readCatalogue(R"({"format":"sw67.plans","trialDays":365,"plans":[]})").catalogue);
    CHECK(readCatalogue(R"({"format":"sw67.plans","trialDays":14,"plans":[]})").catalogue->trialDays == 14);
}
