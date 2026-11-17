/**
 * @file json_test.cpp
 * @brief The JSON reader takes files as hostile and round-trips what it writes.
 *
 * Copyright (c) 2026, 6|7 World Studio contributors. LGPL-2.1.
 */

#include "sw67/io/json.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

using sw67::io::Json;
using sw67::io::JsonError;
using sw67::io::JsonLimits;
using sw67::io::parseJson;
using sw67::io::writeJson;

TEST_CASE("JSON values round trip through write and parse", "[json]")
{
    Json doc = Json::object();
    doc.set("name", "Caf\xC3\xA9 \"quoted\"\n");
    doc.set("count", 42);
    doc.set("ratio", 0.125);
    doc.set("on", true);
    doc.set("nothing", nullptr);
    Json list = Json::array();
    list.push(1).push("two").push(Json::object());
    doc.set("list", list);

    const std::string text = writeJson(doc);
    const auto back = parseJson(text);
    REQUIRE(back);
    CHECK((*back)["name"].asString() == "Caf\xC3\xA9 \"quoted\"\n");
    CHECK((*back)["count"].asInt() == 42);
    CHECK((*back)["ratio"].asNumber() == 0.125);
    CHECK((*back)["on"].asBool());
    CHECK((*back)["nothing"].isNull());
    CHECK((*back)["list"].items().size() == 3);
    CHECK(writeJson(*back) == text);
}

TEST_CASE("JSON copies do not share writes", "[json]")
{
    Json a = Json::object();
    a.set("x", 1);
    Json b = a;
    b.set("x", 2);
    CHECK(a["x"].asInt() == 1);
    CHECK(b["x"].asInt() == 2);
}

TEST_CASE("JSON escapes decode, surrogate pairs included", "[json]")
{
    const auto v = parseJson(R"({"s":"a\u00e9\ud83d\ude00\t"})");
    REQUIRE(v);
    CHECK((*v)["s"].asString() == "a\xC3\xA9\xF0\x9F\x98\x80\t");
}

TEST_CASE("Malformed JSON is refused without throwing", "[json]")
{
    for (const char* bad : {"", "{", "[1,]", "{\"a\":}", "{'a':1}", "01", "1.", "\"\\ud800\"", "\"tab\there\"", "nul",
                            "{\"a\":1} x", "[1 2]", "1e999"})
    {
        JsonError err;
        CHECK_FALSE(parseJson(bad, &err));
        CHECK_FALSE(err.what.empty());
    }
}

TEST_CASE("JSON limits stop deep nesting and oversized input", "[json]")
{
    JsonLimits limits;
    limits.maxDepth = 8;
    CHECK(parseJson("[[[[[[[[1]]]]]]]]", nullptr, limits));
    CHECK_FALSE(parseJson("[[[[[[[[[[1]]]]]]]]]]", nullptr, limits));

    const std::string deep(100000, '[');
    CHECK_FALSE(parseJson(deep)); // Default depth 64: refused, not a stack overflow.

    limits = {};
    limits.maxBytes = 10;
    CHECK_FALSE(parseJson("[1,2,3,4,5,6]", nullptr, limits));
}

TEST_CASE("Reading a missing or mistyped member gives a safe default", "[json]")
{
    const auto v = parseJson(R"({"n":"7","f":1.5})");
    REQUIRE(v);
    CHECK((*v)["missing"].isNull());
    CHECK((*v)["n"].asInt(-1) == -1);
    CHECK((*v)["f"].asInt(-1) == -1);
    CHECK((*v)["n"]["deeper"].isNull());
    CHECK((*v)["missing"].items().empty());
}
