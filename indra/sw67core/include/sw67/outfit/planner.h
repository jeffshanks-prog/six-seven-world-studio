/**
 * @file planner.h
 * @brief Outfit Planner: outfits saved as plain files (items by inventory
 *        ID, with names, notes, tags and occasion), a preview of exactly
 *        what wearing one would put on and take off, and the checks that
 *        keep an avatar whole: one shape, skin, hair and eyes, the clothing
 *        layer limit and the attachment limit the grid gives the account.
 *
 *        The planner never deletes, buys or moves an item. An item the
 *        outfit names but the user no longer has is listed as missing; it is
 *        never bought or replaced for them.
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
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace sw67::outfit
{

inline constexpr std::string_view kFormat = "sw67.outfit";
inline constexpr int kFormatVersion = 1;
inline constexpr std::size_t kMaxItems = 500;

enum class ItemKind
{
    BodyPart, ///< Shape, skin, hair, eyes: exactly one of each is worn.
    Clothing, ///< Layers, up to the clothing limit in all.
    Object,   ///< Attachments and HUDs, up to the attachment limit.
};

std::string_view kindKey(ItemKind k);
std::optional<ItemKind> parseKind(std::string_view key);

/// The body part types an avatar always wears one of.
const std::vector<std::string>& bodyPartTypes();
/// Every wearable type name the viewer uses ("shape", "shirt", ...).
bool isWearableTypeName(std::string_view name);

struct Item
{
    std::string id;           ///< The inventory item's UUID (not a link's).
    std::string name;
    ItemKind kind = ItemKind::Object;
    std::string wearableType; ///< For body parts and clothing; empty for objects.
    /// For objects: the attachment point it was worn on when the outfit was
    /// saved ("Chest", "HUD Center"), or empty when not known. Second Life
    /// keeps an item's point in the item itself, so an unworn item's point is
    /// only known once it has been worn.
    std::string attachPoint;
    friend bool operator==(const Item&, const Item&) = default;
};

struct Outfit
{
    std::string name;
    std::string occasion;
    std::string notes;
    std::vector<std::string> tags;
    std::vector<Item> items;
    /// On: wearing takes off worn clothing and attachments the outfit lacks.
    /// Off: the outfit is added to what is worn.
    bool replace = true;
    friend bool operator==(const Outfit&, const Outfit&) = default;
};

struct Limits
{
    int attachments = 38;     ///< The grid's limit for the account (38 on Second Life today).
    int clothingLayers = 60;  ///< The viewer's limit on clothing layers in all.
};

struct Step
{
    enum class What
    {
        Wear,
        Remove,
        Keep,     ///< Worn now and in the outfit.
        Missing,  ///< In the outfit, not in your inventory. Never bought or replaced.
    };
    What what = What::Keep;
    Item item;
    std::string reason;
    /// For a body part taken off because another of its type is worn: the
    /// viewer swaps it as part of wearing the new one, so nothing separate
    /// is sent for it.
    bool implicit = false;
};

struct Problem
{
    bool blocking = false; ///< Blocking problems stop the outfit being worn as planned.
    std::string text;
};

struct Plan
{
    std::vector<Step> steps;
    std::vector<Problem> problems;
    int attachmentsAfter = 0;
    int clothingAfter = 0;

    bool canWear() const;
    std::size_t changeCount() const;   ///< Wear and Remove steps.
    std::string describe() const;      ///< One line a step, then the problems.
};

/// What wearing `outfit` would do. `worn` is what is worn now; `owned` is
/// every wearable item in the user's inventory, by ID; `alwaysKeep` are
/// items the user never wants taken off by an outfit (an AO HUD, say).
Plan planOutfit(const Outfit& outfit, const std::vector<Item>& worn, const std::map<std::string, Item>& owned,
                const Limits& limits, const std::set<std::string>& alwaysKeep = {});

/// Attachment points the outfit puts more than one item on, with those items.
/// Second Life wears several items on one point together, so this is a note
/// for the user (they may overlap or one may be an older copy), not a block.
std::map<std::string, std::vector<Item>> sharedPoints(const Plan& plan);

/// Outfit Doctor's check after wearing: for every Wear and Remove step, did it
/// happen? `wornNow` is what is worn once the viewer has finished, by item ID.
struct ItemResult
{
    enum class Outcome
    {
        Done,        ///< Worn, or taken off, as planned.
        NotWorn,     ///< Asked to wear it; it is not worn.
        StillWorn,   ///< Asked to take it off; it is still worn.
        Missing,     ///< Not in the inventory, so never asked for.
    };
    Item item;
    bool wanted = true;  ///< True for a Wear step, false for a Remove step.
    Outcome outcome = Outcome::Done;
    std::string reason;
};

struct RestoreReport
{
    std::vector<ItemResult> results;
    std::size_t done() const;
    std::size_t failed() const;   ///< NotWorn, StillWorn and Missing.
    bool complete() const { return failed() == 0; }
    std::string describe() const; ///< A summary line, then one line per item that did not go as planned.
};

RestoreReport checkRestore(const Plan& plan, const std::set<std::string>& wornNow);

/// The RLV requests for the plan's Wear and Remove steps, in order, so the
/// gate can preview which it will block. Implicit body part swaps are left
/// out; their Wear step covers them.
std::vector<rlv::ActionRequest> gateRequests(const Plan& plan);

/// An outfit of exactly what is worn now.
Outfit outfitFromWorn(std::string name, const std::vector<Item>& worn);

struct Difference
{
    std::vector<Item> onlyInA;
    std::vector<Item> onlyInB;
};
Difference compare(const Outfit& a, const Outfit& b);

std::string writeOutfit(const Outfit& outfit);

struct ReadResult
{
    std::optional<Outfit> outfit;
    std::vector<std::string> notes;
};
/// Reads an outfit file. Newer formats are refused; items with a malformed
/// ID, kind or type are dropped with a note, as are duplicates.
ReadResult readOutfit(std::string_view text);

/// A file name for an outfit: its name made safe, plus ".sw67outfit.json".
std::string fileNameFor(const Outfit& outfit);

} // namespace sw67::outfit
