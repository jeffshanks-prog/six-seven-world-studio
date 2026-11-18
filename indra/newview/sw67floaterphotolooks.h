/**
 * @file sw67floaterphotolooks.h
 * @brief Photo Studio looks: local colour and lighting presets, custom
 *        looks, and an exact revert. Only this viewer's screen and
 *        snapshots change. Pair with Product Photography's batch export,
 *        which captures with whatever look is on.
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
#include "sw67/photo/looks.h"

#include <optional>
#include <string>
#include <vector>

class SW67FloaterPhotoLooks final : public LLFloater
{
public:
    AL_VIEW_TYPE(SW67FloaterPhotoLooks, LLFloater);

    SW67FloaterPhotoLooks(const LLSD& key);
    bool postBuild() override;
    void onOpen(const LLSD& key) override;

private:
    ~SW67FloaterPhotoLooks() override = default;

    void load();
    bool save();
    void refreshList();
    const sw67::photo::Look* selectedLook() const;
    void onSelect();
    void onApply();
    void onTurnOff();
    void onSaveCurrent();
    void onDelete();
    void refreshState();
    static sw67::photo::Values currentValues();
    static bool applySky(sw67::photo::Sky sky);
    void showStatus(sw67::a11y::StatusRole role, const std::string& text);
    static std::string filePath();

    std::vector<sw67::photo::Look> mCustom;
    std::optional<sw67::photo::LookState> mActive;
    std::vector<sw67::photo::Look> mShown;
};
