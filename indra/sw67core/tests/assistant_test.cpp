/**
 * @file assistant_test.cpp
 * @brief King Jahmir: reply rendering, protocol and conversation state.
 *
 * Copyright (c) 2026, 6|7 World Studio contributors. LGPL-2.1.
 */

#include "sw67/assistant/chat.h"

#include <catch2/catch_test_macros.hpp>

#include <random>
#include <set>

using namespace sw67::assistant;

namespace
{
bool known(std::string_view id) { return id == "sw67_workspaces" || id == "preferences"; }
} // namespace

TEST_CASE("Replies render numbered steps, bullets, bold and app links", "[assistant]")
{
    const auto blocks = parseReply("Here is how:\n\n1. Open [Workspace Profiles](screen:sw67_workspaces)\n"
                                   "2. Click **Preview**\n   then Apply\n\n- See [RLV help](help:rlv)",
                                   known);
    REQUIRE(blocks.size() == 4);
    CHECK(blocks[0].kind == Block::Kind::Paragraph);
    CHECK(blocks[1].kind == Block::Kind::Numbered);
    CHECK(blocks[1].number == 1);
    REQUIRE(blocks[1].spans.size() == 2);
    CHECK(blocks[1].spans[1].kind == Span::Kind::ScreenLink);
    CHECK(blocks[1].spans[1].target == "sw67_workspaces");
    CHECK(blocks[1].spans[1].text == "Workspace Profiles");
    CHECK(blocks[2].number == 2);
    CHECK(blocks[2].spans[1].kind == Span::Kind::Bold);
    CHECK(plainText({blocks[2]}) == "2. Click Preview then Apply");
    CHECK(blocks[3].kind == Block::Kind::Bulleted);
    CHECK(blocks[3].spans[1].kind == Span::Kind::HelpLink);
    CHECK(blocks[3].spans[1].target == "rlv");
}

TEST_CASE("Only known screens and help ids become links", "[assistant]")
{
    const auto blocks = parseReply("[a](screen:god_tools) [b](https://evil.example) [c](javascript:x) "
                                   "[d](help:../etc) [e](screen:Preferences) [f](screen:preferences)",
                                   known);
    REQUIRE(blocks.size() == 1);
    int links = 0;
    for (const Span& s : blocks[0].spans)
    {
        if (s.kind == Span::Kind::ScreenLink || s.kind == Span::Kind::HelpLink)
        {
            ++links;
            CHECK(s.target == "preferences");
        }
    }
    CHECK(links == 1);
    const std::string plain = plainText(blocks);
    CHECK(plain.find("https://") == std::string::npos);
    CHECK(plain.find("javascript") == std::string::npos);
    CHECK(plain == "a b c d e f");
}

TEST_CASE("Control characters and bidi overrides are dropped", "[assistant]")
{
    const auto blocks = parseReply("safe\x1b[31m text\xe2\x80\xae reversed\xe2\x81\xa6 x\x07", known);
    CHECK(plainText(blocks) == "safe[31m text reversed x");
    CHECK(parseReply(std::string(40000, 'a'), known)[0].spans[0].text.size() <= 16u * 1024u);
    CHECK(parseReply("", known).empty());
    CHECK(parseReply("\xe2\x80", known).empty()); // truncated UTF-8
}

TEST_CASE("Chat body leaves screen context out unless contextual help is on", "[assistant]")
{
    ChatRequest r;
    r.message = "  Explain this screen  ";
    r.screen = "sw67_workspaces";
    r.feature = "workspace_profiles";
    r.viewerVersion = "26.4.0.1";
    auto body = chatBody(r);
    CHECK(body["message"].asString() == "Explain this screen");
    CHECK_FALSE(body.has("context"));
    CHECK_FALSE(body.has("conversation_id"));
    CHECK(body["flags"]["contextual"].asBool(true) == false);
    r.flags.contextual = true;
    r.conversationId = "conv_abc";
    body = chatBody(r);
    CHECK(body["context"]["screen"].asString() == "sw67_workspaces");
    CHECK(body["context"]["feature"].asString() == "workspace_profiles");
    CHECK(body["conversation_id"].asString() == "conv_abc");
    r.screen = "../bad";
    CHECK_FALSE(chatBody(r).has("context"));
}

TEST_CASE("Gateway replies are read and validated", "[assistant]")
{
    const auto res = parseChatResponse(200, R"({"conversation_id":"conv_x1","reply":"Hi","development_mode":true,
        "links":[{"type":"screen","id":"sw67_workspaces","label":"W"},{"type":"screen","id":"../x","label":"bad"},
                 {"type":"help","id":"rlv","label":"RLV"}],
        "actions":[{"type":"open_screen","screen":"preferences","title":"Preferences"},
                   {"type":"confirm_action","action_id":"act_abcdEFGH1234","kind":"support_request",
                    "title":"Send this support request?","preview":"[bug] x"},
                   {"type":"confirm_action","action_id":"act_<script>","kind":"x"},
                   {"type":"purchase","id":"x"}]})");
    REQUIRE(res.reply);
    CHECK_FALSE(res.error);
    CHECK(res.reply->conversationId == "conv_x1");
    CHECK(res.reply->developmentMode);
    REQUIRE(res.reply->links.size() == 2);
    CHECK(res.reply->links[1].id == "rlv");
    REQUIRE(res.reply->actions.size() == 2);
    CHECK(res.reply->actions[0].kind == Action::Kind::OpenScreen);
    CHECK(res.reply->actions[1].kind == Action::Kind::Confirm);
    CHECK(res.reply->actions[1].id == "act_abcdEFGH1234");
}

