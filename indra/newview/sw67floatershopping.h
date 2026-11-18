/**
 * @file sw67floatershopping.h
 * @brief Shopping and Demo Organizer: saved stores, a shopping list and
 *        tried-on demos, tagged by body, size, style and colour. Nothing is
 *        bought or paid; stores open on the world map for the user to go.
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
#include "sw67/shop/organizer.h"

#include <string>

class SW67FloaterShopping final : public LLFloater
{
public:
    AL_VIEW_TYPE(SW67FloaterShopping, LLFloater);

    SW67FloaterShopping(const LLSD& key);
    bool postBuild() override;
    void onOpen(const LLSD& key) override;

private:
    ~SW67FloaterShopping() override = default;

    enum class View
    {
        Stores,
        List,
        Demos,
    };
    View view() const;
    void onViewChanged();
    void refresh();
    void loadEntry();
    void onAdd();
    void onUpdate();
    void onRemove();
    void onHere();
    void onFromInventory();
    void onShowOnMap();
    void onWearDemo();
    bool save();
    void showStatus(sw67::a11y::StatusRole role, const std::string& text);
    static std::string filePath();
    S32 selected() const;

    sw67::shop::Organizer mData;
};
