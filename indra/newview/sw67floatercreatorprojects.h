/**
 * @file sw67floatercreatorprojects.h
 * @brief Creator Project Manager: open or start a project folder, list its
 *        files and inventory links with what the project actually holds,
 *        record revisions and testing notes, tick off the release checklist
 *        and see what stands between the project and a release.
 *
 *        Projects are plain files in a folder the user picks. Opening and
 *        editing them needs no subscription.
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
#include "sw67/creator/project.h"

#include <optional>
#include <string>

class SW67FloaterCreatorProjects final : public LLFloater
{
public:
    AL_VIEW_TYPE(SW67FloaterCreatorProjects, LLFloater);

    SW67FloaterCreatorProjects(const LLSD& key);
    bool postBuild() override;

private:
    ~SW67FloaterCreatorProjects() override = default;

    void onBrowse();
    void openFolder(const std::string& folder);
    void onScan();
    void onRemoveAsset();
    void onToggleChecklist();
    void onAddRevision();
    void onSave();
    void refresh();
    void markDirty();
    bool fileExists(const std::string& relative) const;
    void showStatus(sw67::a11y::StatusRole role, const std::string& text);

    std::string mFolder;
    std::optional<sw67::creator::CreatorProject> mProject;
    bool mDirty = false;
};
