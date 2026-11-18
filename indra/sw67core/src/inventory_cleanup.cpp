/**
 * @file inventory_cleanup.cpp
 * @brief Inventory Cleanup Preview. See cleanup.h.
 *
 * Copyright (c) 2026, 6|7 World Studio contributors.
 *
 * The source code in this file is provided to you under the terms of the
 * GNU Lesser General Public License, version 2.1, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE. Terms of the LGPL can be found in doc/LGPL-license.txt
 * in this distribution, or online at http://www.gnu.org/licenses/lgpl-2.1.txt
 */

#include "sw67/inventory/cleanup.h"

#include "sw67/io/json.h"

#include <algorithm>
#include <cctype>

namespace sw67::inventory
{
namespace
{
using io::Json;

const std::set<std::string>& protectedTypes()
{
    static const std::set<std::string> types = {"trash", "current_outfit", "outfit", "my_outfits", "library",
                                                "inbox", "outbox", "marketplace", "marketplace_stock",
                                                "marketplace_version", "suitcase", "rlv",
                                                "anim_overrides"};
    return types;
}

/// Item types with a system folder of the same name.
const std::set<std::string>& homedTypes()
{
    static const std::set<std::string> types = {"texture", "sound",     "landmark", "clothing", "bodypart",
                                                "object",  "notecard",  "script",   "animation", "gesture",
                                                "material", "settings", "snapshot", "callingcard"};
    return types;
}

bool isNullId(const std::string& id)
{
    return id.empty() || id == "00000000-0000-0000-0000-000000000000";
}

std::string lower(std::string_view s)
{
    std::string out(s);
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

std::string trim(std::string s)
{
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
    size_t start = 0;
    while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) ++start;
    return s.substr(start);
}

bool validId(const std::string& id)
{
    if (id.size() != 36) return false;
    for (size_t i = 0; i < id.size(); ++i)
    {
        const char c = id[i];
        if (i == 8 || i == 13 || i == 18 || i == 23)
        {
            if (c != '-') return false;
        }
        else if (!std::isxdigit(static_cast<unsigned char>(c)))
        {
            return false;
        }
    }
    return true;
}
} // namespace

const Folder* Snapshot::folder(const std::string& id) const
{
    const auto it = folders.find(id);
    return it == folders.end() ? nullptr : &it->second;
}

std::string Snapshot::pathOf(const std::string& folderId) const
{
    std::vector<std::string> names;
    std::string id = folderId;
    for (int depth = 0; depth < 32; ++depth)
    {
        const Folder* f = folder(id);
        if (!f) break;
        names.push_back(f->name);
        if (f->parentId.empty() || f->id == rootId) break;
        id = f->parentId;
    }
    if (names.empty()) return "(a folder that no longer exists)";
    std::string out;
    for (auto it = names.rbegin(); it != names.rend(); ++it) out += (out.empty() ? "" : "/") + *it;
    return out;
}

bool Snapshot::isProtected(const std::string& folderId) const
{
    std::string id = folderId;
    for (int depth = 0; depth < 32; ++depth)
    {
        const Folder* f = folder(id);
        if (!f) return true; // unknown folders (the Library's, say) are left alone
        if (protectedTypes().count(f->systemType)) return true;
        if (f->name == "#RLV") return true; // RLV shared folders: devices find items by these paths
        if (f->id == rootId) return false;
        if (f->parentId.empty()) return true; // a tree that is not My Inventory
        id = f->parentId;
    }
    return true;
}

std::optional<std::string> Snapshot::homeFor(const std::string& itemType) const
{
    if (!homedTypes().count(itemType)) return std::nullopt;
    for (const auto& [id, f] : folders)
    {
        if (f.systemType == itemType && f.parentId == rootId) return id;
    }
    return std::nullopt;
}

std::string baseName(std::string_view name)
{
    std::string s = lower(trim(std::string(name)));
    bool changed = true;
    while (changed)
    {
        changed = false;
        for (std::string_view prefix : {"copy of ", "copy "})
        {
            if (s.rfind(prefix, 0) == 0)
            {
                s = trim(s.substr(prefix.size()));
                changed = true;
            }
        }
        for (std::string_view suffix : {"(copy)", " copy", "(1)", "(2)", "(3)"})
        {
            if (s.size() > suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0)
            {
                s = trim(s.substr(0, s.size() - suffix.size()));
                changed = true;
            }
        }
        // A trailing " 2", " #3", " - 4" (but not a name that is only digits).
        size_t end = s.size();
        while (end > 0 && std::isdigit(static_cast<unsigned char>(s[end - 1]))) --end;
        if (end < s.size() && end > 0)
        {
            size_t cut = end;
            while (cut > 0 && (s[cut - 1] == ' ' || s[cut - 1] == '#' || s[cut - 1] == '-')) --cut;
            if (cut < end && cut > 0)
            {
                s = trim(s.substr(0, cut));
                changed = true;
            }
        }
    }
    return s;
}

CleanupPlan planCleanup(const Snapshot& snap, const Options& options)
{
    CleanupPlan plan;
    std::set<std::string> scan = options.scanFolders;
    if (scan.empty()) scan.insert(snap.rootId);

    for (const InvItem& item : snap.items)
    {
        if (!scan.count(item.parentId)) continue;
        const std::optional<std::string> home = snap.homeFor(item.type);
        if (!home || *home == item.parentId) continue;
        if (item.link)
        {
            plan.skipped.push_back({item.id, "it is a link; links stay where they are"});
            continue;
        }
        if (item.worn)
        {
            plan.skipped.push_back({item.id, "it is worn now"});
            continue;
        }
        if (snap.isProtected(item.parentId) || snap.isProtected(*home))
        {
            plan.skipped.push_back({item.id, "its folder is one cleanup never changes"});
            continue;
        }
        plan.moves.push_back({item.id, item.name, item.parentId, *home,
                              "a loose " + item.type + " goes in " + snap.pathOf(*home)});
    }

    if (options.findDuplicates)
    {
        std::map<std::pair<std::string, std::string>, std::vector<const InvItem*>> byAsset;
        std::map<std::tuple<std::string, std::string, std::string>, std::vector<const InvItem*>> byName;
        for (const InvItem& item : snap.items)
        {
            if (item.link || snap.isProtected(item.parentId)) continue;
            if (!isNullId(item.assetId))
            {
                byAsset[{item.type, lower(item.assetId)}].push_back(&item);
            }
            else
            {
                byName[{item.type, item.creatorId, baseName(item.name)}].push_back(&item);
            }
        }
        for (const auto& [key, items] : byAsset)
        {
            if (items.size() < 2) continue;
            DuplicateGroup g;
            g.confidence = DuplicateGroup::Confidence::SameAsset;
            g.label = items.front()->name;
            for (const InvItem* i : items) g.itemIds.push_back(i->id);
            plan.duplicates.push_back(std::move(g));
        }
        for (const auto& [key, items] : byName)
        {
            if (items.size() < 2 || std::get<2>(key).empty()) continue;
            DuplicateGroup g;
            g.confidence = DuplicateGroup::Confidence::SameName;
            g.label = items.front()->name;
            for (const InvItem* i : items) g.itemIds.push_back(i->id);
            plan.duplicates.push_back(std::move(g));
        }
    }
    return plan;
}

std::string CleanupPlan::describe(const Snapshot& snap) const
{
    std::map<std::string, const InvItem*> byId;
    for (const InvItem& i : snap.items) byId[i.id] = &i;
    auto nameOf = [&](const std::string& id) {
        const auto it = byId.find(id);
        return it == byId.end() ? id : it->second->name;
    };

    std::string out;
    if (moves.empty())
    {
        out += "No moves to suggest.\n";
    }
    else
    {
        out += std::to_string(moves.size()) + " suggested move(s). Only the ones you tick are made:\n";
        for (const Move& m : moves)
        {
            out += "  - " + m.itemName + ": " + snap.pathOf(m.fromFolderId) + "  ->  " + snap.pathOf(m.toFolderId) +
                   "\n";
        }
    }
    if (!duplicates.empty())
    {
        out += "\n" + std::to_string(duplicates.size()) +
               " group(s) of likely duplicates, for you to review. Nothing is deleted or moved because of them:\n";
        for (const DuplicateGroup& g : duplicates)
        {
            out += "  - \"" + g.label + "\" x" + std::to_string(g.itemIds.size()) +
                   (g.confidence == DuplicateGroup::Confidence::SameAsset ? " (same asset)" : " (same name)");
            std::string where;
            for (const std::string& id : g.itemIds)
            {
                const auto it = byId.find(id);
                if (it != byId.end()) where += (where.empty() ? "" : "; ") + snap.pathOf(it->second->parentId);
            }
            out += ": " + where + "\n";
        }
    }
    if (!skipped.empty())
    {
        out += "\nLeft alone:\n";
        for (const Skipped& s : skipped) out += "  - " + nameOf(s.itemId) + ": " + s.reason + "\n";
    }
    return out;
}

std::vector<Move> confirmedMoves(const CleanupPlan& plan, const std::set<std::string>& confirmedItemIds)
{
    std::vector<Move> out;
    for (const Move& m : plan.moves)
    {
        if (confirmedItemIds.count(m.itemId) && out.size() < kMaxMovesPerApply) out.push_back(m);
    }
    return out;
}

std::vector<rlv::ActionRequest> gateRequests(const std::vector<Move>& moves)
{
    std::vector<rlv::ActionRequest> out;
    for (const Move& m : moves)
    {
        rlv::ActionRequest r;
        r.kind = rlv::ActionKind::MoveInventory;
        r.targetId = m.itemId;
        r.destinationId = m.toFolderId;
        out.push_back(r);
    }
    return out;
}

UndoLog undoLogFor(const std::vector<Move>& moves, const Snapshot& before, std::string created)
{
    UndoLog log;
    log.created = std::move(created);
    for (const Move& m : moves)
    {
        log.entries.push_back({m.itemId, m.itemName, m.fromFolderId, before.pathOf(m.fromFolderId), m.toFolderId});
    }
    return log;
}

std::string writeUndoLog(const UndoLog& log)
{
    Json root = Json::object();
    root.set("format", Json(kUndoFormat));
    root.set("version", Json(kUndoFormatVersion));
    root.set("created", Json(log.created));
    Json entries = Json::array();
    for (const UndoEntry& e : log.entries)
    {
        Json j = Json::object();
        j.set("item", Json(e.itemId));
        j.set("name", Json(e.itemName));
        j.set("from", Json(e.fromFolderId));
        j.set("from_path", Json(e.fromPath));
        j.set("to", Json(e.toFolderId));
        entries.push(std::move(j));
    }
    root.set("moves", std::move(entries));
    return io::writeJson(root);
}

UndoReadResult readUndoLog(std::string_view text)
{
    UndoReadResult r;
    io::JsonError err;
    const auto root = io::parseJson(text, &err);
    if (!root || !root->isObject() || (*root)["format"].asString("") != kUndoFormat)
    {
        r.notes.push_back("This is not a 6|7 inventory undo record.");
        return r;
    }
    const int version = (*root)["version"].asInt(0);
    if (version < 1 || version > kUndoFormatVersion)
    {
        r.notes.push_back("This undo record was saved by a newer 6|7 World Studio.");
        return r;
    }
    UndoLog log;
    log.created = (*root)["created"].asString("").substr(0, 40);
    for (const Json& j : (*root)["moves"].items())
    {
        if (log.entries.size() >= kMaxMovesPerApply)
        {
            r.notes.push_back("Only the first " + std::to_string(kMaxMovesPerApply) + " moves were read.");
            break;
        }
        UndoEntry e{lower(j["item"].asString("")), j["name"].asString("").substr(0, 200),
                    lower(j["from"].asString("")), j["from_path"].asString("").substr(0, 1000),
                    lower(j["to"].asString(""))};
        if (!validId(e.itemId) || !validId(e.fromFolderId) || !validId(e.toFolderId))
        {
            r.notes.push_back("Left out an entry with a malformed ID.");
            continue;
        }
        log.entries.push_back(std::move(e));
    }
    r.log = std::move(log);
    return r;
}

std::size_t ApplyReport::count(MoveResult::Outcome outcome) const
{
    return static_cast<std::size_t>(
        std::count_if(results.begin(), results.end(), [&](const MoveResult& r) { return r.outcome == outcome; }));
}

std::string ApplyReport::describe(std::string_view verb) const
{
    const std::size_t moved = count(MoveResult::Outcome::Moved);
    std::string out = std::to_string(moved) + " item(s) " + std::string(verb) + ".";
    const std::size_t skipped = count(MoveResult::Outcome::Skipped);
    const std::size_t failed = count(MoveResult::Outcome::Failed);
    if (skipped) out += " " + std::to_string(skipped) + " skipped.";
    if (failed) out += " " + std::to_string(failed) + " failed.";
    for (const MoveResult& r : results)
    {
        if (r.outcome == MoveResult::Outcome::Moved) continue;
        out += "\n  - " + (r.itemName.empty() ? r.itemId : r.itemName) +
               (r.outcome == MoveResult::Outcome::Failed ? " (failed): " : " (skipped): ") + r.reason;
    }
    return out;
}

ApplyReport checkApplied(const std::vector<Move>& sent, const std::vector<Skipped>& notSent,
                         const std::map<std::string, std::string>& parentNow,
                         const std::map<std::string, std::string>& names)
{
    ApplyReport report;
    for (const Move& m : sent)
    {
        MoveResult r{m.itemId, m.itemName, MoveResult::Outcome::Moved, ""};
        const auto it = parentNow.find(m.itemId);
        if (it == parentNow.end())
        {
            r.outcome = MoveResult::Outcome::Failed;
            r.reason = "the item is no longer in your inventory";
        }
        else if (it->second != m.toFolderId)
        {
            r.outcome = MoveResult::Outcome::Failed;
            r.reason = "it is not in the new folder; the server may have refused the move";
        }
        report.results.push_back(std::move(r));
    }
    for (const Skipped& s : notSent)
    {
        const auto n = names.find(s.itemId);
        report.results.push_back({s.itemId, n == names.end() ? "" : n->second, MoveResult::Outcome::Skipped, s.reason});
    }
    return report;
}

UndoPlan planUndo(const UndoLog& log, const Snapshot& now)
{
    UndoPlan plan;
    std::map<std::string, const InvItem*> byId;
    for (const InvItem& i : now.items) byId[i.id] = &i;
    for (const UndoEntry& e : log.entries)
    {
        const auto it = byId.find(e.itemId);
        if (it == byId.end())
        {
            plan.skipped.push_back({e.itemId, "it is no longer in your inventory"});
            continue;
        }
        const InvItem& item = *it->second;
        if (item.parentId != e.toFolderId)
        {
            plan.skipped.push_back({e.itemId, "it was moved again since; it stays in " + now.pathOf(item.parentId)});
            continue;
        }
        if (!now.folder(e.fromFolderId))
        {
            plan.skipped.push_back({e.itemId, "its original folder (" + e.fromPath + ") no longer exists"});
            continue;
        }
        if (now.isProtected(e.fromFolderId))
        {
            plan.skipped.push_back({e.itemId, "its original folder is now one cleanup never changes"});
            continue;
        }
        plan.moves.push_back({e.itemId, item.name, e.toFolderId, e.fromFolderId, "back to " + e.fromPath});
    }
    return plan;
}

} // namespace sw67::inventory
