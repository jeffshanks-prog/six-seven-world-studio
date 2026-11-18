/**
 * @file inventory_test.cpp
 * @brief Inventory Cleanup Preview: exact preview, only confirmed moves,
 *        protected folders never touched, duplicates never deleted, undo
 *        records and per-item results when part of a move fails.
 *
 * Copyright (c) 2026, 6|7 World Studio contributors. LGPL-2.1.
 */

#include "sw67/inventory/cleanup.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdio>

using namespace sw67::inventory;

namespace
{
std::string id(int n)
{
    char buf[40];
    std::snprintf(buf, sizeof(buf), "00000000-0000-4000-8000-%012d", n);
    return buf;
}

Snapshot sample()
{
    Snapshot s;
    s.rootId = id(1);
    s.folders[id(1)] = {id(1), "My Inventory", "", "root"};
    s.folders[id(2)] = {id(2), "Textures", id(1), "texture"};
    s.folders[id(3)] = {id(3), "Landmarks", id(1), "landmark"};
    s.folders[id(4)] = {id(4), "Objects", id(1), "object"};
    s.folders[id(5)] = {id(5), "Trash", id(1), "trash"};
    s.folders[id(6)] = {id(6), "Current Outfit", id(1), "current_outfit"};
    s.folders[id(7)] = {id(7), "Shopping", id(1), ""};
    s.folders[id(8)] = {id(8), "Clothing", id(1), "clothing"};
    s.items = {
        {id(100), "Brick wall", "texture", id(1), id(900), id(50), false, false, true},
        {id(101), "Store LM", "landmark", id(1), id(901), id(50), false, false, true},
        {id(102), "Hair HUD", "object", id(1), id(902), id(50), false, true, true},     // worn
        {id(103), "Link to shirt", "clothing", id(1), id(903), id(50), true, false, true}, // link
        {id(104), "Brick wall (copy)", "texture", id(7), id(900), id(50), false, false, true},
        {id(105), "Old texture", "texture", id(5), id(904), id(50), false, false, true}, // in trash
        {id(106), "Sneakers", "object", id(7), "", id(51), false, false, false},
        {id(107), "Sneakers 2", "object", id(4), "", id(51), false, false, false},
        {id(108), "Sneakers", "object", id(4), "", id(52), false, false, false},   // other creator
        {id(109), "Already home", "texture", id(2), id(905), id(50), false, false, true},
    };
    return s;
}

const Move* moveFor(const std::vector<Move>& moves, const std::string& item)
{
    for (const Move& m : moves)
        if (m.itemId == item) return &m;
    return nullptr;
}
} // namespace

TEST_CASE("Cleanup suggests loose items go to their system folder", "[inventory]")
{
    const Snapshot s = sample();
    const CleanupPlan p = planCleanup(s);
    REQUIRE(moveFor(p.moves, id(100)));
    REQUIRE(moveFor(p.moves, id(100))->toFolderId == id(2));
    REQUIRE(moveFor(p.moves, id(101))->toFolderId == id(3));
    REQUIRE_FALSE(moveFor(p.moves, id(102))); // worn
    REQUIRE_FALSE(moveFor(p.moves, id(103))); // link
    REQUIRE_FALSE(moveFor(p.moves, id(104))); // not in a scanned folder
    REQUIRE_FALSE(moveFor(p.moves, id(109))); // already home
    REQUIRE(p.moves.size() == 2);
    REQUIRE(p.skipped.size() == 2);

    const std::string preview = p.describe(s);
    REQUIRE(preview.find("Brick wall: My Inventory  ->  My Inventory/Textures") != std::string::npos);
    REQUIRE(preview.find("Nothing is deleted") != std::string::npos);
    REQUIRE(preview.find("Hair HUD: it is worn now") != std::string::npos);
}

TEST_CASE("Cleanup never touches the trash, Current Outfit or unknown trees", "[inventory]")
{
    Snapshot s = sample();
    REQUIRE(s.isProtected(id(5)));
    REQUIRE(s.isProtected(id(6)));
    REQUIRE(s.isProtected(id(777))); // unknown folder
    REQUIRE_FALSE(s.isProtected(id(7)));
    s.folders[id(9)] = {id(9), "#RLV", id(1), ""};
    s.folders[id(10)] = {id(10), "~collar", id(9), ""};
    REQUIRE(s.isProtected(id(10))); // RLV shared folders keep their paths
    Options o;
    o.scanFolders = {id(5), id(6)};
    const CleanupPlan p = planCleanup(s, o);
    REQUIRE(p.moves.empty());
    // A duplicate in the trash is not reported either.
    for (const DuplicateGroup& g : p.duplicates)
        REQUIRE(std::find(g.itemIds.begin(), g.itemIds.end(), id(105)) == g.itemIds.end());
}

