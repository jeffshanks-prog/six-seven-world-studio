/**
 * @file sw67floateroutfits.h
 * @brief Outfit Planner: outfits saved as plain files, a preview of exactly
 *        what wearing one puts on and takes off, the attachment and layer
 *        limits checked, and RLV asked before anything is worn or removed.
 *        Never deletes, buys or moves an inventory item.
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
#include "sw67/outfit/planner.h"

#include "lltimer.h"

#include <optional>
#include <set>
#include <string>
#include <vector>

class SW67FloaterOutfits final : public LLFloater
{
public:
    AL_VIEW_TYPE(SW67FloaterOutfits, LLFloater);

    SW67FloaterOutfits(const LLSD& key);
    bool postBuild() override;
    void onOpen(const LLSD& key) override;

    /// What is worn now, from the Current Outfit folder.
    static std::vector<sw67::outfit::Item> wornItems();
    /// The worn HUDs, which "keep my HUDs" never takes off.
    static std::set<std::string> wornHUDs(const std::vector<sw67::outfit::Item>& worn);

private:
    ~SW67FloaterOutfits() override = default;

    void refreshList();
    void refreshItems();
    void refreshPreview();
    void loadSelected();
    void editorToOutfit();
    sw67::outfit::Plan planNow() const;
    void onCapture();
    void onAddSelected();
    void onRemoveItem();
    void onSave();
    void onDelete();
    void onWear();
    void showStatus(sw67::a11y::StatusRole role, const std::string& text);
    static std::string outfitsDir();
    /// Outfit Doctor: after Wear, check every item until the viewer settles.
    void scheduleRestoreCheck(U32 attempt);
    void checkRestore(U32 attempt);
    static std::set<std::string> wornIds(const sw67::outfit::Plan& plan);

    sw67::outfit::Outfit mOutfit;
    std::optional<sw67::outfit::Plan> mPendingPlan;
    LLFrameTimer mCheckStarted;
    U32 mCheckAttempt = 0;
};
