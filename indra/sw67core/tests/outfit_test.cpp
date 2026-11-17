/**
 * @file outfit_test.cpp
 * @brief Outfit Planner: exact wear and take-off preview, an avatar kept
 *        whole, limits respected, missing items never bought, nothing ever
 *        deleted, and outfit files validated.
 *
 * Copyright (c) 2026, 6|7 World Studio contributors. LGPL-2.1.
 */

#include "sw67/outfit/planner.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdio>

using namespace sw67::outfit;

namespace
{
std::string uuid(int n)
{
    char buf[40];
    std::snprintf(buf, sizeof(buf), "00000000-0000-4000-8000-%012d", n);
    return buf;
}

Item body(int n, const char* type) { return {uuid(n), std::string("Body ") + type, ItemKind::BodyPart, type}; }
Item cloth(int n, const char* type) { return {uuid(n), std::string("Cloth ") + std::to_string(n), ItemKind::Clothing, type}; }
Item object(int n) { return {uuid(n), "Attachment " + std::to_string(n), ItemKind::Object, ""}; }

std::map<std::string, Item> own(const std::vector<Item>& items)
{
    std::map<std::string, Item> m;
    for (const Item& i : items) m[i.id] = i;
    return m;
}

const Step* stepFor(const Plan& p, const std::string& id)
{
    for (const Step& s : p.steps)
        if (s.item.id == id) return &s;
    return nullptr;
}

std::vector<Item> wornNow()
{
    return {body(1, "shape"), body(2, "skin"), body(3, "hair"), body(4, "eyes"), cloth(10, "shirt"), object(20),
            object(21)};
}
} // namespace

TEST_CASE("Replacing shows every item worn, taken off and kept", "[outfit]")
{
    const std::vector<Item> worn = wornNow();
    std::vector<Item> all = worn;
    for (const Item& i : {body(5, "hair"), cloth(11, "pants"), object(22)}) all.push_back(i);
    Outfit o;
    o.name = "Club night";
    o.items = {body(5, "hair"), cloth(11, "pants"), object(22), object(20)};
    const Plan p = planOutfit(o, worn, own(all), {});
    CHECK(stepFor(p, uuid(5))->what == Step::What::Wear);
    CHECK(stepFor(p, uuid(11))->what == Step::What::Wear);
    CHECK(stepFor(p, uuid(22))->what == Step::What::Wear);
    CHECK(stepFor(p, uuid(20))->what == Step::What::Keep);
    CHECK(stepFor(p, uuid(10))->what == Step::What::Remove);
    CHECK(stepFor(p, uuid(21))->what == Step::What::Remove);
    // The old hair is swapped by wearing the new one, not removed separately.
    const Step* oldHair = stepFor(p, uuid(3));
    REQUIRE(oldHair);
    CHECK(oldHair->what == Step::What::Remove);
    CHECK(oldHair->implicit);
    // Body parts the outfit lacks stay on.
    CHECK(stepFor(p, uuid(1))->what == Step::What::Keep);
    CHECK(p.changeCount() == 6);
    CHECK(p.attachmentsAfter == 2);
    CHECK(p.canWear());
    const auto req = gateRequests(p);
    CHECK(req.size() == 5);
    CHECK(std::none_of(req.begin(), req.end(), [](const auto& r) { return r.targetId == uuid(3); }));
}

TEST_CASE("Adding keeps everything worn now", "[outfit]")
{
    const std::vector<Item> worn = wornNow();
    std::vector<Item> all = worn;
    all.push_back(object(22));
    Outfit o;
    o.replace = false;
    o.items = {object(22)};
    const Plan p = planOutfit(o, worn, own(all), {});
    CHECK(p.changeCount() == 1);
    CHECK(stepFor(p, uuid(10))->what == Step::What::Keep);
    CHECK(p.attachmentsAfter == 3);
}

TEST_CASE("Items the user always keeps are never taken off", "[outfit]")
{
    const std::vector<Item> worn = wornNow();
    Outfit o;
    o.items = {};
    const Plan p = planOutfit(o, worn, own(worn), {}, {uuid(21)});
    CHECK(stepFor(p, uuid(21))->what == Step::What::Keep);
    CHECK(stepFor(p, uuid(20))->what == Step::What::Remove);
}