TEST_CASE("Duplicates are grouped for review, never deleted", "[inventory]")
{
    const Snapshot s = sample();
    const CleanupPlan p = planCleanup(s);
    REQUIRE(p.duplicates.size() == 2);
    bool asset = false, name = false;
    for (const DuplicateGroup& g : p.duplicates)
    {
        if (g.confidence == DuplicateGroup::Confidence::SameAsset)
        {
            asset = true;
            REQUIRE(g.itemIds.size() == 2); // 100 and 104
        }
        else
        {
            name = true;
            REQUIRE(g.itemIds.size() == 2); // 106 and 107: same creator, "Sneakers 2"
            REQUIRE(std::find(g.itemIds.begin(), g.itemIds.end(), id(108)) == g.itemIds.end());
        }
    }
    REQUIRE(asset);
    REQUIRE(name);
    // Duplicates add no moves: the plan's only moves are the two loose items.
    for (const Move& m : p.moves)
        REQUIRE((m.itemId == id(100) || m.itemId == id(101)));
}

TEST_CASE("Only confirmed moves are applied, and a stale selection moves nothing extra", "[inventory]")
{
    const CleanupPlan p = planCleanup(sample());
    const std::vector<Move> c = confirmedMoves(p, {id(101), id(104), id(999)});
    REQUIRE(c.size() == 1);
    REQUIRE(c[0].itemId == id(101));
    const auto reqs = gateRequests(c);
    REQUIRE(reqs.size() == 1);
    REQUIRE(reqs[0].kind == sw67::rlv::ActionKind::MoveInventory);
    REQUIRE(reqs[0].destinationId == id(3));
}

TEST_CASE("A partly failed apply reports each item", "[inventory]")
{
    const Snapshot s = sample();
    const CleanupPlan p = planCleanup(s);
    const std::vector<Move> sent = confirmedMoves(p, {id(100), id(101)});
    // The texture moved; the landmark did not.
    std::map<std::string, std::string> now = {{id(100), id(2)}, {id(101), id(1)}};
    const ApplyReport r = checkApplied(sent, {{id(102), "RLV blocked moving it"}}, now, {{id(102), "Hair HUD"}});
    REQUIRE(r.count(MoveResult::Outcome::Moved) == 1);
    REQUIRE(r.count(MoveResult::Outcome::Failed) == 1);
    REQUIRE(r.count(MoveResult::Outcome::Skipped) == 1);
    const std::string text = r.describe("moved");
    REQUIRE(text.find("Store LM (failed)") != std::string::npos);
    REQUIRE(text.find("Hair HUD (skipped): RLV blocked") != std::string::npos);
}

TEST_CASE("Undo puts back what cleanup moved and leaves later changes alone", "[inventory]")
{
    const Snapshot before = sample();
    const CleanupPlan p = planCleanup(before);
    const std::vector<Move> sent = confirmedMoves(p, {id(100), id(101)});
    const UndoLog log = undoLogFor(sent, before, "2026-11-18 01:00:00");

    const UndoReadResult rr = readUndoLog(writeUndoLog(log));
    REQUIRE(rr.log);
    REQUIRE(rr.log->entries.size() == 2);
    REQUIRE(rr.log->entries[0].fromPath == "My Inventory");

    // After: the texture is in Textures; the user later moved the landmark to Shopping.
    Snapshot after = before;
    for (InvItem& i : after.items)
    {
        if (i.id == id(100)) i.parentId = id(2);
        if (i.id == id(101)) i.parentId = id(7);
    }
    const UndoPlan u = planUndo(*rr.log, after);
    REQUIRE(u.moves.size() == 1);
    REQUIRE(u.moves[0].itemId == id(100));
    REQUIRE(u.moves[0].fromFolderId == id(2));
    REQUIRE(u.moves[0].toFolderId == id(1));
    REQUIRE(u.skipped.size() == 1);
    REQUIRE(u.skipped[0].reason.find("moved again since") != std::string::npos);

    // Original folder gone: skipped, never moved elsewhere.
    Snapshot gone = after;
    for (InvItem& i : gone.items)
        if (i.id == id(101)) i.parentId = id(3);
    UndoLog fromShopping = log;
    fromShopping.entries[1].fromFolderId = id(77);
    const UndoPlan u2 = planUndo(fromShopping, gone);
    REQUIRE(u2.moves.size() == 1);
    REQUIRE(u2.skipped[0].reason.find("no longer exists") != std::string::npos);
}

TEST_CASE("Undo records refuse malformed and foreign files", "[inventory]")
{
    REQUIRE_FALSE(readUndoLog("{}").log);
    REQUIRE_FALSE(readUndoLog(R"({"format":"sw67.inventory.undo","version":9})").log);
    const auto r = readUndoLog(
        R"({"format":"sw67.inventory.undo","version":1,"moves":[{"item":"x","from":"y","to":"z"}]})");
    REQUIRE(r.log);
    REQUIRE(r.log->entries.empty());
    REQUIRE(r.notes.size() == 1);
}

TEST_CASE("Base names set aside copies and numbering", "[inventory]")
{
    REQUIRE(baseName("Sneakers (copy)") == "sneakers");
    REQUIRE(baseName("Copy of Sneakers") == "sneakers");
    REQUIRE(baseName("Sneakers 2") == "sneakers");
    REQUIRE(baseName("Sneakers #12") == "sneakers");
    REQUIRE(baseName("2024") == "2024");
    REQUIRE(baseName("Dress v2") == "dress v2");
}
