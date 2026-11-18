/**
 * @file sw67floaterinventorycleanup.h
 * @brief Inventory Cleanup Preview: suggested folder moves and likely
 *        duplicates. Only the moves the user selects, previews and confirms
 *        are made, through the viewer's own inventory move; each is recorded
 *        first with its original folder so it can be undone. This window
 *        has no delete: duplicates are listed for review only.
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

#include "llfloater.h"

#include "sw67/a11y/profile.h"
#include "sw67/inventory/cleanup.h"

#include <set>
#include <string>
#include <vector>

class SW67FloaterInventoryCleanup final : public LLFloater
{
public:
    AL_VIEW_TYPE(SW67FloaterInventoryCleanup, LLFloater);

    SW67FloaterInventoryCleanup(const LLSD& key);
    bool postBuild() override;
    void onOpen(const LLSD& key) override;

    /// The user's inventory as sw67core sees it (My Inventory and the trash
    /// folder itself; the Library is left out).
    static sw67::inventory::Snapshot snapshotNow();

private:
    ~SW67FloaterInventoryCleanup() override = default;

    void onScan();
    void onUseSelectedFolder();
    void onSelectionChanged();
    void onPreview();
    void onApply();
    void onUndo();
    void onShowDuplicate();
    void refreshLists();
    std::set<std::string> selectedMoveIds() const;
    /// Sends the moves (after RLV) and checks them a few seconds later.
    void send(const std::vector<sw67::inventory::Move>& moves, bool undoing);
    void checkSent(U32 attempt);
    void showStatus(sw67::a11y::StatusRole role, const std::string& text);
    static std::string undoDir();

    std::string mScanFolder;
    sw67::inventory::Snapshot mSnapshot;
    sw67::inventory::CleanupPlan mPlan;
    std::set<std::string> mPreviewed;   ///< The selection the last preview showed.
    sw67::inventory::UndoPlan mUndoPlan;
    bool mUndoPreviewed = false;
    std::vector<sw67::inventory::Move> mSent;
    std::vector<sw67::inventory::Skipped> mNotSent;
    bool mSentUndo = false;
    U32 mAttempt = 0;
};
