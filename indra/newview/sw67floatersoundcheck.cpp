/**
 * @file sw67floatersoundcheck.cpp
 * @brief Sound Check floater.
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

#include "sw67floatersoundcheck.h"

#include "sw67accessibility.h"
#include "sw67audioeq.h"

#include "llaudioengine.h"
#include "llparcel.h"
#include "lltextbox.h"
#include "lluictrl.h"
#include "llviewercontrol.h"
#include "llviewerparcelmgr.h"

using namespace sw67::audio;
using sw67::a11y::StatusRole;
namespace eq = sw67viewer::audio;

namespace
{
const char* kPaths =
    "Local playback: Monitor EQ changes world and interface sounds as this viewer mixes them.\n"
    "Headphone preview: this viewer has one output, so it is the same as local playback; your outgoing stream is unchanged.\n"
    "Outgoing broadcast: sent by your own broadcast app; 6|7 can't reach or change it.";
}

SW67FloaterSoundCheck::SW67FloaterSoundCheck(const LLSD& key)
:   LLFloater(key)
{
}

bool SW67FloaterSoundCheck::postBuild()
{
    getChild<LLUICtrl>("paths_text")->setValue(LLSD(std::string(kPaths)));
    getChild<LLUICtrl>("broadcast_check")->setEnabled(false);
    getChild<LLUICtrl>("broadcast_check")->setValue(LLSD(false));
    getChild<LLUICtrl>("sc_on_check")->setCommitCallback([this](LLUICtrl* c, const LLSD&) {
        eq::settings().soundCheck.on = c->getValue().asBoolean();
        changed(c->getValue().asBoolean() ? "Monitor EQ on (local playback only)." : "Monitor EQ off.");
    });
    struct Band
    {
        const char* name;
        float SoundCheck::*field;
        const char* label;
    };
    static const Band bands[] = {{"low_slider", &SoundCheck::lowDb, "Low"},
                                 {"mid_slider", &SoundCheck::midDb, "Mid"},
                                 {"high_slider", &SoundCheck::highDb, "High"},
                                 {"input_slider", &SoundCheck::inputDb, "Input gain"}};
    for (const Band& b : bands)
    {
        getChild<LLUICtrl>(b.name)->setCommitCallback([this, b](LLUICtrl* c, const LLSD&) {
            eq::settings().soundCheck.*(b.field) = clampGain(static_cast<float>(c->getValue().asReal()));
            changed(llformat("%s %+.1f dB.", b.label, static_cast<double>(eq::settings().soundCheck.*(b.field))));
        });
    }
    getChild<LLUICtrl>("sc_reset_btn")->setCommitCallback([this](LLUICtrl*, const LLSD&) {
        eq::settings().soundCheck = SoundCheck{};
        refresh();
        changed("Sound Check reset: flat and off.");
    });
    return LLFloater::postBuild();
}

void SW67FloaterSoundCheck::onOpen(const LLSD& key)
{
    LLFloater::onOpen(key);
    // The broadcast switch never carries over: it starts off every time.
    eq::settings().soundCheck.applyToBroadcast = false;
    refresh();
}

void SW67FloaterSoundCheck::draw()
{
    if (mMeterTick.getElapsedTimeF32() >= 0.25f)
    {
        mMeterTick.reset();
        refreshMeters();
    }
    LLFloater::draw();
}

void SW67FloaterSoundCheck::refresh()
{
    const SoundCheck& sc = eq::settings().soundCheck;
    getChild<LLUICtrl>("sc_on_check")->setValue(LLSD(sc.on));
    getChild<LLUICtrl>("low_slider")->setValue(LLSD(static_cast<F64>(sc.lowDb)));
    getChild<LLUICtrl>("mid_slider")->setValue(LLSD(static_cast<F64>(sc.midDb)));
    getChild<LLUICtrl>("high_slider")->setValue(LLSD(static_cast<F64>(sc.highDb)));
    getChild<LLUICtrl>("input_slider")->setValue(LLSD(static_cast<F64>(sc.inputDb)));
    const EngineSupport sup = eq::support();
    const bool usable = sup.world || sup.ui;
    for (const char* n : {"sc_on_check", "low_slider", "mid_slider", "high_slider", "input_slider", "sc_reset_btn"})
    {
        getChild<LLUICtrl>(n)->setEnabled(usable);
    }
    if (!usable)
    {
        showStatus(StatusRole::Info, limitation(sup, Source::World));
    }
    refreshMeters();
}

void SW67FloaterSoundCheck::refreshMeters()
{
    std::string out;
    bool clipping = false;
    for (Source s : {Source::World, Source::Ui})
    {
        Level l;
        out += std::string(sourceName(s)) + ": ";
        if (eq::level(s, l))
        {
            out += describeLevel(l);
            clipping = clipping || clipState(l) == ClipState::Clipping;
        }
        else
        {
            out += "no meter";
        }
        out += "\n";
    }
    out += "Parcel music: not measurable here (it plays in the media plugin, outside the viewer's mixer).";
    getChild<LLUICtrl>("meters_text")->setValue(LLSD(out));
    if (clipping)
    {
        mClipped = true;
        mClipHold.reset();
    }
    else if (mClipped && mClipHold.getElapsedTimeF32() > 3.f)
    {
        mClipped = false;
    }
    LLTextBox* clip = getChild<LLTextBox>("clip_text");
    clip->setText(std::string(mClipped ? "[!] CLIPPING: lower Input gain or the bands" : "No clipping"));
    clip->setColor(LLUIColor(sw67viewer::statusColor(mClipped ? StatusRole::Error : StatusRole::Success)));

    std::string stream = "Stream: ";
    LLParcel* parcel = LLViewerParcelMgr::getInstance()->getAgentParcel();
    if (!parcel || parcel->getMusicURL().empty()) stream += "this parcel has no music stream set.";
    else if (!gSavedSettings.getBOOL("AudioStreamingMusic")) stream += "set, but streaming music is off in your sound settings.";
    else if (gAudiop && gAudiop->isInternetStreamPlaying() == LLAudioEngine::AUDIO_PLAYING) stream += "playing in this viewer.";
    else stream += "set, not playing in this viewer now.";
    getChild<LLUICtrl>("stream_text")->setValue(LLSD(stream + " For a full check, open Event Control Room > Stream check."));
}

void SW67FloaterSoundCheck::changed(const std::string& message)
{
    if (eq::saveAndApply()) showStatus(StatusRole::Success, message + " Saved.");
    else showStatus(StatusRole::Error, message + " It applies now but could not be saved.");
}

void SW67FloaterSoundCheck::showStatus(StatusRole role, const std::string& text)
{
    LLTextBox* box = getChild<LLTextBox>("status_text");
    box->setText(sw67viewer::statusText(role) + "  " + text);
    box->setColor(LLUIColor(sw67viewer::statusColor(role)));
}
