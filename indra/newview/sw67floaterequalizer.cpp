/**
 * @file sw67floaterequalizer.cpp
 * @brief Sound Equalizer floater.
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

#include "sw67floaterequalizer.h"

#include "sw67accessibility.h"
#include "sw67audioeq.h"

#include "llcombobox.h"
#include "lltextbox.h"
#include "lluictrl.h"

using namespace sw67::audio;
using sw67::a11y::StatusRole;
namespace eq = sw67viewer::audio;

SW67FloaterEqualizer::SW67FloaterEqualizer(const LLSD& key)
:   LLFloater(key)
{
}

bool SW67FloaterEqualizer::postBuild()
{
    getChild<LLUICtrl>("master_check")->setCommitCallback([this](LLUICtrl* c, const LLSD&) {
        eq::settings().enabled = c->getValue().asBoolean();
        changed(eq::settings().enabled ? "Equalizer on." : "Equalizer off: every source plays unchanged.");
    });
    getChild<LLUICtrl>("source_combo")->setCommitCallback([this](LLUICtrl*, const LLSD&) { refresh(); });
    getChild<LLUICtrl>("source_on_check")->setCommitCallback([this](LLUICtrl* c, const LLSD&) {
        eq::settings().source(source()).on = c->getValue().asBoolean();
        changed(std::string(sourceName(source())) + (c->getValue().asBoolean() ? ": equalized." : ": not equalized."));
    });
    for (int b = 0; b < kBands; ++b)
    {
        getChild<LLUICtrl>("band_" + std::to_string(b))->setCommitCallback([this, b](LLUICtrl* c, const LLSD&) {
            setBand(eq::settings(), source(), b, static_cast<float>(c->getValue().asReal()));
            refreshPresets();
            changed(bandLabel(b) + " set to " + llformat("%+.1f dB.", eq::settings().source(source()).gains[static_cast<size_t>(b)]));
        });
    }
    getChild<LLUICtrl>("preset_combo")->setCommitCallback([this](LLUICtrl* c, const LLSD&) {
        const std::string key = c->getValue().asString();
        if (key == "custom" || !applyPreset(eq::settings(), source(), key)) return;
        refresh();
        changed("Preset applied.");
    });
    getChild<LLUICtrl>("reset_btn")->setCommitCallback([this](LLUICtrl*, const LLSD&) {
        reset(eq::settings(), source());
        refresh();
        changed(std::string(sourceName(source())) + " reset: flat and off.");
    });
    getChild<LLUICtrl>("save_preset_btn")->setCommitCallback([this](LLUICtrl*, const LLSD&) {
        const auto key = saveCustomPreset(eq::settings(), source(), getChild<LLUICtrl>("preset_name")->getValue().asString());
        if (!key)
        {
            showStatus(StatusRole::Warning, "Type a name for the preset first (up to 50 custom presets).");
            return;
        }
        getChild<LLUICtrl>("preset_name")->setValue(LLSD(""));
        refreshPresets();
        changed("Preset saved.");
    });
    getChild<LLUICtrl>("delete_preset_btn")->setCommitCallback([this](LLUICtrl*, const LLSD&) {
        const std::string key = getChild<LLUICtrl>("preset_combo")->getValue().asString();
        if (!removeCustomPreset(eq::settings(), key))
        {
            showStatus(StatusRole::Info, "Choose one of your own presets to delete; built-in presets stay.");
            return;
        }
        refreshPresets();
        changed("Preset deleted. The bands keep their values.");
    });
    return LLFloater::postBuild();
}

void SW67FloaterEqualizer::onOpen(const LLSD& key)
{
    LLFloater::onOpen(key);
    refresh();
}

void SW67FloaterEqualizer::draw()
{
    if (mMeterTick.getElapsedTimeF32() >= 0.25f)
    {
        mMeterTick.reset();
        refreshMeters();
    }
    LLFloater::draw();
}

Source SW67FloaterEqualizer::source() const
{
    const std::string v = getChild<LLComboBox>("source_combo")->getValue().asString();
    if (v == "ui") return Source::Ui;
    if (v == "music") return Source::Music;
    if (v == "voice") return Source::Voice;
    return Source::World;
}

void SW67FloaterEqualizer::refresh()
{
    const Settings& s = eq::settings();
    const Source src = source();
    const EngineSupport sup = eq::support();
    const bool usable = sup.supports(src);
    getChild<LLUICtrl>("master_check")->setValue(LLSD(s.enabled));
    getChild<LLUICtrl>("source_on_check")->setValue(LLSD(s.source(src).on && usable));
    getChild<LLUICtrl>("source_on_check")->setEnabled(usable);
    const std::string why = limitation(sup, src);
    getChild<LLUICtrl>("limitation_text")->setValue(LLSD(why.empty() ? std::string("Equalized through the viewer's own mixer.") : "Not available: " + why));
    for (int b = 0; b < kBands; ++b)
    {
        LLUICtrl* band = getChild<LLUICtrl>("band_" + std::to_string(b));
        band->setValue(LLSD(static_cast<F64>(s.source(src).gains[static_cast<size_t>(b)])));
        band->setEnabled(usable);
    }
    for (const char* n : {"preset_combo", "reset_btn", "save_preset_btn", "delete_preset_btn", "preset_name"})
    {
        getChild<LLUICtrl>(n)->setEnabled(usable);
    }
    refreshPresets();
    refreshMeters();
}

void SW67FloaterEqualizer::refreshPresets()
{
    LLComboBox* combo = getChild<LLComboBox>("preset_combo");
    combo->removeall();
    for (const Preset& p : allPresets(eq::settings())) combo->add(p.name + (p.builtin ? "" : " (yours)"), LLSD(p.key));
    combo->add("Custom (bands changed)", LLSD("custom"));
    combo->setSelectedByValue(LLSD(eq::settings().source(source()).preset), true);
}

void SW67FloaterEqualizer::refreshMeters()
{
    std::string out;
    for (Source s : {Source::World, Source::Ui, Source::Music, Source::Voice})
    {
        Level l;
        out += std::string(sourceName(s)) + ": ";
        if (eq::level(s, l))
        {
            const ClipState c = clipState(l);
            out += std::string(c == ClipState::Clipping ? "[!] " : c == ClipState::Hot ? "[~] " : "") + describeLevel(l);
        }
        else
        {
            out += "no meter (outside the viewer's mixer)";
        }
        out += "\n";
    }
    getChild<LLUICtrl>("meters_text")->setValue(LLSD(out));
}

void SW67FloaterEqualizer::changed(const std::string& message)
{
    if (eq::saveAndApply()) showStatus(StatusRole::Success, message + " Saved.");
    else showStatus(StatusRole::Error, message + " It applies now but could not be saved.");
}

void SW67FloaterEqualizer::showStatus(StatusRole role, const std::string& text)
{
    LLTextBox* box = getChild<LLTextBox>("status_text");
    box->setText(sw67viewer::statusText(role) + "  " + text);
    box->setColor(LLUIColor(sw67viewer::statusColor(role)));
}
