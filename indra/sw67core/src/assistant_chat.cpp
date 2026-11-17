/**
 * @file assistant_chat.cpp
 * @brief King Jahmir protocol, reply rendering and conversation state.
 *
 * Copyright (c) 2026, 6|7 World Studio contributors.
 *
 * The source code in this file is provided to you under the terms of the
 * GNU Lesser General Public License, version 2.1, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE. Terms of the LGPL can be found in doc/LGPL-license.txt
 * in this distribution, or online at http://www.gnu.org/licenses/lgpl-2.1.txt
 */

#include "sw67/assistant/chat.h"

#include <algorithm>
#include <cctype>

namespace sw67::assistant
{
namespace
{
constexpr std::size_t kMaxReplyBytes = 16u * 1024u;
constexpr std::size_t kMaxLinks = 16;
constexpr std::size_t kMaxActions = 8;

bool idChars(std::string_view id, bool allowDash)
{
    if (id.empty() || id.size() > 64)
    {
        return false;
    }
    for (char c : id)
    {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || (allowDash && c == '-');
        if (!ok)
        {
            return false;
        }
    }
    return true;
}

/// "act_" and 8 to 40 URL-safe characters, as the gateway makes them.
bool validActionId(std::string_view id)
{
    if (id.size() < 12 || id.size() > 44 || id.substr(0, 4) != "act_")
    {
        return false;
    }
    return id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-", 4) ==
           std::string_view::npos;
}

/// Drops C0 controls (except newline and tab), DEL, and the bidirectional
/// overrides and isolates (U+202A-202E, U+2066-2069) that can make text read
/// differently from what it is. Cuts at a character boundary.
std::string cleanText(std::string_view in, std::size_t maxBytes)
{
    std::string out;
    out.reserve(std::min(in.size(), maxBytes));
    for (std::size_t i = 0; i < in.size();)
    {
        const auto c = static_cast<unsigned char>(in[i]);
        std::size_t len = 1;
        if (c >= 0xF0)
        {
            len = 4;
        }
        else if (c >= 0xE0)
        {
            len = 3;
        }
        else if (c >= 0xC0)
        {
            len = 2;
        }
        if (i + len > in.size())
        {
            break;
        }
        if (out.size() + len > maxBytes)
        {
            break;
        }
        if (len == 1)
        {
            if ((c < 0x20 && c != '\n' && c != '\t') || c == 0x7F)
            {
                ++i;
                continue;
            }
            if (c == '\r')
            {
                ++i;
                continue;
            }
        }
        else if (len == 3 && c == 0xE2)
        {
            const auto c1 = static_cast<unsigned char>(in[i + 1]);
            const auto c2 = static_cast<unsigned char>(in[i + 2]);
            const bool bidi = (c1 == 0x80 && c2 >= 0xAA && c2 <= 0xAE) || (c1 == 0x81 && c2 >= 0xA6 && c2 <= 0xA9);
            if (bidi)
            {
                i += 3;
                continue;
            }
        }
        out.append(in.substr(i, len));
        i += len;
    }
    return out;
}

std::string_view trim(std::string_view s)
{
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t'))
    {
        s.remove_prefix(1);
    }
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t'))
    {
        s.remove_suffix(1);
    }
    return s;
}

void pushText(std::vector<Span>& spans, Span::Kind kind, std::string_view text)
{
    if (text.empty())
    {
        return;
    }
    if (!spans.empty() && spans.back().kind == kind && kind != Span::Kind::ScreenLink && kind != Span::Kind::HelpLink)
    {
        spans.back().text.append(text);
        return;
    }
    spans.push_back(Span{kind, std::string(text), {}});
}

/// Inline markup: **bold** and [label](screen:id) / [label](help:id).
void parseInline(std::string_view line, const std::function<bool(std::string_view)>& isKnownScreen,
                 std::vector<Span>& spans)
{
    bool bold = false;
    std::size_t i = 0;
    std::size_t textStart = 0;
    auto flush = [&](std::size_t end) {
        pushText(spans, bold ? Span::Kind::Bold : Span::Kind::Text, line.substr(textStart, end - textStart));
    };
    while (i < line.size())
    {
        if (line.compare(i, 2, "**") == 0)
        {
            flush(i);
            bold = !bold;
            i += 2;
            textStart = i;
            continue;
        }
        if (line[i] == '[')
        {
            const std::size_t close = line.find(']', i + 1);
            if (close != std::string_view::npos && close + 1 < line.size() && line[close + 1] == '(')
            {
                const std::size_t end = line.find(')', close + 2);
                if (end != std::string_view::npos)
                {
                    const std::string_view label = trim(line.substr(i + 1, close - i - 1));
                    const std::string_view target = line.substr(close + 2, end - close - 2);
                    flush(i);
                    Span span{Span::Kind::Text, std::string(label), {}};
                    if (target.substr(0, 7) == "screen:" && validScreenId(target.substr(7)) &&
                        isKnownScreen && isKnownScreen(target.substr(7)))
                    {
                        span.kind = Span::Kind::ScreenLink;
                        span.target = std::string(target.substr(7));
                    }
                    else if (target.substr(0, 5) == "help:" && validHelpId(target.substr(5)))
                    {
                        span.kind = Span::Kind::HelpLink;
                        span.target = std::string(target.substr(5));
                    }
                    if (!label.empty())
                    {
                        if (span.kind == Span::Kind::Text)
                        {
                            pushText(spans, bold ? Span::Kind::Bold : Span::Kind::Text, label);
                        }
                        else
                        {
                            spans.push_back(std::move(span));
                        }
                    }
                    i = end + 1;
                    textStart = i;
                    continue;
                }
            }
        }
        ++i;
    }
    flush(line.size());
}

/// "12. text" -> 12, else 0.
int numberedPrefix(std::string_view line, std::size_t& textAt)
{
    std::size_t i = 0;
    int n = 0;
    while (i < line.size() && i < 3 && std::isdigit(static_cast<unsigned char>(line[i])))
    {
        n = n * 10 + (line[i] - '0');
        ++i;
    }
    if (i == 0 || i >= line.size() || (line[i] != '.' && line[i] != ')') || i + 1 >= line.size() || line[i + 1] != ' ')
    {
        return 0;
    }
    textAt = i + 2;
    return n;
}

std::string str(const io::Json& j, std::string_view key, std::size_t maxBytes)
{
    const io::Json& v = j[key];
    return v.isString() ? cleanText(v.asString(), maxBytes) : std::string();
}
} // namespace

std::vector<std::string> defaultStarters()
{
    return {"Explain my stats",          "How do I use this feature?", "Help me build furniture",
            "Show my next level requirements", "Troubleshoot a problem", "What can I do here?"};
}

bool validScreenId(std::string_view id) { return idChars(id, false); }
bool validHelpId(std::string_view id) { return idChars(id, true) && id.front() != '-'; }

std::vector<Block> parseReply(std::string_view raw, const std::function<bool(std::string_view)>& isKnownScreen)
{
    const std::string text = cleanText(raw, kMaxReplyBytes);
    std::vector<Block> blocks;
    bool continuing = false; // the previous line belongs to an open block
    std::size_t pos = 0;
    while (pos <= text.size())
    {
        std::size_t nl = text.find('\n', pos);
        if (nl == std::string::npos)
        {
            nl = text.size();
        }
        std::string_view line = trim(std::string_view(text).substr(pos, nl - pos));
        pos = nl + 1;
        if (line.empty())
        {
            continuing = false;
            if (nl == text.size())
            {
                break;
            }
            continue;
        }
        std::size_t textAt = 0;
        const int number = numberedPrefix(line, textAt);
        Block block;
        if (number > 0)
        {
            block.kind = Block::Kind::Numbered;
            block.number = number;
            line.remove_prefix(textAt);
        }
        else if (line.size() > 2 && (line[0] == '-' || line[0] == '*') && line[1] == ' ')
        {
            block.kind = Block::Kind::Bulleted;
            line.remove_prefix(2);
        }
        else if (line.front() == '#')
        {
            // Headings read as bold paragraphs.
            while (!line.empty() && line.front() == '#')
            {
                line.remove_prefix(1);
            }
            line = trim(line);
            block.spans.push_back(Span{Span::Kind::Bold, std::string(line), {}});
            blocks.push_back(std::move(block));
            continuing = false;
            if (nl == text.size())
            {
                break;
            }
            continue;
        }
        else if (continuing && !blocks.empty())
        {
            // A wrapped line continues the block above it.
            pushText(blocks.back().spans, Span::Kind::Text, " ");
            parseInline(line, isKnownScreen, blocks.back().spans);
            if (nl == text.size())
            {
                break;
            }
            continue;
        }
        parseInline(line, isKnownScreen, block.spans);
        blocks.push_back(std::move(block));
        continuing = true;
        if (nl == text.size())
        {
            break;
        }
    }
    return blocks;
}

std::string plainText(const std::vector<Block>& blocks)
{
    std::string out;
    for (const Block& b : blocks)
    {
        if (!out.empty())
        {
            out += b.kind == Block::Kind::Paragraph ? "\n\n" : "\n";
        }
        if (b.kind == Block::Kind::Numbered)
        {
            out += std::to_string(b.number) + ". ";
        }
        else if (b.kind == Block::Kind::Bulleted)
        {
            out += "- ";
        }
        for (const Span& s : b.spans)
        {
            out += s.text;
        }
    }
    return out;
}

io::Json chatBody(const ChatRequest& r)
{
    io::Json body = io::Json::object();
    body.set("message", std::string(trim(r.message)));
    if (!r.conversationId.empty())
    {
        body.set("conversation_id", r.conversationId);
    }
    io::Json flags = io::Json::object();
    flags.set("contextual", r.flags.contextual);
    flags.set("account_help", r.flags.accountHelp);
    flags.set("memory", r.flags.memory);
    body.set("flags", flags);
    if (r.flags.contextual && validScreenId(r.screen))
    {
        io::Json ctx = io::Json::object();
        ctx.set("screen", r.screen);
        if (validScreenId(r.feature))
        {
            ctx.set("feature", r.feature);
        }
        body.set("context", ctx);
    }
    io::Json client = io::Json::object();
    client.set("viewer_version", r.viewerVersion.substr(0, 40));
    body.set("client", client);
    return body;
}

ServiceError parseError(int httpStatus, std::string_view body)
{
    ServiceError e;
    e.httpStatus = httpStatus;
    io::JsonLimits limits;
    limits.maxBytes = 64u * 1024u;
    if (auto j = io::parseJson(body, nullptr, limits); j && j->isObject() && (*j)["error"].isObject())
    {
        const io::Json& err = (*j)["error"];
        e.code = str(err, "code", 64);
        e.message = str(err, "message", 300);
        e.retryable = err["retryable"].asBool(httpStatus == 0 || httpStatus >= 500 || httpStatus == 429);
        e.retryAfter = std::clamp(err["retry_after"].asNumber(0.0), 0.0, 86400.0);
        return e;
    }
    if (httpStatus == 0)
    {
        e.code = "unreachable";
    }
    else if (httpStatus == 401)
    {
        e.code = "not_signed_in";
        e.retryable = false;
    }
    else if (httpStatus == 429)
    {
        e.code = "rate_limited";
    }
    else if (httpStatus >= 500)
    {
        e.code = "server_error";
    }
    else
    {
        e.code = "bad_response";
        e.retryable = false;
    }
    return e;
}

ChatResult parseChatResponse(int httpStatus, std::string_view body)
{
    ChatResult result;
    if (httpStatus < 200 || httpStatus > 299)
    {
        result.error = parseError(httpStatus, body);
        return result;
    }
    io::JsonLimits limits;
    limits.maxBytes = 256u * 1024u;
    const auto j = io::parseJson(body, nullptr, limits);
    if (!j || !j->isObject() || !(*j)["reply"].isString())
    {
        ServiceError e;
        e.httpStatus = httpStatus;
        e.code = "bad_response";
        e.retryable = true;
        result.error = e;
        return result;
    }
    ChatReply reply;
    reply.conversationId = str(*j, "conversation_id", 64);
    reply.text = cleanText((*j)["reply"].asString(), kMaxReplyBytes);
    reply.developmentMode = (*j)["development_mode"].asBool(false);
    reply.replayed = (*j)["replayed"].asBool(false);
    if ((*j)["links"].isArray())
    {
        for (const io::Json& l : (*j)["links"].items())
        {
            if (reply.links.size() >= kMaxLinks)
            {
                break;
            }
            Link link;
            const std::string type = str(l, "type", 16);
            link.screen = type == "screen";
            link.id = str(l, "id", 64);
            link.label = str(l, "label", 100);
            if ((link.screen && validScreenId(link.id)) || (type == "help" && validHelpId(link.id)))
            {
                reply.links.push_back(std::move(link));
            }
        }
    }
    if ((*j)["actions"].isArray())
    {
        for (const io::Json& a : (*j)["actions"].items())
        {
            if (reply.actions.size() >= kMaxActions)
            {
                break;
            }
            const std::string type = str(a, "type", 32);
            Action action;
            if (type == "open_screen")
            {
                action.kind = Action::Kind::OpenScreen;
                action.id = str(a, "screen", 64);
                action.title = str(a, "title", 100);
                if (!validScreenId(action.id))
                {
                    continue;
                }
            }
            else if (type == "confirm_action")
            {
                action.kind = Action::Kind::Confirm;
                action.id = str(a, "action_id", 64);
                action.title = str(a, "title", 120);
                action.preview = str(a, "preview", 2400);
                action.confirmKind = str(a, "kind", 32);
                if (!validActionId(action.id))
                {
                    continue;
                }
            }
            else
            {
                continue; // a type this viewer does not know does nothing
            }
            reply.actions.push_back(std::move(action));
        }
    }
    result.reply = std::move(reply);
    return result;
}

ErrorView describe(const ServiceError& e)
{
    ErrorView v;
    v.canRetry = e.retryable;
    const std::string& c = e.code;
    if (c == "unreachable")
    {
        v.text = "King Jahmir can't be reached right now. Check your connection, then press Retry.";
    }
    else if (c == "not_signed_in" || c == "invalid_token" || c == "token_expired")
    {
        v.role = a11y::StatusRole::Error;
        v.text = c == "token_expired" ? "Your 6|7 session has expired. Sign in again in King Jahmir settings."
                                      : "Sign in to 6|7 to talk with King Jahmir (King Jahmir settings).";
        v.canRetry = false;
    }
    else if (c == "rate_limited" || c == "busy" || c == "ai_busy" || c == "in_progress")
    {
        v.text = "King Jahmir is busy. Wait a moment, then press Retry.";
    }
    else if (c == "ai_not_configured")
    {
        v.role = a11y::StatusRole::Error;
        v.text = "King Jahmir's AI service isn't set up on the server yet.";
        v.canRetry = false;
    }
    else if (c == "conversation_full")
    {
        v.role = a11y::StatusRole::Info;
        v.text = "This conversation is full. Press New conversation to keep going.";
        v.canRetry = false;
    }
    else if (!e.message.empty())
    {
        v.text = e.message;
    }
    else
    {
        v.text = "King Jahmir is unavailable right now.";
    }
    if (v.canRetry && e.retryAfter >= 1.0)
    {
        v.text += " (try again in about " + std::to_string(static_cast<int>(e.retryAfter + 0.5)) + " s)";
    }
    return v;
}

std::string newIdempotencyKey(const std::function<std::uint64_t()>& random)
{
    static constexpr char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string key;
    key.reserve(24);
    while (key.size() < 24)
    {
        std::uint64_t r = random();
        for (int i = 0; i < 10 && key.size() < 24; ++i)
        {
            key += kAlphabet[r & 63u];
            r >>= 6;
        }
    }
    return key;
}

// ---- Conversation ------------------------------------------------------------

bool Conversation::send(std::string text, std::string key, std::size_t maxChars)
{
    const std::string_view t = trim(text);
    if (mOutstanding || t.empty() || t.size() > maxChars || key.size() < 8)
    {
        return false;
    }
    std::string clean = cleanText(t, maxChars);
    mMessages.push_back(Message{Message::Role::User, clean});
    mOutstanding = std::make_pair(std::move(clean), std::move(key));
    return true;
}

void Conversation::received(const ChatReply& reply, bool panelVisible)
{
    mOutstanding.reset();
    if (!reply.conversationId.empty())
    {
        mId = reply.conversationId;
    }
    mMessages.push_back(Message{Message::Role::Assistant, reply.text});
    for (const Action& a : reply.actions)
    {
        if (a.kind == Action::Kind::Confirm)
        {
            mActions.push_back(a);
        }
    }
    if (!panelVisible)
    {
        ++mUnread;
    }
}

void Conversation::failed(const ServiceError& error)
{
    if (!error.retryable)
    {
        abandon();
    }
}

void Conversation::abandon()
{
    if (!mOutstanding)
    {
        return;
    }
    // The unanswered message leaves the transcript with it.
    for (auto it = mMessages.rbegin(); it != mMessages.rend(); ++it)
    {
        if (it->role == Message::Role::User)
        {
            mMessages.erase(std::next(it).base());
            break;
        }
    }
    mOutstanding.reset();
}

void Conversation::addNotice(std::string text) { mMessages.push_back(Message{Message::Role::Notice, std::move(text)}); }

void Conversation::load(std::string id, std::vector<Message> messages)
{
    clear();
    mId = std::move(id);
    mMessages = std::move(messages);
}

void Conversation::clear()
{
    mId.clear();
    mMessages.clear();
    mOutstanding.reset();
    mActions.clear();
    mUnread = 0;
}

std::string Conversation::unreadLabel() const
{
    if (mUnread <= 0)
    {
        return {};
    }
    return (mUnread > 99 ? std::string("99+") : std::to_string(mUnread)) + " new";
}

void Conversation::resolveAction(const std::string& actionId)
{
    mActions.erase(std::remove_if(mActions.begin(), mActions.end(),
                                  [&](const Action& a) { return a.id == actionId; }),
                   mActions.end());
}

} // namespace sw67::assistant
