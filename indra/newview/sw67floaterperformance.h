/**
 * @file sw67floaterperformance.h
 * @brief Performance Assistant: the viewer's own measurements explained in
 *        plain language (measured facts apart from estimates), and the
 *        Everyday, Shopping, Building, Photography and Event presets with a
 *        preview of each change and a restore of the previous values.
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
#include "lltimer.h"

#include "sw67/a11y/profile.h"
#include "sw67/perf/assistant.h"

class SW67FloaterPerformance final : public LLFloater
{
public:
    AL_VIEW_TYPE(SW67FloaterPerformance, LLFloater);

    SW67FloaterPerformance(const LLSD& key);
    bool postBuild() override;
    void onOpen(const LLSD& key) override;
    void draw() override;

    /// What the viewer measures now. Fields it cannot read stay unset.
    static sw67::perf::Sample measure();
    /// The managed graphics settings' current values.
    static sw67::perf::GraphicsSettings currentSettings();

private:
    ~SW67FloaterPerformance() override = default;

    void refreshFindings();
    void refreshPreview();
    void onApply();
    void onRestore();
    void showStatus(sw67::a11y::StatusRole role, const std::string& text);
    static std::string restorePath();

    LLFrameTimer mRefresh;
};
