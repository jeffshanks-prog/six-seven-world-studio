/**
 * @file sw67floaterinventorycleanup.cpp
 * @brief Inventory Cleanup Preview floater.
 *
 * Copyright (c) 2026, 6|7 World Studio contributors.
 *
 * The source code in this file is provided to you under the terms of the
 * GNU Lesser General Public License, version 2.1, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE. Terms of the LGPL can be found in doc/LGPL-license.txt
 * in this distribution, or online at http://www.gnu.org/licenses/lgpl-2.1.txt
 */

#include "llviewerprecompiledheaders.h"

#include "sw67floaterinventorycleanup.h"

#include "sw67accessibility.h"
#include "sw67rlvstate.h"
#include "sw67viewerfiles.h"

#include "llagent.h"
#include "llbutton.h"
#include "llcallbacklist.h"
#include "lldate.h"
#include "llfolderviewitem.h"
#include "llfolderviewmodelinventory.h"
#include "llformat.h"
#include "llinventoryfunctions.h"
#include "llinventorymodel.h"
#include "llinventorypanel.h"
#include "llscrolllistctrl.h"
#include "lltextbox.h"
#include "lluictrl.h"
#include "llviewerinventory.h"

#include <map>

using namespace sw67::inventory;
using sw67::a11y::StatusRole;

namespace
{
std::string folderTypeKey(LLFolderType::EType t)
{
    switch (t)
    {
    case LLFolderType::FT_TEXTURE: return "texture";
    case LLFolderType::FT_SOUND: return "sound";
    case LLFolderType::FT_CALLINGCARD: return "callingcard";
    case LLFolderType::FT_LANDMARK: return "landmark";
    case LLFolderType::FT_CLOTHING: return "clothing";
    case LLFolderType::FT_OBJECT: return "object";
    case LLFolderType::FT_NOTECARD: return "notecard";
    case LLFolderType::FT_ROOT_INVENTORY: return "root";
    case LLFolderType::FT_LSL_TEXT: return "script";
    case LLFolderType::FT_BODYPART: return "bodypart";
    case LLFolderType::FT_TRASH: return "trash";
    case LLFolderType::FT_SNAPSHOT_CATEGORY: return "snapshot";
    case LLFolderType::FT_LOST_AND_FOUND: return "lost_and_found";
    case LLFolderType::FT_ANIMATION: return "animation";
    case LLFolderType::FT_GESTURE: return "gesture";
    case LLFolderType::FT_FAVORITE: return "favorite";
    case LLFolderType::FT_CURRENT_OUTFIT: return "current_outfit";
    case LLFolderType::FT_OUTFIT: return "outfit";
    case LLFolderType::FT_MY_OUTFITS: return "my_outfits";
    case LLFolderType::FT_INBOX: return "inbox";
    case LLFolderType::FT_OUTBOX: return "outbox";
    case LLFolderType::FT_MARKETPLACE_LISTINGS: return "marketplace";
    case LLFolderType::FT_MARKETPLACE_STOCK: return "marketplace_stock";
    case LLFolderType::FT_MARKETPLACE_VERSION: return "marketplace_version";
    case LLFolderType::FT_SETTINGS: return "settings";
    case LLFolderType::FT_MATERIAL: return "material";
    case LLFolderType::FT_ANIM_OVERRIDES: return "anim_overrides";
    case LLFolderType::FT_RLV: return "rlv";
    default: return "";
    }
}

std::string itemTypeKey(LLAssetType::EType t)
{
    switch (t)
    {
    case LLAssetType::AT_TEXTURE: return "texture";
    case LLAssetType::AT_SOUND: return "sound";
    case LLAssetType::AT_CALLINGCARD: return "callingcard";
    case LLAssetType::AT_LANDMARK: return "landmark";
    case LLAssetType::AT_CLOTHING: return "clothing";
    case LLAssetType::AT_OBJECT: return "object";
    case LLAssetType::AT_NOTECARD: return "notecard";
    case LLAssetType::AT_LSL_TEXT: return "script";
    case LLAssetType::AT_BODYPART: return "bodypart";
    case LLAssetType::AT_ANIMATION: return "animation";
    case LLAssetType::AT_GESTURE: return "gesture";
    case LLAssetType::AT_SETTINGS: return "settings";
    case LLAssetType::AT_MATERIAL: return "material";
    default: return "other";
    }
}

Folder folderFrom(const LLViewerInventoryCategory* cat)
{
    return {cat->getUUID().asString(), cat->getName(), cat->getParentUUID().notNull() ? cat->getParentUUID().asString() : "",
            folderTypeKey(cat->getPreferredType())};
}

LLUUID toId(const std::string& id)
{
    LLUUID out;
    if (LLUUID::validate(id)) out.set(id, false);
    return out;
}

std::string moveLabel(const Snapshot& snap, const Move& m)
{
    return snap.pathOf(m.fromFolderId) + "  ->  " + snap.pathOf(m.toFolderId);
}
} // namespace

