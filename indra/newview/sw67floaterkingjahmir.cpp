/**
 * @file sw67floaterkingjahmir.cpp
 * @brief King Jahmir's conversation panel and crown bubble.
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

#include "sw67floaterkingjahmir.h"

#include "sw67accessibility.h"
#include "sw67kingjahmir.h"

#include "llbutton.h"
#include "llcheckboxctrl.h"
#include "llcombobox.h"
#include "llfloaterreg.h"
#include "lllineeditor.h"
#include "llnotificationsutil.h"
#include "llscrolllistctrl.h"
#include "lltextbox.h"
#include "lltexteditor.h"
#include "lluicolortable.h"
#include "llviewercontrol.h"

#include <algorithm>
#include <array>

namespace kj = sw67viewer::kingjahmir;
using namespace sw67::assistant;

namespace
{
constexpr std::array<const char*, 5> kFonts = {"SansSerifSmall", "SansSerif", "SansSerifMedium", "SansSerifLarge",
                                               "SansSerifHuge"};

int textSize() { return llclamp(gSavedSettings.getS32("SW67KingJahmirTextSize"), 0, 4); }

LLStyle::Params style(const char* colour, bool bold)
{
    LLStyle::Params p;
    const LLUIColor c = LLUIColorTable::instance().getColor(colour);
    p.color(c);
    p.readonly_color(c);
    p.font.name(kFonts[static_cast<size_t>(textSize())]);
    p.font.style(bold ? "BOLD" : "NORMAL");
    return p;
}

void appendBlocks(LLTextEditor* out, const std::vector<Block>& blocks)
{
    for (const Block& b : blocks)
    {
        std::string prefix;
        if (b.kind == Block::Kind::Numbered)
        {
            prefix = "  " + std::to_string(b.number) + ". ";
        }
        else if (b.kind == Block::Kind::Bulleted)
        {
            prefix = "  \xE2\x80\xA2 "; // bullet
        }
        out->appendText(prefix, true, style("SW67White", b.kind == Block::Kind::Numbered));
        for (const Span& s : b.spans)
        {
            switch (s.kind)
            {
            case Span::Kind::Text: out->appendText(s.text, false, style("SW67White", false)); break;
            case Span::Kind::Bold: out->appendText(s.text, false, style("SW67White", true)); break;
            case Span::Kind::ScreenLink:
            {
                // Opens the registered window through the viewer's own
                // floater link, and only when the user clicks it.
                LLStyle::Params p = style("SW67Gold", true);
                p.link_href = "secondlife:///app/openfloater/" + s.target;
                out->appendText(s.text + " (open)", false, p);
                break;
            }
            case Span::Kind::HelpLink:
            {
                const std::string url = kj::helpUrl(s.target);
                LLStyle::Params p = style("SW67Gold", true);
                if (!url.empty())
                {
                    p.link_href = url;
                }
                out->appendText(s.text + " (help page)", false, p);
                break;
            }
            }
        }
    }
}
} // namespace

// ---- conversation panel ------------------------------------------------------

SW67FloaterKingJahmir::SW67FloaterKingJahmir(const LLSD& key)
:   LLFloater(key)
{
}

SW67FloaterKingJahmir::~SW67FloaterKingJahmir() = default;

bool SW67FloaterKingJahmir::postBuild()
{
    getChild<LLUICtrl>("send_btn")->setCommitCallback([this](LLUICtrl*, const LLSD&) { onSend(); });
    getChild<LLUICtrl>("message_input")->setCommitCallback([this](LLUICtrl*, const LLSD&) { onSend(); });
    getChild<LLUICtrl>("retry_btn")->setCommitCallback([](LLUICtrl*, const LLSD&) { kj::retry(); });
    getChild<LLUICtrl>("stop_btn")->setCommitCallback([](LLUICtrl*, const LLSD&) { kj::abandon(); });
    for (int i = 0; i < 6; ++i)
    {
        getChild<LLUICtrl>("starter_" + std::to_string(i))
            ->setCommitCallback([this, i](LLUICtrl*, const LLSD&) { onStarter(i); });
    }
    getChild<LLUICtrl>("text_smaller_btn")->setCommitCallback([this](LLUICtrl*, const LLSD&) { changeTextSize(-1); });
    getChild<LLUICtrl>("text_larger_btn")->setCommitCallback([this](LLUICtrl*, const LLSD&) { changeTextSize(1); });
    getChild<LLUICtrl>("new_btn")->setCommitCallback([](LLUICtrl*, const LLSD&) { kj::newConversation(); });
    getChild<LLUICtrl>("history_btn")->setCommitCallback([this](LLUICtrl*, const LLSD&) { onHistory(); });
    getChild<LLUICtrl>("history_combo")->setCommitCallback([](LLUICtrl* ctrl, const LLSD&) {
        kj::openConversation(ctrl->getValue().asString());
    });
    getChild<LLUICtrl>("delete_btn")->setCommitCallback([this](LLUICtrl*, const LLSD&) { onDelete(false); });
    getChild<LLUICtrl>("delete_all_btn")->setCommitCallback([this](LLUICtrl*, const LLSD&) { onDelete(true); });
    getChild<LLUICtrl>("confirm_btn")->setCommitCallback([](LLUICtrl*, const LLSD&) {
        if (!kj::conversation().pendingActions().empty())
        {
            kj::resolveAction(kj::conversation().pendingActions().front().id, true);
        }
    });
    getChild<LLUICtrl>("cancel_action_btn")->setCommitCallback([](LLUICtrl*, const LLSD&) {
        if (!kj::conversation().pendingActions().empty())
        {
            kj::resolveAction(kj::conversation().pendingActions().front().id, false);
        }
    });
    for (int i = 0; i < kMaxOpenButtons; ++i)
    {
        getChild<LLUICtrl>("open_" + std::to_string(i))->setCommitCallback([this, i](LLUICtrl*, const LLSD&) {
            const std::string screen = mOpenTargets[static_cast<size_t>(i)];
            if (kj::isKnownScreen(screen))
            {
                LLFloaterReg::showInstance(screen);
            }
        });
    }

    // Settings & privacy
    getChild<LLUICtrl>("gateway_input")->setValue(LLSD(gSavedSettings.getString("SW67KingJahmirGatewayURL")));
    getChild<LLUICtrl>("gateway_save_btn")->setCommitCallback([this](LLUICtrl*, const LLSD&) { onSaveGateway(); });
    getChild<LLUICtrl>("sign_in_btn")->setCommitCallback([this](LLUICtrl*, const LLSD&) { onSignIn(); });
    getChild<LLUICtrl>("sign_out_btn")->setCommitCallback([this](LLUICtrl*, const LLSD&) { onSignOut(); });
    getChild<LLUICtrl>("pref_refresh_btn")->setCommitCallback([this](LLUICtrl*, const LLSD&) {
        kj::fetchPreferences([h = getDerivedHandle<SW67FloaterKingJahmir>()](std::map<std::string, std::string> p) {
            if (auto* self = h.get())
            {
                self->refreshPreferences(p);
            }
        });
    });
    getChild<LLUICtrl>("pref_save_btn")->setCommitCallback([this](LLUICtrl*, const LLSD&) { onSavePreference(); });
    getChild<LLUICtrl>("pref_delete_btn")->setCommitCallback([this](LLUICtrl*, const LLSD&) { onDeletePreference(); });
    // The three opt-in switches and the bubble switch are bound to their
    // settings in the XUI (control_name), so they persist on their own.

    LLTextEditor* transcript = getChild<LLTextEditor>("transcript");
    transcript->setParseHTML(false); // only links King Jahmir's renderer made are clickable

    mChanged = kj::onChanged([this]() { refresh(); });
    return LLFloater::postBuild();
}

void SW67FloaterKingJahmir::onOpen(const LLSD& key)
{
    LLFloater::onOpen(key);
    kj::conversation().markRead();
    mRendered = static_cast<std::size_t>(-1);
    refresh();
    getChild<LLUICtrl>("message_input")->setFocus(true);
    if (LLFloater* bubble = LLFloaterReg::findInstance("sw67_king_jahmir_bubble"))
    {
        bubble->refresh();
    }
}

void SW67FloaterKingJahmir::setMinimized(bool minimize)
{
    LLFloater::setMinimized(minimize);
    if (!minimize)
    {
        kj::conversation().markRead();
        refresh();
    }
}

void SW67FloaterKingJahmir::setVisible(bool visible)
{
    LLFloater::setVisible(visible);
    if (visible)
    {
        kj::conversation().markRead();
    }
    if (LLFloater* bubble = LLFloaterReg::findInstance("sw67_king_jahmir_bubble"))
    {
        bubble->refresh();
    }
}

void SW67FloaterKingJahmir::refresh()
{
    const auto& conv = kj::conversation();
    if (getVisible() && !isMinimized())
    {
        kj::conversation().markRead();
    }
    renderTranscript();
    refreshActions();

    const kj::Status& st = kj::status();
    LLTextBox* status = getChild<LLTextBox>("status_text");
    if (st.text.empty())
    {
        status->setText(conv.waiting() ? "King Jahmir is thinking..." : std::string());
    }
    else
    {
        status->setText(sw67viewer::statusText(st.role) + "  " + st.text);
        status->setColor(LLUIColor(sw67viewer::statusColor(st.role)));
    }
    getChild<LLUICtrl>("retry_btn")->setVisible(st.canRetry && conv.waiting());
    getChild<LLUICtrl>("stop_btn")->setVisible(conv.waiting());
    getChild<LLUICtrl>("send_btn")->setEnabled(!conv.waiting());

    const kj::Bootstrap& boot = kj::bootstrap();
    std::string mode = "AI assistant";
    if (boot.developmentMode)
    {
        mode += boot.mockModel ? "  |  DEVELOPMENT MODE: mock model, not real AI" : "  |  DEVELOPMENT MODE";
    }
    getChild<LLTextBox>("mode_text")->setText(mode);

    // Starters show on an empty conversation.
    const std::vector<std::string> starters = boot.starters.size() == 6 ? boot.starters : defaultStarters();
    const bool empty = conv.messages().empty();
    getChild<LLView>("starters_panel")->setVisible(empty);
    for (int i = 0; i < 6; ++i)
    {
        getChild<LLButton>("starter_" + std::to_string(i))->setLabel(starters[static_cast<size_t>(i)]);
    }

    // Settings tab
    const bool signedIn = kj::signedIn();
    getChild<LLTextBox>("account_status_text")
        ->setText(signedIn ? sw67viewer::statusText(sw67::a11y::StatusRole::Success) + "  Signed in to 6|7."
                           : sw67viewer::statusText(sw67::a11y::StatusRole::Neutral) + "  Not signed in.");
    getChild<LLUICtrl>("sign_out_btn")->setEnabled(signedIn);
    std::string retention = "Conversations are stored on the King Jahmir server";
    retention += boot.retentionDays > 0 ? " for " + std::to_string(boot.retentionDays) + " days, then deleted."
                                        : " for a limited time, then deleted.";
    getChild<LLTextBox>("retention_text")->setText(retention);
}

void SW67FloaterKingJahmir::renderTranscript()
{
    const auto& msgs = kj::conversation().messages();
    if (msgs.size() == mRendered && textSize() == mRenderedSize)
    {
        return;
    }
    LLTextEditor* out = getChild<LLTextEditor>("transcript");
    out->setText(std::string());
    const std::string greeting = kj::bootstrap().greeting.empty() ? std::string(kGreeting) : kj::bootstrap().greeting;
    out->appendText("King Jahmir (AI): ", false, style("SW67Gold", true));
    out->appendText(greeting, false, style("SW67White", false));
    for (const Message& m : msgs)
    {
        out->appendText("", true, style("SW67White", false));
        switch (m.role)
        {
        case Message::Role::User:
            out->appendText("You: ", true, style("SW67Gold", true));
            out->appendText(m.text, false, style("SW67White", false));
            break;
        case Message::Role::Assistant:
            out->appendText("King Jahmir (AI):", true, style("SW67Gold", true));
            appendBlocks(out, parseReply(m.text, kj::isKnownScreen));
            break;
        case Message::Role::Notice:
            out->appendText("\xE2\x96\xA0 " + m.text, true, style("SW67Gold", false)); // square: app notice
            break;
        }
    }
    out->setCursorAndScrollToEnd();
    mRendered = msgs.size();
    mRenderedSize = textSize();
}

void SW67FloaterKingJahmir::refreshActions()
{
    const auto& pending = kj::conversation().pendingActions();
    LLView* card = getChild<LLView>("confirm_panel");
    card->setVisible(!pending.empty());
    if (!pending.empty())
    {
        getChild<LLTextBox>("confirm_title")->setText(pending.front().title);
        getChild<LLTextEditor>("confirm_preview")->setText(pending.front().preview);
    }
    // Open buttons for the windows the last reply offered.
    std::vector<Link> screens;
    const auto& msgs = kj::conversation().messages();
    if (!msgs.empty() && msgs.back().role == Message::Role::Assistant)
    {
        for (const Block& b : parseReply(msgs.back().text, kj::isKnownScreen))
        {
            for (const Span& s : b.spans)
            {
                if (s.kind == Span::Kind::ScreenLink && screens.size() < static_cast<size_t>(kMaxOpenButtons) &&
                    std::none_of(screens.begin(), screens.end(), [&](const Link& l) { return l.id == s.target; }))
                {
                    screens.push_back(Link{true, s.target, s.text});
                }
            }
        }
    }
    for (int i = 0; i < kMaxOpenButtons; ++i)
    {
        LLButton* btn = getChild<LLButton>("open_" + std::to_string(i));
        const bool show = static_cast<size_t>(i) < screens.size();
        btn->setVisible(show);
        if (show)
        {
            btn->setLabel("Open: " + screens[static_cast<size_t>(i)].label);
            mOpenTargets[static_cast<size_t>(i)] = screens[static_cast<size_t>(i)].id;
        }
        else
        {
            mOpenTargets[static_cast<size_t>(i)].clear();
        }
    }
}

void SW67FloaterKingJahmir::onSend()
{
    LLUICtrl* input = getChild<LLUICtrl>("message_input");
    const std::string text = input->getValue().asString();
    if (kj::send(text))
    {
        input->setValue(LLSD(""));
    }
}

void SW67FloaterKingJahmir::onStarter(int index)
{
    const std::vector<std::string> starters =
        kj::bootstrap().starters.size() == 6 ? kj::bootstrap().starters : defaultStarters();
    kj::send(starters[static_cast<size_t>(index)]);
}

void SW67FloaterKingJahmir::changeTextSize(int delta)
{
    gSavedSettings.setS32("SW67KingJahmirTextSize", llclamp(textSize() + delta, 0, 4));
    renderTranscript();
}

void SW67FloaterKingJahmir::onHistory()
{
    kj::listConversations(
        [h = getDerivedHandle<SW67FloaterKingJahmir>()](std::vector<std::pair<std::string, std::string>> list) {
            auto* self = h.get();
            if (!self)
            {
                return;
            }
            self->mHistory = std::move(list);
            LLComboBox* combo = self->getChild<LLComboBox>("history_combo");
            combo->removeall();
            for (const auto& [id, title] : self->mHistory)
            {
                combo->add(title, LLSD(id));
            }
            combo->setLabel(self->mHistory.empty() ? std::string("No saved conversations")
                                                   : std::string("Choose a past conversation"));
            combo->setEnabled(!self->mHistory.empty());
        });
}

void SW67FloaterKingJahmir::onDelete(bool all)
{
    LLSD args;
    args["MESSAGE"] = all ? "Delete every conversation you have had with King Jahmir from his server?\nThis cannot be "
                            "undone."
                          : "Delete this conversation from King Jahmir's server?\nThis cannot be undone.";
    LLNotificationsUtil::add("GenericAlertYesCancel", args, LLSD(), [all](const LLSD& n, const LLSD& r) {
        if (LLNotificationsUtil::getSelectedOption(n, r) != 0)
        {
            return;
        }
        if (all)
        {
            kj::deleteAllConversations();
        }
        else
        {
            kj::deleteConversation();
        }
    });
}

void SW67FloaterKingJahmir::onSaveGateway()
{
    const std::string url = getChild<LLUICtrl>("gateway_input")->getValue().asString();
    gSavedSettings.setString("SW67KingJahmirGatewayURL", url);
    LLTextBox* note = getChild<LLTextBox>("gateway_note");
    if (!url.empty() && kj::gatewayUrl().empty())
    {
        note->setText(sw67viewer::statusText(sw67::a11y::StatusRole::Warning) +
                      "  Use an https:// address (http only for 127.0.0.1 or localhost).");
    }
    else
    {
        note->setText(url.empty() ? std::string() : sw67viewer::statusText(sw67::a11y::StatusRole::Success) + "  Saved.");
    }
    kj::reload();
}

void SW67FloaterKingJahmir::onSignIn()
{
    LLUICtrl* field = getChild<LLUICtrl>("token_input");
    const std::string tok = field->getValue().asString();
    field->setValue(LLSD(""));
    if (tok.size() < 20 || tok.size() > 4096 || std::count(tok.begin(), tok.end(), '.') != 2)
    {
        getChild<LLTextBox>("account_status_text")
            ->setText(sw67viewer::statusText(sw67::a11y::StatusRole::Warning) + "  That is not a 6|7 session token.");
        return;
    }
    kj::setToken(tok);
}

void SW67FloaterKingJahmir::onSignOut() { kj::setToken(std::string()); }

void SW67FloaterKingJahmir::refreshPreferences(const std::map<std::string, std::string>& prefs)
{
    LLScrollListCtrl* list = getChild<LLScrollListCtrl>("pref_list");
    list->deleteAllItems();
    for (const auto& [k, v] : prefs)
    {
        LLSD row;
        row["value"] = k;
        row["columns"][0]["column"] = "key";
        row["columns"][0]["value"] = k;
        row["columns"][1]["column"] = "value";
        row["columns"][1]["value"] = v;
        list->addElement(row);
    }
}

void SW67FloaterKingJahmir::onSavePreference()
{
    const std::string key = getChild<LLUICtrl>("pref_key_input")->getValue().asString();
    const std::string value = getChild<LLUICtrl>("pref_value_input")->getValue().asString();
    if (key.empty() || value.empty())
    {
        return;
    }
    kj::setPreference(key, value, [h = getDerivedHandle<SW67FloaterKingJahmir>()](std::map<std::string, std::string> p) {
        if (auto* self = h.get())
        {
            self->refreshPreferences(p);
        }
    });
}

void SW67FloaterKingJahmir::onDeletePreference()
{
    LLScrollListCtrl* list = getChild<LLScrollListCtrl>("pref_list");
    LLScrollListItem* item = list->getFirstSelected();
    if (!item)
    {
        return;
    }
    kj::deletePreference(item->getValue().asString(),
                         [h = getDerivedHandle<SW67FloaterKingJahmir>()](std::map<std::string, std::string> p) {
                             if (auto* self = h.get())
                             {
                                 self->refreshPreferences(p);
                             }
                         });
}

// ---- crown bubble --------------------------------------------------------------

SW67FloaterKingJahmirBubble::SW67FloaterKingJahmirBubble(const LLSD& key)
:   LLFloater(key)
{
}

bool SW67FloaterKingJahmirBubble::postBuild()
{
    getChild<LLUICtrl>("crown_btn")->setCommitCallback([](LLUICtrl*, const LLSD&) {
        LLFloater* panel = LLFloaterReg::findInstance("sw67_king_jahmir");
        if (panel && panel->getVisible() && !panel->isMinimized())
        {
            panel->setMinimized(true);
        }
        else
        {
            LLFloaterReg::showInstance("sw67_king_jahmir");
            if ((panel = LLFloaterReg::findInstance("sw67_king_jahmir")) != nullptr)
            {
                panel->setMinimized(false);
                panel->setFocus(true);
            }
        }
    });
    mChanged = kj::onChanged([this]() { refresh(); });
    return LLFloater::postBuild();
}

void SW67FloaterKingJahmirBubble::onOpen(const LLSD& key)
{
    LLFloater::onOpen(key);
    placeClearOfControls();
    refresh();
}

void SW67FloaterKingJahmirBubble::placeClearOfControls()
{
    if (!gFloaterView)
    {
        return;
    }
    // First time: the right edge, above the bottom toolbar, where no viewer
    // control sits. After that, wherever the user dragged it (save_rect),
    // kept on screen.
    const LLRect snap = gFloaterView->getSnapRect();
    if (!gSavedSettings.getBOOL("SW67KingJahmirBubblePlaced"))
    {
        const S32 margin = 16;
        setOrigin(snap.mRight - getRect().getWidth() - margin, snap.mBottom + margin + 48);
        gSavedSettings.setBOOL("SW67KingJahmirBubblePlaced", true);
    }
    gFloaterView->adjustToFitScreen(this, false);
}

void SW67FloaterKingJahmirBubble::refresh()
{
    const std::string unread = kj::conversation().unreadLabel();
    LLTextBox* badge = getChild<LLTextBox>("unread_text");
    badge->setText(unread);
    badge->setVisible(!unread.empty());
    LLUICtrl* btn = getChild<LLUICtrl>("crown_btn");
    std::string label = "King Jahmir, your 6|7 AI guide. Press to open or minimize the conversation.";
    if (!unread.empty())
    {
        label = "King Jahmir: " + unread + " replies. " + label;
    }
    if (kj::conversation().waiting())
    {
        label += " He is answering.";
    }
    btn->setToolTip(label);
}
