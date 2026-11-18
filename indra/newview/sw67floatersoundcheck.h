/**
 * @file sw67floatersoundcheck.h
 * @brief Sound Check (DJ & Hosting): a 3-band monitor EQ with input gain,
 *        output meters, clipping warnings and the stream's state, labelled
 *        with the path each control reaches. The DJ broadcasts from an
 *        external app, so every control here is Monitor Only and the
 *        broadcast switch stays off.
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

class SW67FloaterSoundCheck final : public LLFloater
{
public:
    AL_VIEW_TYPE(SW67FloaterSoundCheck, LLFloater);

    SW67FloaterSoundCheck(const LLSD& key);
    bool postBuild() override;
    void onOpen(const LLSD& key) override;
    void draw() override;

private:
    ~SW67FloaterSoundCheck() override = default;

    void refresh();
    void refreshMeters();
    void changed(const std::string& message);
    void showStatus(sw67::a11y::StatusRole role, const std::string& text);

    LLFrameTimer mMeterTick;
    LLFrameTimer mClipHold; ///< Keeps a clipping warning up for a few seconds.
    bool mClipped = false;
};