SW67FloaterInventoryCleanup::SW67FloaterInventoryCleanup(const LLSD& key)
:   LLFloater(key)
{
}

bool SW67FloaterInventoryCleanup::postBuild()
{
    getChild<LLUICtrl>("scan_btn")->setCommitCallback([this](LLUICtrl*, const LLSD&) { onScan(); });
    getChild<LLUICtrl>("use_folder_btn")->setCommitCallback([this](LLUICtrl*, const LLSD&) { onUseSelectedFolder(); });
    getChild<LLUICtrl>("move_list")->setCommitCallback([this](LLUICtrl*, const LLSD&) { onSelectionChanged(); });
    getChild<LLUICtrl>("select_all_btn")->setCommitCallback([this](LLUICtrl*, const LLSD&) {
        getChild<LLScrollListCtrl>("move_list")->selectAll();
        onSelectionChanged();
    });
    getChild<LLUICtrl>("preview_btn")->setCommitCallback([this](LLUICtrl*, const LLSD&) { onPreview(); });
    getChild<LLUICtrl>("apply_btn")->setCommitCallback([this](LLUICtrl*, const LLSD&) { onApply(); });
    getChild<LLUICtrl>("undo_btn")->setCommitCallback([this](LLUICtrl*, const LLSD&) { onUndo(); });
    getChild<LLUICtrl>("show_dup_btn")->setCommitCallback([this](LLUICtrl*, const LLSD&) { onShowDuplicate(); });
    getChild<LLUICtrl>("apply_btn")->setEnabled(false);
    return LLFloater::postBuild();
}

void SW67FloaterInventoryCleanup::onOpen(const LLSD& key)
{
    LLFloater::onOpen(key);
    if (mScanFolder.empty())
    {
        mScanFolder = gInventory.getRootFolderID().asString();
    }
    getChild<LLUICtrl>("scan_folder_text")->setValue(LLSD("Tidying: " + snapshotNow().pathOf(mScanFolder)));
}

std::string SW67FloaterInventoryCleanup::undoDir()
{
    return sw67viewer::userDir("inventory");
}

Snapshot SW67FloaterInventoryCleanup::snapshotNow()
{
    Snapshot snap;
    const LLUUID root = gInventory.getRootFolderID();
    snap.rootId = root.asString();
    if (const LLViewerInventoryCategory* r = gInventory.getCategory(root))
    {
        Folder f = folderFrom(r);
        f.parentId.clear();
        f.systemType = "root";
        snap.folders[f.id] = f;
    }
    LLInventoryModel::cat_array_t cats;
    LLInventoryModel::item_array_t items;
    gInventory.collectDescendents(root, cats, items, LLInventoryModel::EXCLUDE_TRASH);
    for (const LLPointer<LLViewerInventoryCategory>& cat : cats)
    {
        if (cat) snap.folders[cat->getUUID().asString()] = folderFrom(cat);
    }
    // The trash folder itself, so its contents (not collected) read as protected.
    const LLUUID trash = gInventory.findCategoryUUIDForType(LLFolderType::FT_TRASH);
    if (const LLViewerInventoryCategory* t = gInventory.getCategory(trash))
    {
        snap.folders[t->getUUID().asString()] = folderFrom(t);
    }
    const LLUUID self = gAgent.getID();
    for (const LLPointer<LLViewerInventoryItem>& item : items)
    {
        if (!item) continue;
        InvItem i;
        i.id = item->getUUID().asString();
        i.name = item->getName();
        i.type = itemTypeKey(item->getType());
        i.parentId = item->getParentUUID().asString();
        i.link = item->getIsLinkType();
        if (!i.link)
        {
            i.assetId = item->getAssetUUID().notNull() ? item->getAssetUUID().asString() : "";
            i.creatorId = item->getCreatorUUID().asString();
            i.copyable = item->getPermissions().allowCopyBy(self);
            i.worn = get_is_item_worn(item->getUUID());
        }
        snap.items.push_back(std::move(i));
    }
    return snap;
}

