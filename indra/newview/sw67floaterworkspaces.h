/**
 * @file sw67floaterworkspaces.h
 * @brief Workspace Profiles: pick Everyday, Creator, Photographer, DJ, Host
 *        or a saved layout, preview exactly what applying it changes, apply,
 *        undo, save the current layout, and export or import profiles.
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
#include "sw67/workspace/profile.h"

#include <optional>
#include <vector>

class SW67FloaterWorkspaces final : public LLFloater
{
public:
    AL_VIEW_TYPE(SW67FloaterWorkspaces, LLFloater);

    SW67FloaterWorkspaces(const LLSD& key);
    bool postBuild() override;
    void onOpen(const LLSD& key) override;

private:
    ~SW67FloaterWorkspaces() override = default;

    void refreshList();
    const sw67::workspace::WorkspaceProfile* selected() const;
    void refreshPreview();
    void onApply();
    void onUndo();
    void onSaveCurrent();
    void onDelete();
    void onExport();
    void onImport();
    void showStatus(sw67::a11y::StatusRole role, const std::string& text);

    std::vector<sw67::workspace::WorkspaceProfile> mProfiles; ///< Built-ins first, then saved.
    size_t mBuiltinCount = 0;
    std::optional<sw67::workspace::WorkspaceProfile> mUndo;
};
