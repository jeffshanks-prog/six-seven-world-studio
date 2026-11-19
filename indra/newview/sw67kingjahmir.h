/**
 * @file sw67kingjahmir.h
 * @brief King Jahmir, the 6|7 AI guide: the viewer's connection to the King
 *        Jahmir gateway (services/king-jahmir) and the conversation both
 *        King Jahmir windows show.
 *
 * The viewer holds no AI key. It sends the resident's message, with their
 * 6|7 session token, to the gateway named by SW67KingJahmirGatewayURL; the
 * gateway checks the token, runs the AI and its tools, and answers. The
 * token is kept in the viewer's protected data store, never in settings.
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

#include "sw67/a11y/profile.h"
#include "sw67/assistant/chat.h"

#include <boost/signals2.hpp>

#include <functional>
#include <map>
#include <string>
#include <vector>

namespace sw67viewer::kingjahmir
{
/// The one conversation both windows show. Main thread only.
sw67::assistant::Conversation& conversation();

/// Fires on the main thread whenever the conversation, status or settings
/// change, so the windows redraw.
boost::signals2::connection onChanged(const std::function<void()>& cb);

struct Status
{
    sw67::a11y::StatusRole role = sw67::a11y::StatusRole::Neutral;
    std::string text;
    bool canRetry = false;
};
const Status& status();

/// What the gateway said at start: greeting, starters, development mode.
struct Bootstrap
{
    bool loaded = false;
    std::string greeting;
    std::vector<std::string> starters;
    bool developmentMode = false;
    bool mockModel = false;
    int retentionDays = 0;
    bool owner = false;
};
const Bootstrap& bootstrap();

/// The gateway's base URL when it is set and acceptable (https, or http to
/// 127.0.0.1 or localhost for development); otherwise empty.
std::string gatewayUrl();
bool signedIn();
/// Stores the 6|7 session token in the protected data store. Empty signs out.
void setToken(const std::string& token);

/// Reloads the gateway's greeting after its address or the sign-in changed.
void reload();

/// Sends a message. False when nothing was sent (empty, too long, another
/// message still waiting, not set up); the status says why.
bool send(const std::string& text);
/// Sends the waiting message again, with the same Idempotency-Key, so the
/// gateway answers it once however often it is retried.
void retry();
/// Gives up on the waiting message.
void abandon();
void newConversation();

/// Past conversations: id -> title, newest first, via the callback.
void listConversations(std::function<void(std::vector<std::pair<std::string, std::string>>)> done);
void openConversation(const std::string& id);
/// Deletes the current conversation on the gateway, then clears it here.
void deleteConversation();
void deleteAllConversations();

/// The user pressed Confirm or Cancel on a draft (support request, preference).
void resolveAction(const std::string& actionId, bool confirm);

void fetchPreferences(std::function<void(std::map<std::string, std::string>)> done);
void setPreference(const std::string& key, const std::string& value,
                   std::function<void(std::map<std::string, std::string>)> done);
void deletePreference(const std::string& key, std::function<void(std::map<std::string, std::string>)> done);

/// A draft LSL script from the gateway, for the Creator Workbench. Text only:
/// nothing here compiles, saves, uploads or runs it.
struct LslDraft
{
    bool ok = false;
    std::string code;
    std::string notes;
    std::string draftNote; ///< "Draft only: not compiled or tested..."
    std::string error;     ///< When !ok: a plain sentence.
};
/// Asks for a draft. False (and `done` is not called) when not set up or
/// signed in; `whyNot` says why. One draft at a time.
bool draftLsl(const std::string& request, const std::string& existingScript,
              std::function<void(const LslDraft&)> done, std::string& whyNot);

/// One request to the 6|7 gateway with the 6|7 sign-in, for the other 6|7
/// windows (the Phone Companion). `done` runs on the main thread with the
/// answer's JSON body, or with a plain sentence in `error`.
struct GatewayReply
{
    bool ok = false;
    int status = 0;
    std::string body;
    std::string error;
};
bool callGateway(const std::string& method, const std::string& path, const std::string& body,
                 std::function<void(const GatewayReply&)> done, std::string& whyNot);

/// Registered windows replies may link to (doc/sw67/help/features.json).
bool isKnownScreen(std::string_view id);
/// The help page's address on the gateway, or empty.
std::string helpUrl(const std::string& helpId);

/// The window in front other than King Jahmir's own, for "Explain this
/// screen" when the user turned contextual help on.
std::string frontScreen();

/// After login: shows the crown bubble when SW67KingJahmirBubble is on, and
/// loads the gateway's greeting.
void onLogin();
} // namespace sw67viewer::kingjahmir
