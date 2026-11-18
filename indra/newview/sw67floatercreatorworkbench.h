/**
 * @file sw67floatercreatorworkbench.h
 * @brief Creator Workbench: checks a mesh file against Second Life's limits
 *        before handing it to the viewer's own model uploader, and drafts
 *        LSL through the 6|7 gateway when one is set up. A draft is text:
 *        this window never compiles, uploads or runs it, and copy and save
 *        stay off until the creator ticks that they reviewed it.
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
#include "sw67/creator/mesh_check.h"

#include <string>

class SW67FloaterCreatorWorkbench final : public LLFloater
{
public:
    AL_VIEW_TYPE(SW67FloaterCreatorWorkbench, LLFloater);

    SW67FloaterCreatorWorkbench(const LLSD& key);
    bool postBuild() override;
    void onOpen(const LLSD& key) override;

private:
    ~SW67FloaterCreatorWorkbench() override = default;

    void onPickMesh();
    void checkFile(const std::string& path);
    void onOpenUploader();
    void onDraft();
    void onReviewed();
    void onCopy();
    void onSaveLsl();
    void refreshButtons();
    void showStatus(sw67::a11y::StatusRole role, const std::string& text);

    std::string mMeshPath;
    bool mMeshOk = false;
    bool mDrafting = false;
    bool mHaveDraft = false;
};
