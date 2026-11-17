/**
 * @file sw67floaterrecordingprivacy.h
 * @brief Recording Privacy Mode: one switch that hides name tags, chat and
 *        IM pop-ups, location and balance on this screen while streaming or
 *        recording, and puts them back exactly when turned off.
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

#include "sw67/a11y/profile.h"
#include "sw67/privacy/recording.h"

#include <optional>
#include <string>

class SW67FloaterRecordingPrivacy final : public LLFloater
{
public:
    AL_VIEW_TYPE(SW67FloaterRecordingPrivacy, LLFloater);

    SW67FloaterRecordingPrivacy(const LLSD& key);
    bool postBuild() override;
    void onOpen(const LLSD& key) override;

    /// The saved state, when privacy mode is on (also after a restart).
    static std::optional<sw67::privacy::State> savedState();
    /// The managed display settings as they are now.
    static sw67::privacy::Values currentValues();

private:
    ~SW67FloaterRecordingPrivacy() override = default;

    sw67::privacy::Options optionsFromControls() const;
    void refresh();
    void onToggle();
    void showStatus(sw67::a11y::StatusRole role, const std::string& text);
    static std::string statePath();
};
