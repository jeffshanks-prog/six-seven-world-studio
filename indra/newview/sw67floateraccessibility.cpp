/**
 * @file sw67floateraccessibility.cpp
 * @brief 6|7 World Studio's accessibility floater.
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

#include "sw67floateraccessibility.h"

#include "sw67accessibility.h"
#include "sw67rlvstate.h"

#include "llfloaterreg.h"
#include "llformat.h"
#include "lltextbox.h"
#include "lluictrl.h"

using namespace sw67::a11y;

namespace
{
constexpr std::array<PalettePreset, 6> kPresets{PalettePreset::Standard,   PalettePreset::Protanopia,
                                                PalettePreset::Deuteranopia, PalettePreset::Tritanopia,
                                                PalettePreset::HighContrast, PalettePreset::Custom};
} // namespace

SW67FloaterAccessibility::SW67FloaterAccessibility(const LLSD& key)
:   LLFloater(key)
{
}

bool SW67FloaterAccessibility::postBuild()
{
    for (const char* name : {"preset_combo", "text_scale", "focus_highlight", "reduce_motion", "scene_vision",
                             "scene_strength", "scene_in_snapshots"})
    {
        getChild<LLUICtrl>(name)->setCommitCallback([this](LLUICtrl*, const LLSD&) { saveFromControls(); });
    }
    getChild<LLUICtrl>("rlv_show_btn")->setCommitCallback(
        [](LLUICtrl*, const LLSD&) { LLFloaterReg::showInstance("rlv_behaviours"); });
    getChild<LLUICtrl>("rlv_prefs_btn")->setCommitCallback(
        [](LLUICtrl*, const LLSD&) { LLFloaterReg::showInstance("preferences"); });
    loadIntoControls();
    return LLFloater::postBuild();
}

void SW67FloaterAccessibility::onOpen(const LLSD& key)
{
    LLFloater::onOpen(key);
    loadIntoControls();
}

void SW67FloaterAccessibility::loadIntoControls()
{
    mLoading = true;
    const AccessibilityProfile p = sw67viewer::loadAccessibility();
    getChild<LLUICtrl>("preset_combo")->setValue(LLSD(std::string(presetKey(p.preset))));
    getChild<LLUICtrl>("text_scale")->setValue(LLSD(p.textScale));
    getChild<LLUICtrl>("focus_highlight")->setValue(LLSD(p.keyboardFocusHighlight));
    getChild<LLUICtrl>("reduce_motion")->setValue(LLSD(p.reduceMotion));
    const bool filtering = p.sceneFilter.mode == SceneColorFilter::Mode::Compensate;
    getChild<LLUICtrl>("scene_vision")->setValue(
        LLSD(std::string(visionTypeKey(filtering ? p.sceneFilter.vision : VisionType::Typical))));
    getChild<LLUICtrl>("scene_strength")->setValue(LLSD(p.sceneFilter.strength));
    getChild<LLUICtrl>("scene_in_snapshots")->setValue(LLSD(p.sceneFilter.includeInSnapshots));
    mLoading = false;
    refreshPreview();
    refreshRlv();
}

void SW67FloaterAccessibility::saveFromControls()
{
    if (mLoading)
    {
        return;
    }
    AccessibilityProfile p = sw67viewer::loadAccessibility(); // keeps custom overrides
    const std::string preset = getChild<LLUICtrl>("preset_combo")->getValue().asString();
    for (PalettePreset candidate : kPresets)
    {
        if (presetKey(candidate) == preset)
        {
            p.preset = candidate;
        }
    }
    p.textScale = std::clamp((double)getChild<LLUICtrl>("text_scale")->getValue().asReal(), kMinTextScale, kMaxTextScale);
    p.keyboardFocusHighlight = getChild<LLUICtrl>("focus_highlight")->getValue().asBoolean();
    p.reduceMotion = getChild<LLUICtrl>("reduce_motion")->getValue().asBoolean();

    const auto vision = parseVisionType(getChild<LLUICtrl>("scene_vision")->getValue().asString());
    p.sceneFilter.vision = vision.value_or(VisionType::Typical);
    p.sceneFilter.mode = (p.sceneFilter.vision == VisionType::Typical) ? SceneColorFilter::Mode::Off
                                                                       : SceneColorFilter::Mode::Compensate;
    p.sceneFilter.strength = std::clamp((double)getChild<LLUICtrl>("scene_strength")->getValue().asReal(), 0.0, 1.0);
    p.sceneFilter.includeInSnapshots = getChild<LLUICtrl>("scene_in_snapshots")->getValue().asBoolean();

    sw67viewer::saveAccessibility(p);
    refreshPreview();
}

void SW67FloaterAccessibility::refreshPreview()
{
    const AccessibilityProfile p = sw67viewer::loadAccessibility();
    const InterfacePalette palette = resolvePalette(p);
    for (std::size_t i = 0; i < kStatusRoleCount; ++i)
    {
        const StatusIndicator s = indicator(palette, static_cast<StatusRole>(i));
        LLTextBox* box = getChild<LLTextBox>(llformat("status_%d", (S32)i));
        box->setText(std::string(shapeGlyph(s.shape)) + " " + std::string(s.label));
        box->setColor(LLUIColor(sw67viewer::toColor(s.colour)));
    }

    const bool strict = p.preset == PalettePreset::HighContrast;
    const auto issues = validatePalette(palette, presetVision(p.preset), strict);
    std::string report;
    if (issues.empty())
    {
        report = strict ? "Contrast check passed (AAA text, 4.5:1 status colours)."
                        : "Contrast check passed (AA text, 3:1 status colours, distinct for this colour vision).";
    }
    else
    {
        report = llformat("Contrast check: %d issue(s). First: %s", (S32)issues.size(), issues.front().what.c_str());
    }
    getChild<LLTextBox>("contrast_report")->setText(report);
}

void SW67FloaterAccessibility::refreshRlv()
{
    SW67RlvState rlv;
    std::string text;
    if (!rlv.isEnabled())
    {
        text = "RLV is off. 6|7 automation runs without RLV checks. Turn RLV on in Preferences if you use RLV devices.";
    }
    else if (!rlv.isReady())
    {
        text = "RLV is turned on but not active in this session (it starts at login). 6|7 automation will wait rather than act.";
    }
    else
    {
        const size_t count = rlv.activeRestrictions().size();
        text = llformat("RLV is active with %d restriction(s). 6|7 automation checks every step against them and "
                        "never releases them; only the objects that set them can.",
                        (S32)count);
    }
    getChild<LLTextBox>("rlv_status")->setText(text);
}
