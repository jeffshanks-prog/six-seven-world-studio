/**
 * @file chat.h
 * @brief King Jahmir, the 6|7 AI guide: the viewer side of the gateway
 *        protocol, reply rendering and conversation state. No AI runs in
 *        the viewer and it holds no AI key; it talks to the King Jahmir
 *        gateway (services/king-jahmir) with the resident's 6|7 session
 *        token.
 *
 * Replies are untrusted text. parseReply() turns them into blocks of plain
 * text, bold text and two kinds of link only: an app window the caller
 * knows ("screen:<id>") and a help page ("help:<id>"). Everything else is
 * shown as the characters it is.
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
#include "sw67/io/json.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sw67::assistant
{

inline constexpr std::string_view kName = "King Jahmir";
/// Shown before the gateway answers, and when it cannot be reached.
inline constexpr std::string_view kGreeting =
    "Welcome! I'm King Jahmir, your 6|7 AI guide. Ask me how something works, get help with your avatar, or "
    "tell me what you want to build. What would you like to do?";
std::vector<std::string> defaultStarters();

// ---- reply rendering -------------------------------------------------------

struct Span
{
    enum class Kind
    {
        Text,
        Bold,
        ScreenLink, ///< target: a registered floater name
        HelpLink    ///< target: a help page id
    };
    Kind kind = Kind::Text;
    std::string text;
    std::string target;
};

struct Block
{
    enum class Kind
    {
        Paragraph,
        Numbered, ///< number holds the item's number
        Bulleted
    };
    Kind kind = Kind::Paragraph;
    int number = 0;
    std::vector<Span> spans;
};

/// True for an id made only of [a-z0-9_] (screens) or [a-z0-9_-] (help),
/// 1 to 64 characters.
bool validScreenId(std::string_view id);
bool validHelpId(std::string_view id);

/// Parses a reply. `isKnownScreen` decides which screen links become links;
/// any other link is shown as its label alone. Control characters and
/// bidirectional overrides are dropped. Input over 16 KB is cut.
std::vector<Block> parseReply(std::string_view text, const std::function<bool(std::string_view)>& isKnownScreen);

/// The blocks as plain text ("1. ", "- " prefixes, links as their labels),
/// for screen readers, copying and logs.
std::string plainText(const std::vector<Block>& blocks);

// ---- protocol --------------------------------------------------------------

struct Flags
{
    bool contextual = false;  ///< "Use this screen for help"
    bool accountHelp = false; ///< "Live account help"
    bool memory = false;      ///< "Remember my preferences"
};

struct ChatRequest
{
    std::string message;
    std::string conversationId; ///< empty: start a new conversation
    Flags flags;
    std::string screen;  ///< sent only when flags.contextual
    std::string feature; ///< sent only when flags.contextual
    std::string viewerVersion;
};

/// The JSON body of POST /v1/chat. Message is trimmed; the caller checks
/// it is not empty. Screen context is left out unless contextual help is on.
io::Json chatBody(const ChatRequest& request);

struct Link
{
    bool screen = false; ///< else a help page
    std::string id;
    std::string label;
};

struct Action
{
    enum class Kind
    {
        OpenScreen,
        Confirm
    };
    Kind kind = Kind::OpenScreen;
    std::string id;    ///< screen id, or the pending action's id
    std::string title; ///< window title, or the confirmation question
    std::string preview;
    std::string confirmKind; ///< "support_request", "preference"
};

struct ServiceError
{
    int httpStatus = 0; ///< 0: no answer at all
    std::string code;
    std::string message;
    bool retryable = true;
    double retryAfter = 0.0;
};

struct ChatReply
{
    std::string conversationId;
    std::string text;
    std::vector<Link> links;
    std::vector<Action> actions;
    bool developmentMode = false;
    bool replayed = false;
};

/// Reads a gateway answer. A 2xx with a valid body gives a reply; anything
/// else gives an error, with the gateway's own message when it sent one.
/// Fields are validated: unknown action types and bad ids are dropped.
struct ChatResult
{
    std::optional<ChatReply> reply;
    std::optional<ServiceError> error;
};
ChatResult parseChatResponse(int httpStatus, std::string_view body);

/// Reads any gateway error body ({"error": {...}}), or makes one from the
/// status alone.
ServiceError parseError(int httpStatus, std::string_view body);

/// What the panel shows for an error: a plain sentence and whether Retry
/// makes sense. Never shows raw bodies.
struct ErrorView
{
    a11y::StatusRole role = a11y::StatusRole::Warning;
    std::string text;
    bool canRetry = false;
};
ErrorView describe(const ServiceError& error);

/// A new key for the Idempotency-Key header: 24 URL-safe characters from
/// `random` (which must return uniformly distributed 64-bit values).
std::string newIdempotencyKey(const std::function<std::uint64_t()>& random);

// ---- conversation state ----------------------------------------------------

struct Message
{
    enum class Role
    {
        User,
        Assistant,
        Notice ///< status lines the app adds ("Support request sent")
    };
    Role role = Role::User;
    std::string text;
};

/// One conversation as the panel shows it. Not thread-safe: the main thread
/// owns it.
class Conversation
{
public:
    const std::string& id() const { return mId; }
    const std::vector<Message>& messages() const { return mMessages; }

    /// Queues a message. False (and nothing changes) while another is
    /// outstanding, or when the text is empty or too long.
    bool send(std::string text, std::string idempotencyKey, std::size_t maxChars = 4000);
    bool waiting() const { return mOutstanding.has_value(); }
    /// The outstanding message and its key, for (re)sending.
    const std::optional<std::pair<std::string, std::string>>& outstanding() const { return mOutstanding; }

    /// The answer arrived. `panelVisible`: the user can see it now; if not,
    /// it counts as unread.
    void received(const ChatReply& reply, bool panelVisible);
    /// The request failed. The message stays outstanding when Retry makes
    /// sense, and is withdrawn otherwise.
    void failed(const ServiceError& error);
    /// Withdraws the outstanding message (the user gave up on it).
    void abandon();

    void addNotice(std::string text);
    /// Replaces everything with history loaded from the gateway.
    void load(std::string id, std::vector<Message> messages);
    void clear();

    int unread() const { return mUnread; }
    void markRead() { mUnread = 0; }
    /// "", "1 new" or "N new": the count as words, never colour alone.
    std::string unreadLabel() const;

    const std::vector<Action>& pendingActions() const { return mActions; }
    void resolveAction(const std::string& actionId);

private:
    std::string mId;
    std::vector<Message> mMessages;
    std::optional<std::pair<std::string, std::string>> mOutstanding;
    std::vector<Action> mActions;
    int mUnread = 0;
};

} // namespace sw67::assistant
