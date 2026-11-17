/**
 * @file sw67floaterperformance.cpp
 * @brief Performance Assistant floater.
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

#include "sw67floaterperformance.h"

#include "sw67accessibility.h"
#include "sw67viewerfiles.h"

#include "llappviewer.h"
#include "llcharacter.h"
#include "llcombobox.h"
#include "llfile.h"
#include "llfloaterreg.h"
#include "llformat.h"
#include "lltextbox.h"
#include "lltexturefetch.h"
#include "lltracerecording.h"
#include "lluictrl.h"
#include "llviewercontrol.h"
#include "llviewerjointattachment.h"
#include "llviewerstats.h"
#include "llvoavatar.h"
#include "llvoavatarself.h"

#include <cstdlib>

using namespace sw67::perf;
using sw67::a11y::StatusRole;

namespace
{
std::string valueText(LLControlVariable& c)
{
    const LLSD v = c.getValue();
    switch (c.type())
    {
    case TYPE_BOOLEAN: return v.asBoolean() ? "1" : "0";
    case TYPE_S32:
    case TYPE_U32: return std::to_string(v.asInteger());
    case TYPE_F32: return llformat("%g", v.asReal());
    default: return v.asString();
    }
}

bool setFromText(const std::string& name, const std::string& text)
{
    // Only settings the assistant manages; the core refuses others too.
    if (!isManagedSetting(name))
    {
        return false;
    }
    LLControlVariablePtr c = gSavedSettings.getControl(name);
    if (!c)
    {
        return false;
    }
    switch (c->type())
    {
    case TYPE_BOOLEAN: c->setValue(LLSD(text == "1" || text == "true")); return true;
    case TYPE_S32:
    case TYPE_U32:
    {
        const long n = std::strtol(text.c_str(), nullptr, 10);
        c->setValue(LLSD(static_cast<LLSD::Integer>(llclamp(n, 0L, 2147483647L))));
        return true;
    }
    case TYPE_F32: c->setValue(LLSD(std::strtod(text.c_str(), nullptr))); return true;
    default: return false;
    }
}

std::string planText(const ChangePlan& plan)
{
    if (plan.empty())
    {
        return plan.title + ": your settings already match. Nothing would change.";
    }
    std::string out = plan.title + " would change:\n";
    for (const SettingChange& c : plan.changes)
    {
        out += "  - " + c.setting + ": " + (c.from.empty() ? std::string("(unknown)") : c.from) + " -> " + c.to + " (" +
               c.why + ")\n";
    }
    out += "\n" + plan.disclaimer;
    return out;
}
} // namespace

SW67FloaterPerformance::SW67FloaterPerformance(const LLSD& key)
:   LLFloater(key)
{
}

bool SW67FloaterPerformance::postBuild()
{
    LLComboBox* combo = getChild<LLComboBox>("preset_combo");
    for (Preset p : {Preset::Everyday, Preset::Shopping, Preset::Building, Preset::Photography, Preset::Event})
    {
        combo->add(std::string(presetName(p)) + ": " + std::string(presetPurpose(p)), LLSD(std::string(presetKey(p))));
    }
    combo->setValue(LLSD("everyday"));
    combo->setCommitCallback([this](LLUICtrl*, const LLSD&) { refreshPreview(); });
    getChild<LLUICtrl>("apply_btn")->setCommitCallback([this](LLUICtrl*, const LLSD&) { onApply(); });
    getChild<LLUICtrl>("restore_btn")->setCommitCallback([this](LLUICtrl*, const LLSD&) { onRestore(); });
    getChild<LLUICtrl>("refresh_btn")->setCommitCallback([this](LLUICtrl*, const LLSD&) { refreshFindings(); });
    getChild<LLUICtrl>("graphics_btn")->setCommitCallback(
        [](LLUICtrl*, const LLSD&) { LLFloaterReg::showInstance("performance"); });
    return LLFloater::postBuild();
}

void SW67FloaterPerformance::onOpen(const LLSD& key)
{
    LLFloater::onOpen(key);
    refreshFindings();
    refreshPreview();
}

void SW67FloaterPerformance::draw()
{
    // Measurements move; the preset preview does not need to.
    if (mRefresh.getElapsedTimeF32() > 3.f)
    {
        refreshFindings();
    }
    LLFloater::draw();
}

Sample SW67FloaterPerformance::measure()
{
    Sample s;
    LLTrace::PeriodicRecording& rec = LLTrace::get_frame_recording();
    const F64 fps = rec.getPeriodMeanPerSec(LLStatViewer::FPS);
    if (fps > 0.0)
    {
        s.fps = fps;
    }
    const F32Milliseconds frame = rec.getPeriodMean(LLStatViewer::FRAMETIME);
    if (frame.value() > 0.f)
    {
        s.frameTimeMs = frame.value();
    }
    const F32Milliseconds ping = rec.getPeriodMean(LLStatViewer::SIM_PING);
    if (ping.value() > 0.f)
    {
        s.pingMs = ping.value();
    }
    const F32Percent loss = rec.getPeriodMean(LLStatViewer::PACKETS_LOST_PERCENT);
    if (loss.value() >= 0.f)
    {
        s.packetLossPercent = loss.value();
    }
    if (LLTextureFetch* fetch = LLAppViewer::getTextureFetch())
    {
        s.texturesPending = fetch->getNumRequests();
    }
    int nearby = 0;
    int full = 0;
    for (LLCharacter* character : LLCharacter::sInstances)
    {
        LLVOAvatar* av = dynamic_cast<LLVOAvatar*>(character);
        if (!av || av->isDead() || av->isSelf())
        {
            continue;
        }
        ++nearby;
        if (!av->isImpostor() && !av->isVisuallyMuted())
        {
            ++full;
        }
    }
    s.avatarsNearby = nearby;
    s.avatarsFullyDrawn = full;
    if (isAgentAvatarValid())
    {
        int huds = 0;
        for (const auto& [id, point] : gAgentAvatarp->mAttachmentPoints)
        {
            (void)id;
            if (point && point->getIsHUDAttachment())
            {
                huds += point->getNumObjects();
            }
        }
        s.attachmentsHUDs = huds;
    }
    return s;
}

GraphicsSettings SW67FloaterPerformance::currentSettings()
{
    GraphicsSettings g;
    for (const std::string& name : managedSettings())
    {
        if (LLControlVariablePtr c = gSavedSettings.getControl(name))
        {
            g.values[name] = valueText(*c);
        }
    }
    return g;
}

void SW67FloaterPerformance::refreshFindings()
{
    mRefresh.reset();
    const Sample s = measure();
    std::string text;
    if (s.fps)
    {
        text += llformat("Measured now: %.0f fps", *s.fps);
        if (s.frameTimeMs) text += llformat(", %.1f ms a frame", *s.frameTimeMs);
        if (s.pingMs) text += llformat(", ping %.0f ms", *s.pingMs);
        if (s.avatarsNearby) text += llformat(", %d avatars nearby (%d drawn in full)", *s.avatarsNearby, s.avatarsFullyDrawn.value_or(0));
        text += ".\n\n";
    }
    for (const Finding& f : explain(s, currentSettings()))
    {
        text += "[" + std::string(basisLabel(f.basis)) + "] " + f.title + "\n    " + f.explanation + "\n";
        if (!f.settings.empty())
        {
            text += "    Related settings:";
            for (const std::string& name : f.settings) text += " " + name;
            text += "\n";
        }
    }
    if (text.empty())
    {
        text = "No measurements yet. They appear a few seconds after login.";
    }
    getChild<LLUICtrl>("findings_text")->setValue(LLSD(text));
}

void SW67FloaterPerformance::refreshPreview()
{
    const auto preset = parsePreset(getChild<LLUICtrl>("preset_combo")->getValue().asString());
    if (!preset)
    {
        return;
    }
    getChild<LLUICtrl>("preview_text")->setValue(LLSD(planText(planPreset(*preset, currentSettings()))));
    getChild<LLUICtrl>("restore_btn")->setEnabled(sw67viewer::readFile(restorePath(), 64 * 1024).has_value());
}

std::string SW67FloaterPerformance::restorePath()
{
    return sw67viewer::joinPath(sw67viewer::userDir("performance"), "restore.json");
}

void SW67FloaterPerformance::onApply()
{
    const auto preset = parsePreset(getChild<LLUICtrl>("preset_combo")->getValue().asString());
    if (!preset)
    {
        return;
    }
    const GraphicsSettings now = currentSettings();
    const ChangePlan plan = planPreset(*preset, now);
    if (plan.empty())
    {
        showStatus(StatusRole::Info, "Nothing to change.");
        return;
    }
    // Save what to restore before changing anything. A second apply keeps
    // the first restore point, so Restore always goes back to the values
    // from before the assistant changed them.
    if (!sw67viewer::readFile(restorePath(), 64 * 1024))
    {
        const RestorePoint point = makeRestorePoint(plan, now, "settings before " + plan.title);
        if (!sw67viewer::writeFileAtomic(restorePath(), writeRestorePoint(point)))
        {
            showStatus(StatusRole::Error, "Could not save a restore point, so nothing was changed.");
            return;
        }
    }
    else if (auto existing = readRestorePoint(*sw67viewer::readFile(restorePath(), 64 * 1024)))
    {
        // Add any setting this preset changes that the restore point lacks.
        bool grew = false;
        for (const SettingChange& c : plan.changes)
        {
            if (!existing->values.count(c.setting) && now.values.count(c.setting))
            {
                existing->values[c.setting] = now.values.at(c.setting);
                grew = true;
            }
        }
        if (grew)
        {
            sw67viewer::writeFileAtomic(restorePath(), writeRestorePoint(*existing));
        }
    }
    int failed = 0;
    for (const SettingChange& c : plan.changes)
    {
        if (!setFromText(c.setting, c.to)) ++failed;
    }
    showStatus(failed ? StatusRole::Warning : StatusRole::Success,
               failed ? llformat("Applied %s; %d setting(s) could not be changed.", plan.title.c_str(), failed)
                      : "Applied " + plan.title + ". Restore puts your previous values back.");
    refreshPreview();
    refreshFindings();
}

void SW67FloaterPerformance::onRestore()
{
    const auto text = sw67viewer::readFile(restorePath(), 64 * 1024);
    const auto point = text ? readRestorePoint(*text) : std::nullopt;
    if (!point)
    {
        showStatus(StatusRole::Warning, "There is no restore point.");
        return;
    }
    const ChangePlan plan = planRestore(*point, currentSettings());
    int failed = 0;
    for (const SettingChange& c : plan.changes)
    {
        if (!setFromText(c.setting, c.to)) ++failed;
    }
    if (failed == 0)
    {
        LLFile::remove(restorePath());
    }
    showStatus(failed ? StatusRole::Warning : StatusRole::Success,
               failed ? llformat("Restored all but %d setting(s); the restore point is kept.", failed)
                      : llformat("Restored %d setting(s) to their previous values.", static_cast<int>(plan.changes.size())));
    refreshPreview();
    refreshFindings();
}

void SW67FloaterPerformance::showStatus(StatusRole role, const std::string& text)
{
    LLTextBox* box = getChild<LLTextBox>("status_text");
    box->setText(sw67viewer::statusText(role) + "  " + text);
    box->setColor(LLUIColor(sw67viewer::statusColor(role)));
}
