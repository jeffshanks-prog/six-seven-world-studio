/**
 * @file sw67floaterbackup.h
 * @brief Settings Backup and Restore: saves the user's own settings and 6|7
 *        files to one file they choose, and restores one after previewing
 *        every change. Passwords, keys and stream credentials never go in;
 *        consent opt-ins such as RLV are never switched on by a restore.
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
#include "sw67/backup/backup.h"

#include <optional>
#include <string>

class SW67FloaterBackup final : public LLFloater
{
public:
    AL_VIEW_TYPE(SW67FloaterBackup, LLFloater);

    SW67FloaterBackup(const LLSD& key);
    bool postBuild() override;
    void onOpen(const LLSD& key) override;

private:
    ~SW67FloaterBackup() override = default;

    sw67::backup::RestoreOptions restoreOptions() const;
    sw67::backup::CurrentSettings currentFor(const sw67::backup::Backup& backup) const;
    std::map<std::string, std::string> existingFilesFor(const sw67::backup::Backup& backup) const;
    void refreshSummary();
    void refreshPreview();
    void onCreate();
    void onOpenBackup();
    void onRestore();
    void onUndo();
    void showStatus(sw67::a11y::StatusRole role, const std::string& text);

    std::optional<sw67::backup::Backup> mLoaded;
    std::string mLoadedPath;
};
