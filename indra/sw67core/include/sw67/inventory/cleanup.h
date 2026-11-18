/**
 * @file cleanup.h
 * @brief Inventory Cleanup Preview: suggested folder moves and likely
 *        duplicates, shown exactly before anything happens; only the moves
 *        the user confirms are applied, each one recorded with the folder it
 *        came from so it can be undone.
 *
 *        Nothing here deletes. A duplicate is a suggestion to review, and the
 *        module has no delete step to suggest: there is no code path from a
 *        duplicate to the trash.
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

namespace sw67::inventory
{

inline constexpr std::string_view kUndoFormat = "sw67.inventory.undo";
inline constexpr int kUndoFormatVersion = 1;
inline constexpr std::size_t kMaxMovesPerApply = 500;

/// A folder, as the viewer's inventory model has it.
struct Folder
{
    std::string id;
    std::string name;
    std::string parentId;
    /// The system folder type the viewer gives it ("texture", "landmark",
    /// "trash", "current_outfit", "outfit", "lost_and_found", "root"...), or
    /// empty for a folder the user made.
    std::string systemType;
};

/// An item, as the viewer's inventory model has it.
struct InvItem
{
    std::string id;
    std::string name;
    std::string type;     ///< "texture", "sound", "landmark", "clothing", "bodypart", "object", "notecard",
                          ///< "script", "animation", "gesture", "material", "settings", "snapshot"...
    std::string parentId;
    std::string assetId;  ///< Empty or the null UUID when the viewer cannot see it (no-mod items).
    std::string creatorId;
    bool link = false;    ///< A link: never moved or counted as a duplicate.
    bool worn = false;    ///< Worn or attached now: never moved.
    bool copyable = true;
};

struct Snapshot
{
    std::map<std::string, Folder> folders; ///< By ID.
    std::vector<InvItem> items;
    std::string rootId;                    ///< My Inventory.

    const Folder* folder(const std::string& id) const;
    /// "My Inventory/Clothes/Summer". Stops at 32 levels.
    std::string pathOf(const std::string& folderId) const;
    /// True for the trash, Current Outfit, Outfits, the Library, #RLV, the
    /// animation overrides, Marketplace folders and their contents: cleanup never moves anything into or out of them.
    bool isProtected(const std::string& folderId) const;
    /// The system folder for an item type ("texture" -> Textures), if any.
    std::optional<std::string> homeFor(const std::string& itemType) const;
};

struct Move
{
    std::string itemId;
    std::string itemName;
    std::string fromFolderId;
    std::string toFolderId;
    std::string reason;
};

struct DuplicateGroup
{
    enum class Confidence
    {
        SameAsset,  ///< Same type and the same asset: copies of one thing.
        SameName,   ///< Same type, creator and name once "(copy)" and trailing numbers are set aside.
    };
    Confidence confidence = Confidence::SameName;
    std::vector<std::string> itemIds;
    std::string label;  ///< For the list: the shared name.
};

struct Options
{
    /// Folders to tidy: items directly inside them are suggested a move to
    /// their type's system folder. Empty means My Inventory itself.
    std::set<std::string> scanFolders;
    bool findDuplicates = true;
};

struct Skipped
{
    std::string itemId;
    std::string reason;
};

struct CleanupPlan
{
    std::vector<Move> moves;
    std::vector<DuplicateGroup> duplicates;
    std::vector<Skipped> skipped;

    /// The exact preview: every move with both folder paths, then every
    /// duplicate group, saying that nothing is deleted.
    std::string describe(const Snapshot& snap) const;
};

CleanupPlan planCleanup(const Snapshot& snap, const Options& options = {});

/// The moves the user ticked, in the plan's order. IDs not in the plan are
/// ignored, so a stale selection cannot move anything unplanned.
std::vector<Move> confirmedMoves(const CleanupPlan& plan, const std::set<std::string>& confirmedItemIds);

/// The RLV requests for the moves, so the gate can block any of them first.
std::vector<rlv::ActionRequest> gateRequests(const std::vector<Move>& moves);

/// The undo record, written before a move is sent.
struct UndoEntry
{
    std::string itemId;
    std::string itemName;
    std::string fromFolderId;
    std::string fromPath;  ///< For people, should the folder be gone.
    std::string toFolderId;
};

struct UndoLog
{
    std::string created;   ///< UTC, "2026-11-18 01:02:03".
    std::vector<UndoEntry> entries;
};

UndoLog undoLogFor(const std::vector<Move>& moves, const Snapshot& before, std::string created);
std::string writeUndoLog(const UndoLog& log);

struct UndoReadResult
{
    std::optional<UndoLog> log;
    std::vector<std::string> notes;
};
UndoReadResult readUndoLog(std::string_view text);

/// What applying a move did, for the per-item report.
struct MoveResult
{
    enum class Outcome
    {
        Moved,
        Skipped,   ///< Not attempted: blocked, protected, gone or moved meanwhile.
        Failed,    ///< Attempted; the item is not where it should be.
    };
    std::string itemId;
    std::string itemName;
    Outcome outcome = Outcome::Moved;
    std::string reason;
};

struct ApplyReport
{
    std::vector<MoveResult> results;
    std::size_t count(MoveResult::Outcome outcome) const;
    std::string describe(std::string_view verb) const;  ///< verb: "moved", "moved back".
};

/// Checks a finished apply: each move is Moved when the item is now in its
/// destination. `parentNow` is each item's folder now, by item ID.
ApplyReport checkApplied(const std::vector<Move>& sent, const std::vector<Skipped>& notSent,
                         const std::map<std::string, std::string>& parentNow,
                         const std::map<std::string, std::string>& names = {});

/// Undo: a move back for every entry whose item is still where cleanup put
/// it and whose original folder still exists. Anything moved since, or whose
/// folder is gone, is skipped with a reason; nothing is moved anywhere else.
struct UndoPlan
{
    std::vector<Move> moves;
    std::vector<Skipped> skipped;
};
UndoPlan planUndo(const UndoLog& log, const Snapshot& now);

/// The name with "(copy)", "Copy of" and a trailing number set aside, lower case.
std::string baseName(std::string_view name);

} // namespace sw67::inventory
