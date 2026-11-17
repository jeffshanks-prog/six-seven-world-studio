/**
 * @file sw67floaterkingjahmir.h
 * @brief King Jahmir's conversation panel and his crown bubble.
 *
 * The bubble (sw67_king_jahmir_bubble) is a small floater with a gold crown
 * button: drag it by its top strip, press it (or Enter) to open the panel.
 * It shows unread replies as words ("2 new"), never by colour alone.
 *
 * The panel (sw67_king_jahmir) is resizable and has two tabs: Chat
 * (transcript, starters, confirm cards, Open buttons, retry, text size,
 * history and deletion) and Settings & privacy (gateway, sign-in, the three
 * opt-in switches, saved preferences).
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

#include <boost/signals2.hpp>

#include <array>
#include <map>
#include <string>
#include <vector>

class LLButton;
class LLTextBox;
class LLTextEditor;

class SW67FloaterKingJahmir final : public LLFloater
{
public:
    AL_VIEW_TYPE(SW67FloaterKingJahmir, LLFloater);

    SW67FloaterKingJahmir(const LLSD& key);
    bool postBuild() override;
    void onOpen(const LLSD& key) override;
    void setMinimized(bool minimize) override;
    void setVisible(bool visible) override;

private:
    ~SW67FloaterKingJahmir() override;

    void refresh() override;
    void renderTranscript();
    void refreshActions();
    void onSend();
    void onStarter(int index);
    void changeTextSize(int delta);
    void onHistory();
    void onDelete(bool all);
    void onSaveGateway();
    void onSignIn();
    void onSignOut();
    void refreshPreferences(const std::map<std::string, std::string>& prefs);
    void onSavePreference();
    void onDeletePreference();

    boost::signals2::scoped_connection mChanged;
    std::vector<std::pair<std::string, std::string>> mHistory;
    static constexpr int kMaxOpenButtons = 3;
    std::array<std::string, kMaxOpenButtons> mOpenTargets;
    std::size_t mRendered = static_cast<std::size_t>(-1);
    int mRenderedSize = -1;
};

class SW67FloaterKingJahmirBubble final : public LLFloater
{
public:
    AL_VIEW_TYPE(SW67FloaterKingJahmirBubble, LLFloater);

    SW67FloaterKingJahmirBubble(const LLSD& key);
    bool postBuild() override;
    void onOpen(const LLSD& key) override;

private:
    ~SW67FloaterKingJahmirBubble() override = default;

    void refresh() override;
    void placeClearOfControls();

    boost::signals2::scoped_connection mChanged;
};