TEST_CASE("Service failures give useful messages and say whether to retry", "[assistant]")
{
    auto r = parseChatResponse(503, R"({"error":{"code":"ai_busy","message":"King Jahmir is busy right now.",
                                        "retryable":true,"retry_after":20}})");
    REQUIRE(r.error);
    auto v = describe(*r.error);
    CHECK(v.canRetry);
    CHECK(v.text.find("busy") != std::string::npos);
    CHECK(v.text.find("20 s") != std::string::npos);

    v = describe(parseError(0, ""));
    CHECK(v.canRetry);
    CHECK(v.text.find("can't be reached") != std::string::npos);

    v = describe(parseError(401, R"({"error":{"code":"token_expired","message":"x","retryable":false}})"));
    CHECK_FALSE(v.canRetry);
    CHECK(v.role == sw67::a11y::StatusRole::Error);

    // A rejected sign-in says so, rather than asking to sign in again.
    v = describe(parseError(401, R"({"error":{"code":"invalid_token","message":"x","retryable":false}})"));
    CHECK(v.text.find("did not accept your sign-in") != std::string::npos);
    v = describe(parseError(401, R"({"error":{"code":"not_signed_in","message":"x","retryable":false}})"));
    CHECK(v.text.find("Sign in to 6|7") != std::string::npos);

    v = describe(parseError(502, "<html>proxy error</html>"));
    CHECK(v.text.find("html") == std::string::npos);
    CHECK(v.canRetry);

    r = parseChatResponse(200, "not json");
    REQUIRE(r.error);
    CHECK(r.error->code == "bad_response");
}

TEST_CASE("Conversation keeps a retried message once and counts unread replies", "[assistant]")
{
    Conversation c;
    CHECK_FALSE(c.send("   ", "key-00000001"));
    REQUIRE(c.send("Why didn't my shower work?", "key-00000001"));
    CHECK(c.waiting());
    CHECK_FALSE(c.send("second", "key-00000002")); // one at a time

    ServiceError timeout;
    timeout.code = "ai_timeout";
    timeout.retryable = true;
    c.failed(timeout);
    REQUIRE(c.waiting()); // kept for Retry, same key
    CHECK(c.outstanding()->second == "key-00000001");
    CHECK(c.messages().size() == 1);

    ChatReply reply;
    reply.conversationId = "conv_1";
    reply.text = "Records are UNAVAILABLE.";
    c.received(reply, false);
    CHECK_FALSE(c.waiting());
    CHECK(c.id() == "conv_1");
    CHECK(c.messages().size() == 2);
    CHECK(c.unread() == 1);
    CHECK(c.unreadLabel() == "1 new");
    c.markRead();
    CHECK(c.unreadLabel().empty());

    REQUIRE(c.send("again", "key-00000003"));
    ServiceError fatal;
    fatal.code = "ai_not_configured";
    fatal.retryable = false;
    c.failed(fatal);
    CHECK_FALSE(c.waiting());
    CHECK(c.messages().size() == 2); // the unanswered message is withdrawn
}

TEST_CASE("Confirm cards are tracked until resolved", "[assistant]")
{
    Conversation c;
    REQUIRE(c.send("file a ticket", "key-00000009"));
    ChatReply reply;
    reply.text = "Press Confirm to send it.";
    Action a;
    a.kind = Action::Kind::Confirm;
    a.id = "act_abcdefgh";
    reply.actions.push_back(a);
    Action open;
    open.kind = Action::Kind::OpenScreen;
    open.id = "preferences";
    reply.actions.push_back(open);
    c.received(reply, true);
    REQUIRE(c.pendingActions().size() == 1);
    c.resolveAction("act_abcdefgh");
    CHECK(c.pendingActions().empty());
    c.clear();
    CHECK(c.messages().empty());
}

TEST_CASE("Idempotency keys are long, URL-safe and distinct", "[assistant]")
{
    std::mt19937_64 rng(7);
    std::set<std::string> keys;
    for (int i = 0; i < 1000; ++i)
    {
        const std::string k = newIdempotencyKey([&] { return rng(); });
        CHECK(k.size() == 24);
        CHECK(k.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_") ==
              std::string::npos);
        keys.insert(k);
    }
    CHECK(keys.size() == 1000);
    CHECK(defaultStarters().size() == 6);
    CHECK(kGreeting.substr(0, 45) == "Welcome! I'm King Jahmir, your 6|7 AI guide. ");
}