void SW67FloaterInventoryCleanup::onUseSelectedFolder()
{
    LLInventoryPanel* panel = LLInventoryPanel::getActiveInventoryPanel(false);
    if (!panel)
    {
        showStatus(StatusRole::Info, "Open your inventory and select a folder first.");
        return;
    }
    for (LLFolderViewItem* fv : panel->getSelectedItems())
    {
        const auto* model = fv ? static_cast<LLFolderViewModelItemInventory*>(fv->getViewModelItem()) : nullptr;
        if (!model) continue;
        if (gInventory.getCategory(model->getUUID()))
        {
            mScanFolder = model->getUUID().asString();
            const Snapshot snap = snapshotNow();
            getChild<LLUICtrl>("scan_folder_text")->setValue(LLSD("Tidying: " + snap.pathOf(mScanFolder)));
            showStatus(StatusRole::Info, snap.isProtected(mScanFolder)
                                             ? "That folder is one cleanup never changes; nothing in it will be moved."
                                             : "Folder chosen. Press Scan.");
            return;
        }
    }
    showStatus(StatusRole::Info, "Select a folder (not an item) in your inventory first.");
}

void SW67FloaterInventoryCleanup::onScan()
{
    mSnapshot = snapshotNow();
    Options options;
    options.scanFolders = {mScanFolder.empty() ? mSnapshot.rootId : mScanFolder};
    mPlan = planCleanup(mSnapshot, options);
    mPreviewed.clear();
    getChild<LLUICtrl>("apply_btn")->setEnabled(false);
    refreshLists();
    getChild<LLUICtrl>("preview_text")
        ->setValue(LLSD("Select the moves you want, then Preview selected.\n\n" + mPlan.describe(mSnapshot)));
    showStatus(StatusRole::Info, llformat("%d move(s) suggested and %d group(s) of likely duplicates to review. "
                                          "Nothing has changed yet.",
                                          static_cast<int>(mPlan.moves.size()), static_cast<int>(mPlan.duplicates.size())));
}

void SW67FloaterInventoryCleanup::refreshLists()
{
    LLScrollListCtrl* moves = getChild<LLScrollListCtrl>("move_list");
    moves->deleteAllItems();
    for (const Move& m : mPlan.moves)
    {
        LLSD row;
        row["value"] = m.itemId;
        row["columns"][0]["column"] = "item";
        row["columns"][0]["value"] = m.itemName;
        row["columns"][1]["column"] = "move";
        row["columns"][1]["value"] = moveLabel(mSnapshot, m);
        moves->addElement(row);
    }
    LLScrollListCtrl* dups = getChild<LLScrollListCtrl>("dup_list");
    dups->deleteAllItems();
    for (size_t n = 0; n < mPlan.duplicates.size(); ++n)
    {
        const DuplicateGroup& g = mPlan.duplicates[n];
        LLSD row;
        row["value"] = static_cast<S32>(n);
        row["columns"][0]["column"] = "name";
        row["columns"][0]["value"] = g.label;
        row["columns"][1]["column"] = "count";
        row["columns"][1]["value"] = llformat("%d", static_cast<int>(g.itemIds.size()));
        row["columns"][2]["column"] = "why";
        row["columns"][2]["value"] = g.confidence == DuplicateGroup::Confidence::SameAsset ? "same asset" : "same name";
        dups->addElement(row);
    }
}

std::set<std::string> SW67FloaterInventoryCleanup::selectedMoveIds() const
{
    std::set<std::string> ids;
    for (LLScrollListItem* item : getChild<LLScrollListCtrl>("move_list")->getAllSelected())
    {
        if (item) ids.insert(item->getValue().asString());
    }
    return ids;
}

void SW67FloaterInventoryCleanup::onSelectionChanged()
{
    // Changing the selection after a preview needs a new preview.
    if (selectedMoveIds() != mPreviewed)
    {
        getChild<LLUICtrl>("apply_btn")->setEnabled(false);
    }
}

