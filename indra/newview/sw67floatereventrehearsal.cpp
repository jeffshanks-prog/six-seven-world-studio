/**
 * @file sw67floatereventrehearsal.cpp
 * @brief Event Rehearsal Mode floater. Everything here edits a plan or
 *        shows descriptions of what it would do; nothing acts on the world.
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

#include "sw67floatereventrehearsal.h"

#include "sw67accessibility.h"
#include "sw67rlvstate.h"
#include "sw67viewerfiles.h"

#include "sw67/photo/product.h"

#include "llavatarname.h"
#include "llcombobox.h"
#include "llfloateravatarpicker.h"
#include "llfolderviewitem.h"
#include "llfolderviewmodelinventory.h"
#include "llformat.h"
#include "llinventorymodel.h"
#include "llinventorypanel.h"
#include "llscrolllistctrl.h"
#include "lltextbox.h"
#include "lluictrl.h"
#include "llviewerinventory.h"

#include <algorithm>
#include <cstdlib>

using namespace sw67::event;
using sw67::a11y::StatusRole;

namespace
{
int intFrom(const LLUICtrl* ctrl, int fallback)
{
    const std::string s = ctrl->getValue().asString();
    if (s.empty()) return fallback;
    char* end = nullptr;
    const long n = std::strtol(s.c_str(), &end, 10);
    if (!end || *end != '\0') return fallback;
    return static_cast<int>(llclamp(n, -2147483647L, 2147483647L));
}

std::string cueSummary(const Cue& c)
{
    std::string what = c.label;
    if (what.empty()) what = c.text;
    if (!c.targetName.empty()) what += (what.empty() ? "" : " - ") + c.targetName;
    if (c.kind == CueKind::PayPrize) what += llformat(" (L$%d)", c.amount);
    return what;
}

std::string issuesText(const std::vector<Issue>& issues)
{
    std::string out;
    for (const Issue& i : issues)
    {
        out += (i.blocking ? "Must fix: " : "Note: ") + i.text + "\n";
    }
    return out;
}

std::string effectLine(const SimulatedEffect& e)
{
    std::string line = formatTime(e.atSeconds) + "  " + e.description;
    if (e.blockedByRlv) line += "  [RLV would block this: " + e.rlvReason + "]";
    return line + "\n";
}
} // namespace

SW67FloaterEventRehearsal::SW67FloaterEventRehearsal(const LLSD& key)
:   LLFloater(key)
{
    mPlan.name = "New event";
}

bool SW67FloaterEventRehearsal::postBuild()
{
    LLComboBox* kinds = getChild<LLComboBox>("cue_kind");
    for (int k = 0; k < static_cast<int>(CueKind::Count); ++k)
    {
        const CueKind kind = static_cast<CueKind>(k);
        kinds->add(std::string(cueName(kind)), LLSD(std::string(cueKey(kind))));
    }
    kinds->setValue(LLSD("chat"));
    getChild<LLUICtrl>("cue_list")->setCommitCallback([this](LLUICtrl*, const LLSD&) { loadCue(); });
    getChild<LLUICtrl>("add_cue_btn")->setCommitCallback([this](LLUICtrl*, const LLSD&) { onAddCue(); });
    getChild<LLUICtrl>("update_cue_btn")->setCommitCallback([this](LLUICtrl*, const LLSD&) { onUpdateCue(); });
    getChild<LLUICtrl>("remove_cue_btn")->setCommitCallback([this](LLUICtrl*, const LLSD&) { onRemoveCue(); });
    getChild<LLUICtrl>("pick_btn")->setCommitCallback([this](LLUICtrl*, const LLSD&) { onPickPerson(); });
    getChild<LLUICtrl>("check_btn")->setCommitCallback([this](LLUICtrl*, const LLSD&) { onCheck(); });
    getChild<LLUICtrl>("run_btn")->setCommitCallback([this](LLUICtrl*, const LLSD&) { onRun(); });
    getChild<LLUICtrl>("stop_btn")->setCommitCallback([this](LLUICtrl*, const LLSD&) { onStopRun(); });
    getChild<LLUICtrl>("save_btn")->setCommitCallback([this](LLUICtrl*, const LLSD&) { onSave(); });
    getChild<LLUICtrl>("plan_combo")->setCommitCallback([this](LLUICtrl*, const LLSD&) { onLoad(); });
    getChild<LLUICtrl>("new_btn")->setCommitCallback([this](LLUICtrl*, const LLSD&) { onNew(); });
    getChild<LLUICtrl>("stop_btn")->setEnabled(false);
    return LLFloater::postBuild();
}

void SW67FloaterEventRehearsal::onOpen(const LLSD& key)
{
    LLFloater::onOpen(key);
    refreshPlans();
    controlsFromPlan();
}

std::string SW67FloaterEventRehearsal::eventsDir()
{
    return sw67viewer::userDir("events");
}

void SW67FloaterEventRehearsal::refreshPlans()
{
    LLComboBox* combo = getChild<LLComboBox>("plan_combo");
    combo->removeall();
    const std::string suffix = ".sw67event.json";
    for (const std::string& file : sw67viewer::listFiles(eventsDir(), suffix))
    {
        combo->add(file.substr(0, file.size() - suffix.size()), LLSD(file));
    }
    combo->setLabel(std::string("Open a saved event"));
}

void SW67FloaterEventRehearsal::planFromControls()
{
    mPlan.name = getChild<LLUICtrl>("event_name")->getValue().asString();
    mPlan.plannedMinutes = intFrom(getChild<LLUICtrl>("planned_minutes"), 60);
    mPlan.prizeBudget = intFrom(getChild<LLUICtrl>("prize_budget"), 0);
}

void SW67FloaterEventRehearsal::controlsFromPlan()
{
    getChild<LLUICtrl>("event_name")->setValue(LLSD(mPlan.name));
    getChild<LLUICtrl>("planned_minutes")->setValue(LLSD(std::to_string(mPlan.plannedMinutes)));
    getChild<LLUICtrl>("prize_budget")->setValue(LLSD(std::to_string(mPlan.prizeBudget)));
    refreshCues();
}

void SW67FloaterEventRehearsal::refreshCues()
{
    LLScrollListCtrl* list = getChild<LLScrollListCtrl>("cue_list");
    const S32 keep = list->getFirstSelectedIndex();
    list->deleteAllItems();
    for (size_t i = 0; i < mPlan.cues.size(); ++i)
    {
        const Cue& c = mPlan.cues[i];
        LLSD row;
        row["value"] = static_cast<S32>(i);
        row["columns"][0]["column"] = "time";
        row["columns"][0]["value"] = formatTime(c.atSeconds);
        row["columns"][1]["column"] = "kind";
        row["columns"][1]["value"] = std::string(cueName(c.kind));
        row["columns"][2]["column"] = "what";
        row["columns"][2]["value"] = cueSummary(c);
        list->addElement(row);
    }
    if (keep >= 0 && static_cast<size_t>(keep) < mPlan.cues.size())
    {
        list->selectNthItem(keep);
    }
}

void SW67FloaterEventRehearsal::loadCue()
{
    const S32 sel = getChild<LLScrollListCtrl>("cue_list")->getFirstSelectedIndex();
    if (sel < 0 || static_cast<size_t>(sel) >= mPlan.cues.size()) return;
    const Cue& c = mPlan.cues[static_cast<size_t>(sel)];
    getChild<LLUICtrl>("cue_time")->setValue(LLSD(formatTime(c.atSeconds)));
    getChild<LLUICtrl>("cue_kind")->setValue(LLSD(std::string(cueKey(c.kind))));
    getChild<LLUICtrl>("cue_label")->setValue(LLSD(c.label));
    getChild<LLUICtrl>("cue_text")->setValue(LLSD(c.text));
    getChild<LLUICtrl>("cue_target_name")->setValue(LLSD(c.targetName));
    getChild<LLUICtrl>("cue_target_id")->setValue(LLSD(c.targetId));
    getChild<LLUICtrl>("cue_item_id")->setValue(LLSD(c.itemId));
    getChild<LLUICtrl>("cue_amount")->setValue(LLSD(std::to_string(c.amount)));
    getChild<LLUICtrl>("cue_channel")->setValue(LLSD(std::to_string(c.channel)));
}

bool SW67FloaterEventRehearsal::cueFromEditor(Cue& cue)
{
    const auto at = parseTime(getChild<LLUICtrl>("cue_time")->getValue().asString());
    const auto kind = parseCue(getChild<LLUICtrl>("cue_kind")->getValue().asString());
    if (!at)
    {
        showStatus(StatusRole::Warning, "Write the time as minutes:seconds (5:30) or hours:minutes:seconds.");
        return false;
    }
    if (!kind)
    {
        showStatus(StatusRole::Warning, "Choose what the cue does.");
        return false;
    }
    cue.atSeconds = *at;
    cue.kind = *kind;
    cue.label = getChild<LLUICtrl>("cue_label")->getValue().asString();
    cue.text = getChild<LLUICtrl>("cue_text")->getValue().asString();
    cue.targetName = getChild<LLUICtrl>("cue_target_name")->getValue().asString();
    cue.targetId = getChild<LLUICtrl>("cue_target_id")->getValue().asString();
    LLStringUtil::toLower(cue.targetId);
    cue.itemId = getChild<LLUICtrl>("cue_item_id")->getValue().asString();
    LLStringUtil::toLower(cue.itemId);
    cue.amount = intFrom(getChild<LLUICtrl>("cue_amount"), 0);
    cue.channel = intFrom(getChild<LLUICtrl>("cue_channel"), 0);
    return true;
}

void SW67FloaterEventRehearsal::onAddCue()
{
    if (mPlan.cues.size() >= kMaxCues)
    {
        showStatus(StatusRole::Warning, llformat("An event can have at most %d cues.", static_cast<int>(kMaxCues)));
        return;
    }
    Cue cue;
    if (!cueFromEditor(cue)) return;
    mPlan.cues.push_back(std::move(cue));
    refreshCues();
    showStatus(StatusRole::Info, "Cue added. Nothing happens until you rehearse, and a rehearsal sends nothing.");
}

void SW67FloaterEventRehearsal::onUpdateCue()
{
    const S32 sel = getChild<LLScrollListCtrl>("cue_list")->getFirstSelectedIndex();
    if (sel < 0 || static_cast<size_t>(sel) >= mPlan.cues.size()) return;
    Cue cue;
    if (!cueFromEditor(cue)) return;
    mPlan.cues[static_cast<size_t>(sel)] = std::move(cue);
    refreshCues();
}

void SW67FloaterEventRehearsal::onRemoveCue()
{
    const S32 sel = getChild<LLScrollListCtrl>("cue_list")->getFirstSelectedIndex();
    if (sel < 0 || static_cast<size_t>(sel) >= mPlan.cues.size()) return;
    mPlan.cues.erase(mPlan.cues.begin() + sel);
    refreshCues();
}

void SW67FloaterEventRehearsal::onPickPerson()
{
    const LLHandle<LLFloater> handle = getHandle();
    LLFloater* picker = LLFloaterAvatarPicker::show(
        [handle](const uuid_vec_t& ids, const std::vector<LLAvatarName>& names) {
            auto* self = static_cast<SW67FloaterEventRehearsal*>(handle.get());
            if (!self || ids.empty()) return;
            self->getChild<LLUICtrl>("cue_target_id")->setValue(LLSD(ids.front().asString()));
            if (!names.empty())
            {
                self->getChild<LLUICtrl>("cue_target_name")->setValue(LLSD(names.front().getCompleteName()));
            }
        },
        false, true, true, getName(), getChild<LLView>("pick_btn"));
    if (picker)
    {
        addDependentFloater(picker);
    }
}

void SW67FloaterEventRehearsal::onCheck()
{
    planFromControls();
    SW67RlvState state;
    const sw67::rlv::Gate gate(state);
    const RehearsalReport r = rehearse(mPlan, &gate);
    std::string text = r.summary() + "\n\n";
    if (!r.issues.empty())
    {
        text += issuesText(r.issues) + "\n";
    }
    for (const SimulatedEffect& e : r.effects) text += effectLine(e);
    getChild<LLUICtrl>("report_text")->setValue(LLSD(text));
    const bool blocking = std::any_of(r.issues.begin(), r.issues.end(), [](const Issue& i) { return i.blocking; });
    showStatus(blocking ? StatusRole::Warning : StatusRole::Success,
               blocking ? std::string("Rehearsed. Some cues must be fixed before the event; see the report.")
                        : std::string("Rehearsed. Nothing was sent, broadcast or paid."));
}

void SW67FloaterEventRehearsal::onRun()
{
    planFromControls();
    const std::string speed = getChild<LLUICtrl>("speed_combo")->getValue().asString();
    mSpeed = speed == "60" ? 60.f : speed == "10" ? 10.f : 1.f;
    mRun = std::make_unique<Rehearsal>(mPlan);
    mClock.reset();
    mRunLog = "Rehearsal clock started. Nothing is sent, broadcast or paid.\n";
    getChild<LLUICtrl>("report_text")->setValue(LLSD(mRunLog));
    getChild<LLUICtrl>("run_btn")->setEnabled(false);
    getChild<LLUICtrl>("stop_btn")->setEnabled(true);
    showStatus(StatusRole::Info, "Rehearsing...");
}

void SW67FloaterEventRehearsal::onStopRun()
{
    if (!mRun) return;
    mRun.reset();
    getChild<LLUICtrl>("run_btn")->setEnabled(true);
    getChild<LLUICtrl>("stop_btn")->setEnabled(false);
    showStatus(StatusRole::Neutral, "Rehearsal stopped.");
}

void SW67FloaterEventRehearsal::draw()
{
    if (mRun)
    {
        const int now = static_cast<int>(mClock.getElapsedTimeF32() * mSpeed);
        const std::vector<SimulatedEffect> due = mRun->advanceTo(now);
        for (const SimulatedEffect& e : due) mRunLog += effectLine(e);
        if (!due.empty())
        {
            getChild<LLUICtrl>("report_text")->setValue(LLSD(mRunLog));
        }
        const auto next = mRun->nextAt();
        getChild<LLUICtrl>("clock_text")
            ->setValue(LLSD("Rehearsal clock " + formatTime(now) +
                            (next ? "   next cue at " + formatTime(*next) : std::string("   all cues done"))));
        if (mRun->finished())
        {
            mRunLog += "Rehearsal finished. Nothing was sent, broadcast or paid.\n";
            getChild<LLUICtrl>("report_text")->setValue(LLSD(mRunLog));
            onStopRun();
            showStatus(StatusRole::Success, "Rehearsal finished.");
        }
    }
    LLFloater::draw();
}

void SW67FloaterEventRehearsal::onSave()
{
    planFromControls();
    if (mPlan.name.empty())
    {
        showStatus(StatusRole::Warning, "Give the event a name first.");
        return;
    }
    const std::string path =
        sw67viewer::joinPath(eventsDir(), sw67::photo::safeFileStem(mPlan.name) + ".sw67event.json");
    if (!sw67viewer::writeFileAtomic(path, writeEventPlan(mPlan)))
    {
        showStatus(StatusRole::Error, "Could not save " + path + ".");
        return;
    }
    refreshPlans();
    showStatus(StatusRole::Success, "Saved the event plan to " + path + ".");
}

void SW67FloaterEventRehearsal::onLoad()
{
    const std::string file = getChild<LLUICtrl>("plan_combo")->getValue().asString();
    if (file.empty()) return;
    const auto text = sw67viewer::readFile(sw67viewer::joinPath(eventsDir(), file), 2 * 1024 * 1024);
    ReadResult r = text ? readEventPlan(*text) : ReadResult{};
    if (!r.plan)
    {
        showStatus(StatusRole::Error, "Could not read that event plan.");
        return;
    }
    onStopRun();
    mPlan = std::move(*r.plan);
    controlsFromPlan();
    showStatus(r.notes.empty() ? StatusRole::Success : StatusRole::Warning,
               "Opened \"" + mPlan.name + "\"." + (r.notes.empty() ? "" : " " + r.notes.front()));
}

void SW67FloaterEventRehearsal::onNew()
{
    onStopRun();
    mPlan = EventPlan();
    mPlan.name = "New event";
    controlsFromPlan();
    getChild<LLUICtrl>("report_text")->setValue(LLSD(""));
}

void SW67FloaterEventRehearsal::showStatus(StatusRole role, const std::string& text)
{
    LLTextBox* box = getChild<LLTextBox>("status_text");
    box->setText(sw67viewer::statusText(role) + "  " + text);
    box->setColor(LLUIColor(sw67viewer::statusColor(role)));
}
