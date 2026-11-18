/**
 * @file sw67floaterphotolooks.cpp
 * @brief Photo Studio looks floater.
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

#include "sw67floaterphotolooks.h"

#include "sw67accessibility.h"
#include "sw67rlvstate.h"
#include "sw67viewerfiles.h"
#include "sw67viewersettings.h"

#include "llenvironment.h"
#include "llformat.h"
#include "llscrolllistctrl.h"
#include "lltextbox.h"
#include "lluictrl.h"
#include "llviewercontrol.h"

#include <algorithm>

using namespace sw67::photo;
using sw67::a11y::StatusRole;

namespace
{
std::string describePlan(const sw67::perf::ChangePlan& plan)
{
    std::string out = plan.title + "\n\n";
    if (plan.changes.empty()) out += "Nothing would change: this look is already on.\n";
    for (const auto& c : plan.changes)
    {
        out += "  - " + c.setting + ": " + (c.from.empty() ? std::string("(region's)") : c.from) + " -> " + c.to + "\n";
    }
    return out + "\n" + plan.disclaimer;
}
} // namespace

SW67FloaterPhotoLooks::SW67FloaterPhotoLooks(const LLSD& key)
:   LLFloater(key)
{
}

bool SW67FloaterPhotoLooks::postBuild()
{
    getChild<LLUICtrl>("look_list")->setCommitCallback([this](LLUICtrl*, const LLSD&) { onSelect(); });
    getChild<LLUICtrl>("apply_btn")->setCommitCallback([this](LLUICtrl*, const LLSD&) { onApply(); });
    getChild<LLUICtrl>("off_btn")->setCommitCallback([this](LLUICtrl*, const LLSD&) { onTurnOff(); });
    getChild<LLUICtrl>("save_current_btn")->setCommitCallback([this](LLUICtrl*, const LLSD&) { onSaveCurrent(); });
    getChild<LLUICtrl>("delete_btn")->setCommitCallback([this](LLUICtrl*, const LLSD&) { onDelete(); });
    return LLFloater::postBuild();
}

void SW67FloaterPhotoLooks::onOpen(const LLSD& key)
{
    LLFloater::onOpen(key);
    load();
    refreshList();
    refreshState();
}

std::string SW67FloaterPhotoLooks::filePath()
{
    return sw67viewer::joinPath(sw67viewer::userDir("photo"), "looks.sw67looks.json");
}

void SW67FloaterPhotoLooks::load()
{
    const auto text = sw67viewer::readFile(filePath(), 1024 * 1024);
    if (!text) return;
    LooksFile f = readLooks(*text);
    mCustom = std::move(f.custom);
    mActive = std::move(f.active);
}

bool SW67FloaterPhotoLooks::save()
{
    return sw67viewer::writeFileAtomic(filePath(), writeLooks(mCustom, mActive));
}

Values SW67FloaterPhotoLooks::currentValues()
{
    Values v;
    for (const auto& [name, range] : lookSettings())
    {
        if (const auto value = sw67viewer::readSetting(gSavedSettings, name)) v[name] = value->value;
    }
    return v;
}

void SW67FloaterPhotoLooks::refreshList()
{
    mShown = builtinLooks();
    mShown.insert(mShown.end(), mCustom.begin(), mCustom.end());
    LLScrollListCtrl* list = getChild<LLScrollListCtrl>("look_list");
    list->deleteAllItems();
    for (size_t i = 0; i < mShown.size(); ++i)
    {
        const Look& l = mShown[i];
        LLSD row;
        row["value"] = static_cast<S32>(i);
        row["columns"][0]["column"] = "name";
        row["columns"][0]["value"] = l.name + (mActive && mActive->key == l.key ? "  (on)" : "");
        row["columns"][1]["column"] = "sky";
        row["columns"][1]["value"] = std::string(skyKey(l.sky));
        row["columns"][2]["column"] = "kind";
        row["columns"][2]["value"] = l.builtin ? "built-in" : "yours";
        list->addElement(row);
    }
}

const Look* SW67FloaterPhotoLooks::selectedLook() const
{
    LLScrollListItem* sel = getChild<LLScrollListCtrl>("look_list")->getFirstSelected();
    if (!sel) return nullptr;
    const S32 n = sel->getValue().asInteger();
    return n >= 0 && static_cast<size_t>(n) < mShown.size() ? &mShown[static_cast<size_t>(n)] : nullptr;
}

void SW67FloaterPhotoLooks::onSelect()
{
    if (const Look* l = selectedLook())
    {
        getChild<LLUICtrl>("preview_text")
            ->setValue(LLSD(l->description + (l->description.empty() ? "" : "\n\n") +
                            describePlan(planLook(*l, currentValues()))));
    }
}

bool SW67FloaterPhotoLooks::applySky(Sky sky)
{
    LLEnvironment& env = LLEnvironment::instance();
    switch (sky)
    {
    case Sky::Keep: return true;
    case Sky::Sunrise: env.setEnvironment(LLEnvironment::ENV_LOCAL, LLEnvironment::KNOWN_SKY_SUNRISE, LLEnvironment::TRANSITION_INSTANT); break;
    case Sky::Midday: env.setEnvironment(LLEnvironment::ENV_LOCAL, LLEnvironment::KNOWN_SKY_MIDDAY, LLEnvironment::TRANSITION_INSTANT); break;
    case Sky::Sunset: env.setEnvironment(LLEnvironment::ENV_LOCAL, LLEnvironment::KNOWN_SKY_SUNSET, LLEnvironment::TRANSITION_INSTANT); break;
    case Sky::Midnight: env.setEnvironment(LLEnvironment::ENV_LOCAL, LLEnvironment::KNOWN_SKY_MIDNIGHT, LLEnvironment::TRANSITION_INSTANT); break;
    }
    env.setSelectedEnvironment(LLEnvironment::ENV_LOCAL, LLEnvironment::TRANSITION_INSTANT);
    return true;
}

void SW67FloaterPhotoLooks::onApply()
{
    const Look* l = selectedLook();
    if (!l)
    {
        showStatus(StatusRole::Info, "Choose a look first.");
        return;
    }
    if (l->sky != Sky::Keep)
    {
        sw67::rlv::ActionRequest request;
        request.kind = sw67::rlv::ActionKind::ChangeEnvironment;
        const sw67::rlv::Decision d = SW67RlvState::check(request);
        if (!d.allowed())
        {
            showStatus(StatusRole::Restricted, "Nothing was changed: " + d.explanation);
            return;
        }
    }
    const Look look = *l;
    // Turning one look on over another goes back to the original values first.
    Values current = currentValues();
    if (mActive)
    {
        const RevertPlan r = planRevert(*mActive, current);
        for (const auto& c : r.plan.changes)
        {
            if (const auto now = sw67viewer::readSetting(gSavedSettings, c.setting))
                sw67viewer::writeSetting(gSavedSettings, c.setting, {now->type, c.to});
        }
        current = currentValues();
    }
    // The state is saved before anything changes, so it can be turned off after a crash.
    mActive = stateFor(look, current);
    if (!save())
    {
        showStatus(StatusRole::Error, "Could not save the look's state, so nothing was changed.");
        mActive.reset();
        return;
    }
    int failed = 0;
    for (const auto& c : planLook(look, current).changes)
    {
        if (c.setting == "Local sky") continue;
        const auto now = sw67viewer::readSetting(gSavedSettings, c.setting);
        if (!now || !sw67viewer::writeSetting(gSavedSettings, c.setting, {now->type, c.to})) ++failed;
    }
    applySky(look.sky);
    refreshList();
    refreshState();
    showStatus(failed ? StatusRole::Warning : StatusRole::Success,
               "\"" + look.name + "\" is on." + (failed ? llformat(" %d setting(s) could not be set.", failed) : "") +
                   " Product Photography's batch captures with it.");
}

void SW67FloaterPhotoLooks::onTurnOff()
{
    if (!mActive)
    {
        showStatus(StatusRole::Info, "No look is on.");
        return;
    }
    const RevertPlan r = planRevert(*mActive, currentValues());
    for (const auto& c : r.plan.changes)
    {
        if (const auto now = sw67viewer::readSetting(gSavedSettings, c.setting))
            sw67viewer::writeSetting(gSavedSettings, c.setting, {now->type, c.to});
    }
    if (r.restoreSky)
    {
        LLEnvironment::instance().clearEnvironment(LLEnvironment::ENV_LOCAL);
        LLEnvironment::instance().setSelectedEnvironment(LLEnvironment::ENV_LOCAL, LLEnvironment::TRANSITION_INSTANT);
    }
    const std::string name = mActive->name;
    mActive.reset();
    save();
    refreshList();
    refreshState();
    std::string kept;
    for (const std::string& k : r.kept) kept += (kept.empty() ? "" : ", ") + k;
    showStatus(StatusRole::Success, "\"" + name + "\" is off; your settings are back." +
                                        (kept.empty() ? "" : " Kept as you changed them: " + kept + "."));
}

void SW67FloaterPhotoLooks::onSaveCurrent()
{
    const std::string name = getChild<LLUICtrl>("look_name")->getValue().asString();
    if (name.empty())
    {
        showStatus(StatusRole::Warning, "Name the look first.");
        return;
    }
    const auto sky = parseSky(getChild<LLUICtrl>("sky_combo")->getValue().asString()).value_or(Sky::Keep);
    Look l = captureLook(name, currentValues(), sky);
    l.description = "Saved from your Lightbox settings.";
    mCustom.erase(std::remove_if(mCustom.begin(), mCustom.end(), [&](const Look& x) { return x.key == l.key; }),
                  mCustom.end());
    if (mCustom.size() >= kMaxCustomLooks) return;
    mCustom.push_back(std::move(l));
    if (!save())
    {
        showStatus(StatusRole::Error, "Could not save " + filePath() + ".");
        return;
    }
    refreshList();
    showStatus(StatusRole::Success, "Saved \"" + name + "\" from your current colour and lens settings.");
}

void SW67FloaterPhotoLooks::onDelete()
{
    const Look* l = selectedLook();
    if (!l || l->builtin)
    {
        showStatus(StatusRole::Info, "Choose one of your own looks to remove. Built-in looks stay.");
        return;
    }
    const std::string key = l->key;
    mCustom.erase(std::remove_if(mCustom.begin(), mCustom.end(), [&](const Look& x) { return x.key == key; }),
                  mCustom.end());
    save();
    refreshList();
    showStatus(StatusRole::Success, "Removed that look. Your settings did not change.");
}

void SW67FloaterPhotoLooks::refreshState()
{
    getChild<LLUICtrl>("state_text")
        ->setValue(LLSD(mActive ? "On now: " + mActive->name + ". Turn off puts your settings back."
                                : std::string("No look is on: you see your own settings.")));
    getChild<LLUICtrl>("off_btn")->setEnabled(mActive.has_value());
}

void SW67FloaterPhotoLooks::showStatus(StatusRole role, const std::string& text)
{
    LLTextBox* box = getChild<LLTextBox>("status_text");
    box->setText(sw67viewer::statusText(role) + "  " + text);
    box->setColor(LLUIColor(sw67viewer::statusColor(role)));
}