void SW67FloaterInventoryCleanup::onPreview()
{
    mUndoPreviewed = false;
    const std::set<std::string> ids = selectedMoveIds();
    const std::vector<Move> moves = confirmedMoves(mPlan, ids);
    if (moves.empty())
    {
        showStatus(StatusRole::Info, "Select one or more suggested moves first.");
        return;
    }
    std::string text = llformat("Apply would make exactly these %d move(s), and nothing else:\n\n",
                                static_cast<int>(moves.size()));
    for (const Move& m : moves) text += "  - " + m.itemName + ": " + moveLabel(mSnapshot, m) + "\n";
    text += "\nEach original folder is recorded first, so Undo last cleanup can put them back. "
            "Nothing is deleted.";
    getChild<LLUICtrl>("preview_text")->setValue(LLSD(text));
    mPreviewed = ids;
    getChild<LLUICtrl>("apply_btn")->setEnabled(true);
    getChild<LLButton>("apply_btn")->setLabel(llformat("Apply %d move(s)", static_cast<int>(moves.size())));
    showStatus(StatusRole::Info, "Check the preview, then press Apply to confirm.");
}

void SW67FloaterInventoryCleanup::onApply()
{
    if (mUndoPreviewed)
    {
        mUndoPreviewed = false;
        getChild<LLUICtrl>("apply_btn")->setEnabled(false);
        send(mUndoPlan.moves, true);
        return;
    }
    if (mPreviewed.empty() || selectedMoveIds() != mPreviewed)
    {
        getChild<LLUICtrl>("apply_btn")->setEnabled(false);
        showStatus(StatusRole::Warning, "The selection changed since the preview. Preview it again first.");
        return;
    }
    const std::vector<Move> moves = confirmedMoves(mPlan, mPreviewed);
    mPreviewed.clear();
    getChild<LLUICtrl>("apply_btn")->setEnabled(false);

    // The undo record goes to disk before anything moves.
    const UndoLog log = undoLogFor(moves, mSnapshot, LLDate::now().toHTTPDateString(std::string("%Y-%m-%d %H:%M:%S")));
    const std::string path = sw67viewer::joinPath(undoDir(), "last-cleanup.sw67undo.json");
    if (!sw67viewer::writeFileAtomic(path, writeUndoLog(log)))
    {
        showStatus(StatusRole::Error, "Could not save the undo record, so nothing was moved: " + path);
        return;
    }
    send(moves, false);
}

void SW67FloaterInventoryCleanup::send(const std::vector<Move>& moves, bool undoing)
{
    SW67RlvState state;
    const sw67::rlv::Gate gate(state);
    const std::vector<sw67::rlv::Decision> decisions = gate.evaluateBatch(gateRequests(moves));
    mSent.clear();
    mNotSent.clear();
    mSentUndo = undoing;
    for (size_t n = 0; n < moves.size(); ++n)
    {
        const Move& m = moves[n];
        LLViewerInventoryItem* item = gInventory.getItem(toId(m.itemId));
        if (n < decisions.size() && !decisions[n].allowed())
        {
            mNotSent.push_back({m.itemId, decisions[n].explanation});
            continue;
        }
        if (!item || item->getParentUUID().asString() != m.fromFolderId)
        {
            mNotSent.push_back({m.itemId, "it was moved or removed since the scan"});
            continue;
        }
        if (!gInventory.getCategory(toId(m.toFolderId)))
        {
            mNotSent.push_back({m.itemId, "the destination folder no longer exists"});
            continue;
        }
        gInventory.changeItemParent(item, toId(m.toFolderId), false);
        mSent.push_back(m);
    }
    showStatus(StatusRole::Info, llformat("Sent %d move(s); %d skipped. Checking each item...",
                                          static_cast<int>(mSent.size()), static_cast<int>(mNotSent.size())));
    const U32 attempt = ++mAttempt;
    const LLHandle<LLFloater> handle = getHandle();
    doAfterInterval(
        [handle, attempt]() {
            SW67FloaterInventoryCleanup* self = ALViewType::as<SW67FloaterInventoryCleanup>(handle.get());
            if (self && self->mAttempt == attempt) self->checkSent(attempt);
        },
        4.f);
}

