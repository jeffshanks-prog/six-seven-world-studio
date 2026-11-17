/**
 * @file sw67floaterrecordingprivacy.cpp
 * @brief Recording Privacy Mode floater.
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

#include "sw67floaterrecordingprivacy.h"

#include "sw67accessibility.h"
#include "sw67viewerfiles.h"
#include "sw67viewersettings.h"

#include "llbutton.h"
#include "llfile.h"
#include "llformat.h"
#include "lltextbox.h"
#include "lluictrl.h"
#include "llviewercontrol.h"

using namespace sw67::privacy;
using sw67::a11y::StatusRole;

namespace
{
std::string checkName(Hide h)
{
    return "hide_" + std::string(hideKey(h));
}

std::string planText(const sw67::perf::ChangePlan& plan)
{
    std::string out;
    for (const sw67::perf::SettingChange& c : plan.changes)
    {
        out += "  - " + c.why + " (" + c.setting + ": " + c.from + " -> " + c.to + ")\n";
    }
    return out;
}

int applyChanges(const sw67::perf::ChangePlan& plan)
{
    int failed = 0;
    for (const sw67::perf::SettingChange& c : plan.changes)
    {
        if (!isManagedSetting(c.setting))
        {
            ++failed;
            continue;
        }
        const auto now = sw67viewer::readSetting(gSavedSettings, c.setting);
        if (!now || !sw67viewer::writeSetting(gSavedSettings, c.setting, {now->type, c.to}))
        {
            ++failed;
        }
    }
    return failed;
}

size_t hiddenCount(const State& s)
{
    size_t n = 0;
    for (int i = 0; i < static_cast<int>(Hide::Count); ++i)
        if (s.options.hides(static_cast<Hide>(i))) ++n;
    return n;
}
} // namespace

SW67FloaterRecordingPrivacy::SW67FloaterRecordingPrivacy(const LLSD& key)
:   LLFloater(key)
{
}

bool SW67FloaterRecordingPrivacy::postBuild()
{
    for (int i = 0; i < static_cast<int>(Hide::Count); ++i)
    {
        LLUICtrl* box = getChild<LLUICtrl>(checkName(static_cast<Hide>(i)));
        box->setValue(LLSD(true));
        box->setCommitCallback([this](LLUICtrl*, const LLSD&) { refresh(); });
    }
    getChild<LLUICtrl>("toggle_btn")->setCommitCallback([this](LLUICtrl*, const LLSD&) { onToggle(); });
    return LLFloater::postBuild();
}

void SW67FloaterRecordingPrivacy::onOpen(const LLSD& key)
{
    LLFloater::onOpen(key);
    if (const auto state = savedState())
    {
        for (int i = 0; i < static_cast<int>(Hide::Count); ++i)
        {
            const Hide h = static_cast<Hide>(i);
            getChild<LLUICtrl>(checkName(h))->setValue(LLSD(state->options.hides(h)));
        }
    }
    refresh();
}

std::string SW67FloaterRecordingPrivacy::statePath()
{
    return sw67viewer::joinPath(sw67viewer::userDir("privacy"), "state.json");
}

std::optional<State> SW67FloaterRecordingPrivacy::savedState()
{
    const auto text = sw67viewer::readFile(statePath(), 64 * 1024);
    auto state = text ? readState(*text) : std::nullopt;
    if (state && state->active)
    {
        return state;
    }
    return std::nullopt;
}

Values SW67FloaterRecordingPrivacy::currentValues()
{
    Values v;
    for (const std::string& name : managedSettings())
    {
        if (const auto value = sw67viewer::readSetting(gSavedSettings, name))
        {
            v[name] = value->value;
        }
    }
    return v;
}

Options SW67FloaterRecordingPrivacy::optionsFromControls() const
{
    Options o;
    for (int i = 0; i < static_cast<int>(Hide::Count); ++i)
    {
        const Hide h = static_cast<Hide>(i);
        o.hide[h] = getChild<LLUICtrl>(checkName(h))->getValue().asBoolean();
    }
    return o;
}

void SW67FloaterRecordingPrivacy::refresh()
{
    const auto state = savedState();
    const bool on = state.has_value();
    for (int i = 0; i < static_cast<int>(Hide::Count); ++i)
    {
        getChild<LLUICtrl>(checkName(static_cast<Hide>(i)))->setEnabled(!on);
    }
    getChild<LLButton>("toggle_btn")->setLabel(on ? std::string("Turn privacy mode off") : std::string("Turn privacy mode on"));
    std::string text;
    if (on)
    {
        const DisablePlan off = planDisable(*state, currentValues());
        text = "Turning it off would put back:\n" + planText(off.plan);
        if (!off.kept.empty())
        {
            text += "\nYou changed these yourself while it was on, so they stay as you set them:";
            for (const std::string& k : off.kept) text += " " + k;
            text += "\n";
        }
        showStatus(StatusRole::Live, statusLine(true, hiddenCount(*state)));
    }
    else
    {
        const sw67::perf::ChangePlan plan = planEnable(optionsFromControls(), currentValues());
        text = plan.empty() ? std::string("Everything chosen is already hidden.\n")
                            : "Turning it on would change:\n" + planText(plan);
        text += "\n" + plan.disclaimer;
        showStatus(StatusRole::Neutral, statusLine(false, 0));
    }
    getChild<LLUICtrl>("preview_text")->setValue(LLSD(text));
}

void SW67FloaterRecordingPrivacy::onToggle()
{
    if (const auto state = savedState())
    {
        const DisablePlan off = planDisable(*state, currentValues());
        const int failed = applyChanges(off.plan);
        if (failed == 0)
        {
            LLFile::remove(statePath());
        }
        refresh();
        showStatus(failed ? StatusRole::Warning : StatusRole::Success,
                   failed ? llformat("Privacy mode is still on: %d setting(s) could not be put back.", failed)
                          : std::string("Recording privacy mode is off. Your display settings are back as they were."));
        return;
    }
    const Options options = optionsFromControls();
    const sw67::perf::ChangePlan plan = planEnable(options, currentValues());
    // Save the state first, so it can be turned off after a crash too.
    if (!sw67viewer::writeFileAtomic(statePath(), writeState(stateFor(options, plan))))
    {
        showStatus(StatusRole::Error, "Could not save privacy mode's state, so nothing was changed.");
        return;
    }
    const int failed = applyChanges(plan);
    refresh();
    if (failed)
    {
        showStatus(StatusRole::Warning, llformat("Privacy mode is on, but %d setting(s) could not be changed.", failed));
    }
}

void SW67FloaterRecordingPrivacy::showStatus(StatusRole role, const std::string& text)
{
    LLTextBox* box = getChild<LLTextBox>("status_text");
    box->setText(sw67viewer::statusText(role) + "  " + text);
    box->setColor(LLUIColor(sw67viewer::statusColor(role)));
}
