/**
 * @file shop_test.cpp
 * @brief Shopping and Demo Organizer: tags, locations, filters, files.
 *
 * Copyright (c) 2026, 6|7 World Studio contributors. LGPL-2.1.
 */

#include "sw67/shop/organizer.h"

#include <catch2/catch_test_macros.hpp>

using namespace sw67::shop;

TEST_CASE("Tags parse into body, size, style and colour", "[shop]")
{
    const auto t = parseTags("Body: Maitreya, size:M, Colour:Red, goth, unknown:thing, body:maitreya");
    REQUIRE(t.size() == 5);
    REQUIRE(t[0] == Tag{"body", "Maitreya"});
    REQUIRE(t[1] == Tag{"size", "M"});
    REQUIRE(t[2] == Tag{"color", "Red"});
    REQUIRE(t[3] == Tag{"", "goth"});
    REQUIRE(t[4] == Tag{"", "unknown:thing"});
    REQUIRE(formatTags(t) == "body:Maitreya, size:M, color:Red, goth, unknown:thing");
}

TEST_CASE("Locations read SLURLs and map addresses", "[shop]")
{
    auto a = parseLocation("http://maps.secondlife.com/secondlife/Fashion%20Island/128/64/22");
    REQUIRE(a);
    REQUIRE(a->region == "Fashion Island");
    REQUIRE(a->y == 64);
    REQUIRE(a->mapUrl() == "https://maps.secondlife.com/secondlife/Fashion%20Island/128/64/22");
    auto b = parseLocation("secondlife://Ahern/10/20/30");
    REQUIRE(b);
    REQUIRE(b->region == "Ahern");
    auto c = parseLocation("Bay City");
    REQUIRE(c);
    REQUIRE(c->x == 128);
    REQUIRE_FALSE(parseLocation(""));
    REQUIRE_FALSE(parseLocation("secondlife://Ahern/999/20/30"));
    REQUIRE_FALSE(parseLocation("secondlife://Ahern/abc"));
}

TEST_CASE("Filters match tags by category and words anywhere", "[shop]")
{
    const auto tags = parseTags("body:Maitreya Lara, size:M, color:red");
    REQUIRE(matches("body:maitreya", "Dress", "", tags));
    REQUIRE(matches("body:lara red", "Dress", "", tags));
    REQUIRE(matches("dress", "Silk Dress", "", tags));
    REQUIRE_FALSE(matches("body:legacy", "Dress", "", tags));
    REQUIRE_FALSE(matches("size:m color:blue", "Dress", "", tags));
    REQUIRE(matches("", "x", "", {}));
}

TEST_CASE("Liked demos not yet bought are a reminder, never a purchase", "[shop]")
{
    Organizer o;
    o.demos = {{"", "Silk Dress DEMO", "Shop A", "2026-11-18", Demo::Verdict::Like, "", {}},
               {"", "Boots demo", "Shop B", "2026-11-18", Demo::Verdict::Like, "", {}},
               {"", "Hat demo", "Shop B", "2026-11-18", Demo::Verdict::No, "", {}}};
    o.list = {{"Boots", "Shop B", 299, ListItem::Status::Bought, "", {}},
              {"Belt", "Shop B", 50, ListItem::Status::Want, "", {}},
              {"Coat", "Shop C", 400, ListItem::Status::Want, "", {}}};
    const auto liked = likedNotBought(o);
    REQUIRE(liked.size() == 1);
    REQUIRE(liked[0]->name == "Silk Dress DEMO");
    REQUIRE(o.wantedTotalL() == 450);
}

TEST_CASE("Shopping files round-trip and drop bad values", "[shop]")
{
    Organizer o;
    Store s;
    s.name = "Shop A";
    s.location = parseLocation("secondlife://Fashion Island/1/2/3");
    s.tags = parseTags("style:goth");
    o.stores = {s};
    o.list = {{"Belt", "Shop A", 50, ListItem::Status::Want, "black", parseTags("color:black")}};
    o.demos = {{"00000000-0000-4000-8000-000000000001", "Dress demo", "Shop A", "2026-11-18", Demo::Verdict::Maybe, "",
                parseTags("body:Reborn, size:L")}};
    const ReadResult r = readOrganizer(writeOrganizer(o));
    REQUIRE(r.organizer);
    REQUIRE(r.notes.empty());
    REQUIRE(r.organizer->stores[0].location->region == "Fashion Island");
    REQUIRE(r.organizer->list[0].tags == o.list[0].tags);
    REQUIRE(r.organizer->demos[0].verdict == Demo::Verdict::Maybe);
    REQUIRE(r.organizer->demos[0].tags.size() == 2);

    const ReadResult bad = readOrganizer(R"({"format":"sw67.shopping","version":1,
        "stores":[{"name":"X","location":"secondlife://Y/900/1/1","landmark":"nope"}],
        "list":[{"name":"Z","price":-5,"status":"weird"}],
        "demos":[{"item":"bad","name":"D","verdict":"??"}]})");
    REQUIRE(bad.organizer);
    REQUIRE_FALSE(bad.organizer->stores[0].location);
    REQUIRE(bad.organizer->stores[0].landmarkItemId.empty());
    REQUIRE(bad.organizer->list[0].priceL == 0);
    REQUIRE(bad.organizer->list[0].status == ListItem::Status::Want);
    REQUIRE(bad.organizer->demos[0].itemId.empty());
    REQUIRE(bad.notes.size() == 1);
    REQUIRE_FALSE(readOrganizer(R"({"format":"sw67.shopping","version":3})").organizer);
}
