/**
 * @file sw67floaterbackup.cpp
 * @brief Settings Backup and Restore floater.
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

#include "sw67floaterbackup.h"

#include "sw67accessibility.h"
#include "sw67viewerfiles.h"
#include "sw67viewersettings.h"

#include "lldate.h"
#include "lldir.h"
#include "llfile.h"
#include "llfilepicker.h"
#include "llformat.h"
#include "lltextbox.h"
#include "lltoolbarview.h"
#include "lluictrl.h"
#include "llversioninfo.h"
#include "llviewercontrol.h"
#include "llviewerinput.h"
#include "llviewermenufile.h"

#include <algorithm>

using namespace sw67::backup;
using sw67::a11y::StatusRole;
using sw67::workspace::PrefValue;

namespace
{
// Our folders under <user settings>/sw67 that a backup carries.
const std::vector<std::string>& ourFolders()
{
    static const std::vector<std::string> f = {"workspaces", "performance", "outfits", "photoshoots", "events", "privacy"};
    return f;
}

std::string toolbarsPath()
{
    return gDirUtilp->getExpandedFilename(LL_PATH_PER_SL_ACCOUNT, "toolbars.xml");
}

std::string keyBindingsPath()
{
    return gDirUtilp->getExpandedFilename(LL_PATH_USER_SETTINGS, "key_bindings.xml");
}

/// Where a backup path lives on this computer: one of our folders and a
/// file name directly in it, or one of the two account files.
std::optional<std::string> localPathFor(const std::string& rel)
{
    if (!isBackupFilePath(rel))
    {
        return std::nullopt;
    }
    if (rel == "account/toolbars.xml")
    {
        return sw67viewer::accountSettingsReady() ? std::optional<std::string>(toolbarsPath()) : std::nullopt;
    }
    if (rel == "account/key_bindings.xml")
    {
        return keyBindingsPath();
    }
    const size_t slash = rel.find('/');
    const std::string folder = rel.substr(0, slash);
    const std::string name = rel.substr(slash + 1);
    if (name.find('/') != std::string::npos ||
        std::find(ourFolders().begin(), ourFolders().end(), folder) == ourFolders().end())
    {
        return std::nullopt;
    }
    return sw67viewer::joinPath(sw67viewer::userDir(folder), name);
}

std::vector<IncludedFile> collectFiles()
{
    std::vector<IncludedFile> files;
    for (const std::string& folder : ourFolders())
    {
        const std::string dir = sw67viewer::userDir(folder);
        for (const std::string& name : sw67viewer::listFiles(dir, ".json"))
        {
            if (const auto text = sw67viewer::readFile(sw67viewer::joinPath(dir, name), kMaxFileBytes))
            {
                files.push_back({folder + "/" + name, *text});
            }
        }
    }
    if (sw67viewer::accountSettingsReady())
    {
        if (const auto text = sw67viewer::readFile(toolbarsPath(), kMaxFileBytes))
        {
            files.push_back({"account/toolbars.xml", *text});
        }
    }
    if (const auto text = sw67viewer::readFile(keyBindingsPath(), kMaxFileBytes))
    {
        files.push_back({"account/key_bindings.xml", *text});
    }
    return files;
}

LLControlGroup* groupFor(Scope scope)
{
    if (scope == Scope::Account)
    {
        return sw67viewer::accountSettingsReady() ? &gSavedPerAccountSettings : nullptr;
    }
    return &gSavedSettings;
}

std::string nowText(const char* fmt)
{
    return LLDate::now().toHTTPDateString(std::string(fmt));
}

std::string undoPath()
{
    return sw67viewer::joinPath(sw67viewer::userDir("backups"), "before-restore.sw67backup.json");
}

std::string listText(const std::vector<std::string>& lines)
{
    std::string out;
    for (const std::string& l : lines) out += "  - " + l + "\n";
    return out;
}

const SettingEntry* findSetting(const Backup& b, Scope scope, const std::string& name)
{
    for (const SettingEntry& s : b.settings)
        if (s.scope == scope && s.name == name) return &s;
    return nullptr;
}
} // namespace

SW67FloaterBackup::SW67FloaterBackup(const LLSD& key)
:   LLFloater(key)
{
}

bool SW67FloaterBackup::postBuild()
{
    getChild<LLUICtrl>("create_btn")->setCommitCallback([this](LLUICtrl*, const LLSD&) { onCreate(); });
    getChild<LLUICtrl>("open_btn")->setCommitCallback([this](LLUICtrl*, const LLSD&) { onOpenBackup(); });
    getChild<LLUICtrl>("restore_btn")->setCommitCallback([this](LLUICtrl*, const LLSD&) { onRestore(); });
    getChild<LLUICtrl>("undo_btn")->setCommitCallback([this](LLUICtrl*, const LLSD&) { onUndo(); });
    for (const char* name : {"restore_settings", "restore_a11y", "restore_files", "replace_files"})
    {
        getChild<LLUICtrl>(name)->setCommitCallback([this](LLUICtrl*, const LLSD&) { refreshPreview(); });
    }
    getChild<LLUICtrl>("include_a11y")->setCommitCallback([this](LLUICtrl*, const LLSD&) { refreshSummary(); });
    return LLFloater::postBuild();
}

void SW67FloaterBackup::onOpen(const LLSD& key)
{
    LLFloater::onOpen(key);
    refreshSummary();
    refreshPreview();
}

void SW67FloaterBackup::refreshSummary()
{
    std::vector<SettingEntry> settings;
    for (auto& [name, value] : sw67viewer::changedSettings(gSavedSettings))
        settings.push_back({Scope::Global, name, value});
    if (sw67viewer::accountSettingsReady())
    {
        for (auto& [name, value] : sw67viewer::changedSettings(gSavedPerAccountSettings))
            settings.push_back({Scope::Account, name, value});
    }
    BackupOptions o;
    o.includeAccessibility = getChild<LLUICtrl>("include_a11y")->getValue().asBoolean();
    const BuildResult r = makeBackup(settings, collectFiles(), o);
    std::string text = llformat("A backup now would hold %d changed setting(s) and %d file(s), and leave out %d.",
                                static_cast<int>(r.backup.settings.size()), static_cast<int>(r.backup.files.size()),
                                static_cast<int>(r.excluded.size()));
    if (!sw67viewer::accountSettingsReady())
    {
        text += " Log in to include this account's settings and toolbars.";
    }
    getChild<LLUICtrl>("summary_text")->setValue(LLSD(text));
}

RestoreOptions SW67FloaterBackup::restoreOptions() const
{
    RestoreOptions o;
    o.settings = getChild<LLUICtrl>("restore_settings")->getValue().asBoolean();
    o.accessibility = getChild<LLUICtrl>("restore_a11y")->getValue().asBoolean();
    o.files = getChild<LLUICtrl>("restore_files")->getValue().asBoolean();
    o.replaceDifferentFiles = getChild<LLUICtrl>("replace_files")->getValue().asBoolean();
    return o;
}

CurrentSettings SW67FloaterBackup::currentFor(const Backup& backup) const
{
    CurrentSettings current;
    for (const SettingEntry& s : backup.settings)
    {
        if (LLControlGroup* group = groupFor(s.scope))
        {
            if (auto v = sw67viewer::readSetting(*group, s.name))
            {
                current[{s.scope, s.name}] = *v;
            }
        }
    }
    return current;
}

std::map<std::string, std::string> SW67FloaterBackup::existingFilesFor(const Backup& backup) const
{
    std::map<std::string, std::string> existing;
    for (const IncludedFile& f : backup.files)
    {
        if (const auto path = localPathFor(f.path))
        {
            if (const auto text = sw67viewer::readFile(*path, kMaxFileBytes * 2))
            {
                existing[f.path] = *text;
            }
        }
    }
    return existing;
}

void SW67FloaterBackup::refreshPreview()
{
    getChild<LLUICtrl>("undo_btn")->setEnabled(sw67viewer::readFile(undoPath(), 4 * 1024 * 1024).has_value());
    if (!mLoaded)
    {
        getChild<LLUICtrl>("preview_text")->setValue(LLSD("Open a backup file to see what restoring it would change."));
        getChild<LLUICtrl>("restore_btn")->setEnabled(false);
        return;
    }
    const RestorePlan plan = planRestore(*mLoaded, currentFor(*mLoaded), existingFilesFor(*mLoaded), restoreOptions());
    std::string text = "Backup: " + mLoadedPath + "\n";
    if (!mLoaded->created.empty()) text += "Made " + mLoaded->created + " UTC";
    if (!mLoaded->note.empty()) text += " - " + mLoaded->note;
    text += llformat("\nRestoring would make %d change(s):\n\n", static_cast<int>(plan.changeCount()));
    text += plan.describe();
    if (!sw67viewer::accountSettingsReady())
    {
        text += "\nLog in first to restore this account's settings and toolbars.";
    }
    getChild<LLUICtrl>("preview_text")->setValue(LLSD(text));
    getChild<LLUICtrl>("restore_btn")->setEnabled(plan.changeCount() > 0);
}

void SW67FloaterBackup::onCreate()
{
    std::vector<SettingEntry> settings;
    for (auto& [name, value] : sw67viewer::changedSettings(gSavedSettings))
        settings.push_back({Scope::Global, name, value});
    if (sw67viewer::accountSettingsReady())
    {
        for (auto& [name, value] : sw67viewer::changedSettings(gSavedPerAccountSettings))
            settings.push_back({Scope::Account, name, value});
    }
    BackupOptions o;
    o.includeAccessibility = getChild<LLUICtrl>("include_a11y")->getValue().asBoolean();
    o.created = nowText("%Y-%m-%d %H:%M");
    o.viewerVersion = LLVersionInfo::instance().getVersion();
    o.note = getChild<LLUICtrl>("backup_note")->getValue().asString();
    const BuildResult r = makeBackup(settings, collectFiles(), o);
    const std::string text = writeBackup(r.backup);
    const size_t settingCount = r.backup.settings.size();
    const size_t fileCount = r.backup.files.size();
    const size_t leftOut = r.excluded.size();
    const LLHandle<LLFloater> handle = getHandle();
    LLFilePickerReplyThread::startPicker(
        [handle, text, settingCount, fileCount, leftOut](const std::vector<std::string>& files, LLFilePicker::ELoadFilter,
                                                         LLFilePicker::ESaveFilter) {
            auto* self = static_cast<SW67FloaterBackup*>(handle.get());
            if (files.empty() || !self)
            {
                return;
            }
            if (!sw67viewer::writeFileAtomic(files.front(), text))
            {
                self->showStatus(StatusRole::Error, "Could not write " + files.front() + ".");
                return;
            }
            self->showStatus(StatusRole::Success,
                             llformat("Saved %d setting(s) and %d file(s) to ", static_cast<int>(settingCount),
                                      static_cast<int>(fileCount)) +
                                 files.front() +
                                 llformat(". %d were left out: passwords, keys, stream credentials, consent opt-ins "
                                          "and this computer's own folders never go in a backup.",
                                          static_cast<int>(leftOut)));
        },
        LLFilePicker::FFSAVE_ALL, "6-7 settings backup " + nowText("%Y-%m-%d") + ".sw67backup.json");
}

void SW67FloaterBackup::onOpenBackup()
{
    const LLHandle<LLFloater> handle = getHandle();
    LLFilePickerReplyThread::startPicker(
        [handle](const std::vector<std::string>& files, LLFilePicker::ELoadFilter, LLFilePicker::ESaveFilter) {
            auto* self = static_cast<SW67FloaterBackup*>(handle.get());
            if (files.empty() || !self)
            {
                return;
            }
            const auto text = sw67viewer::readFile(files.front(), 4 * 1024 * 1024);
            if (!text)
            {
                self->showStatus(StatusRole::Error, "Could not read that file, or it is over 4 MB.");
                return;
            }
            ReadResult r = readBackup(*text);
            if (!r.backup)
            {
                self->showStatus(StatusRole::Error, listText(r.notes));
                return;
            }
            self->mLoaded = std::move(r.backup);
            self->mLoadedPath = files.front();
            self->refreshPreview();
            self->showStatus(r.notes.empty() ? StatusRole::Info : StatusRole::Warning,
                             "Opened. Nothing has changed yet: check the preview, then Restore." +
                                 (r.notes.empty() ? std::string() : "\n" + listText(r.notes)));
        },
        LLFilePicker::FFLOAD_ALL, false);
}

void SW67FloaterBackup::onRestore()
{
    if (!mLoaded)
    {
        return;
    }
    const CurrentSettings current = currentFor(*mLoaded);
    const RestorePlan plan = planRestore(*mLoaded, current, existingFilesFor(*mLoaded), restoreOptions());
    if (plan.changeCount() == 0)
    {
        showStatus(StatusRole::Info, "Nothing to restore: everything already matches.");
        return;
    }
    // Save what to undo first; without it, change nothing.
    const Backup undo = undoPointFor(plan, current, nowText("%Y-%m-%d %H:%M"));
    if (!undo.settings.empty() && !sw67viewer::writeFileAtomic(undoPath(), writeBackup(undo)))
    {
        showStatus(StatusRole::Error, "Could not save an undo point, so nothing was restored.");
        return;
    }
    int done = 0;
    int failed = 0;
    bool toolbars = false;
    bool keys = false;
    for (const RestoreStep& step : plan.steps)
    {
        if (step.what == RestoreStep::What::SetSetting)
        {
            const SettingEntry* s = findSetting(*mLoaded, step.scope, step.target);
            LLControlGroup* group = groupFor(step.scope);
            if (s && group && sw67viewer::writeSetting(*group, s->name, s->value))
                ++done;
            else
                ++failed;
        }
        else if (step.what == RestoreStep::What::WriteFile)
        {
            const auto path = localPathFor(step.target);
            const auto it = std::find_if(mLoaded->files.begin(), mLoaded->files.end(),
                                         [&](const IncludedFile& f) { return f.path == step.target; });
            if (!path || it == mLoaded->files.end())
            {
                ++failed;
                continue;
            }
            // A file being replaced is kept beside it, never deleted.
            if (const auto old = sw67viewer::readFile(*path, kMaxFileBytes * 2))
            {
                if (!sw67viewer::writeFileAtomic(*path + ".before-restore", *old))
                {
                    ++failed;
                    continue;
                }
            }
            if (sw67viewer::writeFileAtomic(*path, it->content))
            {
                ++done;
                toolbars |= step.target == "account/toolbars.xml";
                keys |= step.target == "account/key_bindings.xml";
            }
            else
            {
                ++failed;
            }
        }
    }
    if (toolbars && gToolBarView)
    {
        gToolBarView->loadToolbars(false);
    }
    if (keys)
    {
        gViewerInput.loadBindingsXML(keyBindingsPath());
    }
    refreshPreview();
    refreshSummary();
    showStatus(failed ? StatusRole::Warning : StatusRole::Success,
               llformat("Restored %d change(s)", done) + (failed ? llformat("; %d could not be made", failed) : "") +
                   ". Undo restore puts the settings back as they were; replaced files are kept as .before-restore. "
                   "Some settings take effect after a restart.");
}

void SW67FloaterBackup::onUndo()
{
    const auto text = sw67viewer::readFile(undoPath(), 4 * 1024 * 1024);
    const ReadResult r = text ? readBackup(*text) : ReadResult{};
    if (!r.backup)
    {
        showStatus(StatusRole::Warning, "There is no restore to undo.");
        return;
    }
    RestoreOptions o;
    o.files = false;
    const RestorePlan plan = planRestore(*r.backup, currentFor(*r.backup), {}, o);
    int done = 0;
    int failed = 0;
    for (const RestoreStep& step : plan.steps)
    {
        if (step.what != RestoreStep::What::SetSetting) continue;
        const SettingEntry* s = findSetting(*r.backup, step.scope, step.target);
        LLControlGroup* group = groupFor(step.scope);
        if (s && group && sw67viewer::writeSetting(*group, s->name, s->value))
            ++done;
        else
            ++failed;
    }
    if (failed == 0)
    {
        LLFile::remove(undoPath());
    }
    refreshPreview();
    refreshSummary();
    showStatus(failed ? StatusRole::Warning : StatusRole::Success,
               llformat("Put back %d setting(s) as they were before the restore", done) +
                   (failed ? llformat("; %d could not be changed, so the undo point is kept.", failed)
                           : ". Files the restore added are left in place."));
}

void SW67FloaterBackup::showStatus(StatusRole role, const std::string& text)
{
    LLTextBox* box = getChild<LLTextBox>("status_text");
    box->setText(sw67viewer::statusText(role) + "  " + text);
    box->setColor(LLUIColor(sw67viewer::statusColor(role)));
}
