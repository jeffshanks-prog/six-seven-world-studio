/**
 * @file sw67floateraccessibility.h
 * @brief 6|7 World Studio's accessibility floater: interface preset, text
 *        scale, keyboard focus, the 3D scene colour filter (kept apart from
 *        the interface), and RLV status.
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

class SW67FloaterAccessibility final : public LLFloater
{
public:
    AL_VIEW_TYPE(SW67FloaterAccessibility, LLFloater);

    SW67FloaterAccessibility(const LLSD& key);
    bool postBuild() override;
    void onOpen(const LLSD& key) override;

private:
    ~SW67FloaterAccessibility() override = default;

    void loadIntoControls();
    void saveFromControls();
    void refreshPreview();
    void refreshRlv();

    bool mLoading = false;
};
