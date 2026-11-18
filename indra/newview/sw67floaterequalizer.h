/**
 * @file sw67floaterequalizer.h
 * @brief Sound Equalizer (Basic): a master switch, ten bands per source with
 *        presets and the listener's own, reset, and level meters with
 *        words as well as numbers. Sources the engine can't reach say why.
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
#include "sw67/audio/equalizer.h"

class SW67FloaterEqualizer final : public LLFloater
{
public:
    AL_VIEW_TYPE(SW67FloaterEqualizer, LLFloater);

    SW67FloaterEqualizer(const LLSD& key);
    bool postBuild() override;
    void onOpen(const LLSD& key) override;
    void draw() override;

private:
    ~SW67FloaterEqualizer() override = default;

    sw67::audio::Source source() const;
    void refresh();
    void refreshPresets();
    void refreshMeters();
    void changed(const std::string& message);
    void showStatus(sw67::a11y::StatusRole role, const std::string& text);

    LLFrameTimer mMeterTick;
};