TEST_CASE("The attachment limit blocks an outfit that would pass it", "[outfit]")
{
    std::vector<Item> worn = wornNow();
    std::vector<Item> all = worn;
    Outfit o;
    o.replace = false;
    for (int n = 100; n < 137; ++n)
    {
        o.items.push_back(object(n));
        all.push_back(object(n));
    }
    const Plan p = planOutfit(o, worn, own(all), {});
    CHECK(p.attachmentsAfter == 39);
    CHECK_FALSE(p.canWear());
    Limits roomy;
    roomy.attachments = 40;
    CHECK(planOutfit(o, worn, own(all), roomy).canWear());
}

TEST_CASE("Missing items are listed and never bought or replaced", "[outfit]")
{
    const std::vector<Item> worn = wornNow();
    Outfit o;
    o.items = {cloth(50, "jacket"), body(51, "skin")};
    const Plan p = planOutfit(o, worn, own(worn), {});
    CHECK(stepFor(p, uuid(50))->what == Step::What::Missing);
    CHECK(stepFor(p, uuid(51))->what == Step::What::Missing);
    CHECK(stepFor(p, uuid(2))->what == Step::What::Keep); // the worn skin stays: the new one is missing
    CHECK(gateRequests(p).size() == 3); // only the removals of shirt and two attachments
    const std::string text = p.describe();
    CHECK(text.find("nothing is bought") != std::string::npos);
    for (const Step& s : p.steps)
    {
        CHECK(s.reason.find("buy ") == std::string::npos);
    }
}

TEST_CASE("Two body parts of one type keep only the first", "[outfit]")
{
    std::vector<Item> all = wornNow();
    all.push_back(body(60, "shape"));
    all.push_back(body(61, "shape"));
    Outfit o;
    o.items = {body(60, "shape"), body(61, "shape")};
    const Plan p = planOutfit(o, wornNow(), own(all), {});
    CHECK(stepFor(p, uuid(60))->what == Step::What::Wear);
    CHECK(stepFor(p, uuid(61)) == nullptr);
    CHECK_FALSE(p.problems.empty());
}

TEST_CASE("An outfit round-trips, and bad entries are dropped with a reason", "[outfit]")
{
    Outfit o = outfitFromWorn("Every day", wornNow());
    o.occasion = "Work";
    o.tags = {"casual", "blue"};
    o.notes = "With the grey boots.";
    const ReadResult back = readOutfit(writeOutfit(o));
    REQUIRE(back.outfit);
    CHECK(*back.outfit == o);

    const ReadResult bad = readOutfit(R"({"format":"sw67.outfit","version":1,"items":[
        {"id":"not-a-uuid","name":"A","kind":"object"},
        {"id":"00000000-0000-4000-8000-000000000001","name":"B","kind":"bodypart","type":"shirt"},
        {"id":"00000000-0000-4000-8000-000000000002","name":"C","kind":"hat"},
        {"id":"00000000-0000-4000-8000-000000000003","name":"D","kind":"clothing","type":"shirt"},
        {"id":"00000000-0000-4000-8000-000000000003","name":"D again","kind":"clothing","type":"shirt"}]})");
    REQUIRE(bad.outfit);
    CHECK(bad.outfit->items.size() == 1);
    CHECK(bad.notes.size() == 4);
    CHECK_FALSE(readOutfit(R"({"format":"sw67.outfit","version":3})").outfit);
}

TEST_CASE("Comparing outfits lists what differs", "[outfit]")
{
    Outfit a, b;
    a.items = {object(1), object(2)};
    b.items = {object(2), object(3)};
    const Difference d = compare(a, b);
    REQUIRE(d.onlyInA.size() == 1);
    REQUIRE(d.onlyInB.size() == 1);
    CHECK(d.onlyInA[0].id == uuid(1));
    CHECK(d.onlyInB[0].id == uuid(3));
    CHECK(fileNameFor(a) == "outfit.sw67outfit.json");
}