void SW67FloaterInventoryCleanup::checkSent(U32)
{
    std::map<std::string, std::string> parentNow;
    std::map<std::string, std::string> names;
    for (const Move& m : mSent)
    {
        if (const LLViewerInventoryItem* item = gInventory.getItem(toId(m.itemId)))
        {
            parentNow[m.itemId] = item->getParentUUID().asString();
        }
    }
    const Snapshot snap = snapshotNow();
    for (const InvItem& i : snap.items) names[i.id] = i.name;
    const ApplyReport report = checkApplied(mSent, mNotSent, parentNow, names);
    getChild<LLUICtrl>("preview_text")->setValue(LLSD(report.describe(mSentUndo ? "moved back" : "moved")));
    const bool clean = report.count(MoveResult::Outcome::Failed) == 0 && report.count(MoveResult::Outcome::Skipped) == 0;
    showStatus(clean ? StatusRole::Success : StatusRole::Warning,
               clean ? (mSentUndo ? "Undo finished: every item is back." : "Done. Undo last cleanup puts them back.")
                     : "Finished with some items not moved; each is listed with the reason.");
    mSent.clear();
    mNotSent.clear();
}

void SW67FloaterInventoryCleanup::onUndo()
{
    const std::string path = sw67viewer::joinPath(undoDir(), "last-cleanup.sw67undo.json");
    const auto text = sw67viewer::readFile(path, 2 * 1024 * 1024);
    const UndoReadResult r = text ? readUndoLog(*text) : UndoReadResult{};
    if (!r.log)
    {
        showStatus(StatusRole::Info, "There is no cleanup to undo.");
        return;
    }
    mSnapshot = snapshotNow();
    mUndoPlan = planUndo(*r.log, mSnapshot);
    std::string preview = llformat("Undo would move %d item(s) back to the folders they came from (cleanup of %s):\n\n",
                                   static_cast<int>(mUndoPlan.moves.size()), r.log->created.c_str());
    for (const Move& m : mUndoPlan.moves) preview += "  - " + m.itemName + ": " + moveLabel(mSnapshot, m) + "\n";
    if (!mUndoPlan.skipped.empty())
    {
        preview += "\nLeft where they are:\n";
        for (const Skipped& s : mUndoPlan.skipped) preview += "  - " + s.itemId + ": " + s.reason + "\n";
    }
    getChild<LLUICtrl>("preview_text")->setValue(LLSD(preview));
    mPreviewed.clear();
    mUndoPreviewed = !mUndoPlan.moves.empty();
    getChild<LLUICtrl>("apply_btn")->setEnabled(mUndoPreviewed);
    getChild<LLButton>("apply_btn")->setLabel(llformat("Undo %d move(s)", static_cast<int>(mUndoPlan.moves.size())));
    showStatus(StatusRole::Info, mUndoPreviewed ? "Check the undo preview, then press Apply to confirm."
                                                : "Nothing can be moved back; see the reasons above.");
}

void SW67FloaterInventoryCleanup::onShowDuplicate()
{
    LLScrollListItem* sel = getChild<LLScrollListCtrl>("dup_list")->getFirstSelected();
    if (!sel) return;
    const S32 n = sel->getValue().asInteger();
    if (n < 0 || static_cast<size_t>(n) >= mPlan.duplicates.size()) return;
    const DuplicateGroup& g = mPlan.duplicates[static_cast<size_t>(n)];
    std::string text = "Likely duplicates of \"" + g.label + "\", for you to review. Cleanup never deletes them:\n\n";
    std::map<std::string, const InvItem*> byId;
    for (const InvItem& i : mSnapshot.items) byId[i.id] = &i;
    for (const std::string& id : g.itemIds)
    {
        const auto it = byId.find(id);
        if (it == byId.end()) continue;
        text += "  - " + it->second->name + " in " + mSnapshot.pathOf(it->second->parentId) +
                (it->second->copyable ? "" : " (no copy)") + "\n";
    }
    getChild<LLUICtrl>("preview_text")->setValue(LLSD(text));
    if (!g.itemIds.empty())
    {
        LLInventoryPanel::openInventoryPanelAndSetSelection(true, toId(g.itemIds.front()), true, false, true);
    }
}

void SW67FloaterInventoryCleanup::showStatus(StatusRole role, const std::string& text)
{
    LLTextBox* box = getChild<LLTextBox>("status_text");
    box->setText(sw67viewer::statusText(role) + "  " + text);
    box->setColor(LLUIColor(sw67viewer::statusColor(role)));
}
