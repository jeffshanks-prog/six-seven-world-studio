/**
 * @file organizer.h
 * @brief Shopping and Demo Organizer: saved stores (with their map
 *        location or landmark), a shopping list, and the demos you tried on,
 *        all taggable by body, size, style and colour.
 *
 *        Nothing here buys, pays or teleports. A price is a note; a store's
 *        location is shown on the world map for the user to choose to go.
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

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sw67::shop
{

inline constexpr std::string_view kFormat = "sw67.shopping";
inline constexpr int kFormatVersion = 1;
inline constexpr std::size_t kMaxEntries = 2000;
inline constexpr std::size_t kMaxTags = 24;

/// "body:Maitreya", "size:M", "style:goth", "color:red", or a plain word.
struct Tag
{
    std::string category; ///< "body", "size", "style", "color", or empty.
    std::string value;
    friend bool operator==(const Tag&, const Tag&) = default;
};

/// The four categories the organizer offers, in order.
const std::vector<std::string>& tagCategories();

/// Parses "body: Maitreya, size:M, red" into tags. Categories are matched
/// case-insensitively ("Colour" is "color"); unknown ones become plain words.
std::vector<Tag> parseTags(std::string_view text);
std::string formatTags(const std::vector<Tag>& tags);

/// A place in Second Life, from a SLURL or a maps.secondlife.com address.
struct Location
{
    std::string region;
    int x = 128;
    int y = 128;
    int z = 0;
    std::string mapUrl() const; ///< https://maps.secondlife.com/secondlife/Region%20Name/x/y/z
};
std::optional<Location> parseLocation(std::string_view text);

struct Store
{
    std::string name;
    std::optional<Location> location;
    std::string landmarkItemId; ///< A landmark in inventory, if saved from one.
    std::string notes;
    std::vector<Tag> tags;
};

struct ListItem
{
    enum class Status
    {
        Want,
        Bought,
        Skip,
    };
    std::string name;
    std::string store;
    int priceL = 0;      ///< A note only. Nothing is ever bought for you.
    Status status = Status::Want;
    std::string notes;
    std::vector<Tag> tags;
};

struct Demo
{
    enum class Verdict
    {
        Untried,
        Like,
        Maybe,
        No,
    };
    std::string itemId;  ///< The demo in inventory, if added from it.
    std::string name;
    std::string store;
    std::string triedOn; ///< "2026-11-18"
    Verdict verdict = Verdict::Untried;
    std::string notes;
    std::vector<Tag> tags;
};

std::string_view statusKey(ListItem::Status s);
std::string_view verdictKey(Demo::Verdict v);

struct Organizer
{
    std::vector<Store> stores;
    std::vector<ListItem> list;
    std::vector<Demo> demos;

    int wantedTotalL() const; ///< The sum of the prices still wanted, as a note.
};

/// True when every term of `query` matches: "body:maitreya red" matches
/// entries tagged body:Maitreya that mention "red" in a tag, name or note.
bool matches(std::string_view query, const std::string& name, const std::string& notes, const std::vector<Tag>& tags);

/// Liked demos with no bought shopping-list entry of the same name: a
/// reminder list. Never a purchase.
std::vector<const Demo*> likedNotBought(const Organizer& o);

std::string writeOrganizer(const Organizer& o);

struct ReadResult
{
    std::optional<Organizer> organizer;
    std::vector<std::string> notes;
};
ReadResult readOrganizer(std::string_view text);

} // namespace sw67::shop
