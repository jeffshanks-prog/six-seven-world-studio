/**
 * @file sw67kingjahmir.cpp
 * @brief King Jahmir gateway client and shared conversation.
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

#include "sw67kingjahmir.h"

#include "llcorehttputil.h"
#include "llcoros.h"
#include "llfloater.h"
#include "llfloaterreg.h"
#include "llhttpconstants.h"
#include "llsecapi.h"
#include "lluri.h"
#include "llversioninfo.h"
#include "llviewercontrol.h"

#include <array>
#include <random>

using namespace sw67::assistant;
using sw67::a11y::StatusRole;
using sw67::io::Json;

namespace sw67viewer::kingjahmir
{
namespace
{
constexpr const char* kStoreType = "sw67_king_jahmir";
constexpr const char* kStoreId = "session";
constexpr F32 kConnectTimeout = 10.f;
constexpr F32 kTransferTimeout = 150.f; // a turn may take several model calls

// Windows a reply may link to: doc/sw67/help/features.json "screens".
constexpr std::array<std::string_view, 15> kScreens = {
    "sw67_king_jahmir", "sw67_accessibility", "sw67_workspaces", "sw67_performance", "sw67_creator_projects",
    "sw67_product_photo", "preferences", "performance", "lightbox", "snapshot", "inventory", "script_studio",
    "ao", "rlv_behaviours", "camera"};

// Which feature a window belongs to, for contextual help.
const std::map<std::string, std::string, std::less<>> kScreenFeature = {
    {"sw67_accessibility", "accessibility"},     {"sw67_workspaces", "workspace_profiles"},
    {"sw67_performance", "performance_assistant"}, {"sw67_creator_projects", "creator_projects"},
    {"sw67_product_photo", "product_photography"}, {"preferences", "viewer_settings"},
    {"script_studio", "script_studio"},           {"ao", "animation_overrider"},
    {"inventory", "inventory_basic"},             {"rlv_behaviours", "rlv"}};

Conversation sConversation;
Status sStatus;
Bootstrap sBootstrap;
boost::signals2::signal<void()> sChanged;
bool sBusy = false; // a non-chat request is running

void changed() { sChanged(); }

void setStatus(StatusRole role, std::string text, bool canRetry = false)
{
    sStatus = Status{role, std::move(text), canRetry};
    changed();
}

std::string token()
{
    if (gSecAPIHandler.isNull())
    {
        return {};
    }
    const LLSD v = gSecAPIHandler->getProtectedData(kStoreType, kStoreId);
    return v.isString() ? v.asString() : std::string();
}

std::uint64_t randomWord()
{
    static std::random_device rd;
    static std::mt19937_64 rng((static_cast<std::uint64_t>(rd()) << 32) ^ rd());
    return rng();
}

struct Answer
{
    int status = 0; ///< HTTP status, or 0 when nothing answered
    std::string body;
};

/// In a coroutine: one request to the gateway.
Answer call(const std::string& method, const std::string& path, const std::string& body,
            const std::string& idempotencyKey = {})
{
    Answer answer;
    const std::string base = gatewayUrl();
    const std::string tok = token();
    if (base.empty() || tok.empty())
    {
        return answer;
    }
    LLCore::HttpRequest::ptr_t request = std::make_shared<LLCore::HttpRequest>();
    auto adapter = std::make_shared<LLCoreHttpUtil::HttpCoroutineAdapter>(
        "SW67KingJahmir", LLCore::HttpRequest::DEFAULT_POLICY_ID,
        LLCoreHttpUtil::HttpCoroutineAdapter::Destination::Outside);
    LLCore::HttpOptions::ptr_t options = std::make_shared<LLCore::HttpOptions>();
    options->setTimeout(kConnectTimeout);
    options->setTransferTimeout(kTransferTimeout);
    options->setRetries(0); // Retry is the user's button, with the same key
    options->setSSLVerifyPeer(true);
    options->setSSLVerifyHost(true);
    options->setFollowRedirects(false);
    LLCore::HttpHeaders::ptr_t headers = std::make_shared<LLCore::HttpHeaders>();
    headers->append(HTTP_OUT_HEADER_ACCEPT, HTTP_CONTENT_JSON);
    headers->append("Authorization", "Bearer " + tok);
    if (!idempotencyKey.empty())
    {
        headers->append("Idempotency-Key", idempotencyKey);
    }

    LLSD reply;
    const std::string url = base + path;
    if (method == "GET")
    {
        reply = adapter->getRawAndSuspend(request, url, options, headers);
    }
    else
    {
        headers->append(HTTP_OUT_HEADER_CONTENT_TYPE, HTTP_CONTENT_JSON);
        LLCore::BufferArray::ptr_t raw(new LLCore::BufferArray);
        raw->append(body.data(), body.size());
        reply = adapter->postRawAndSuspend(request, url, raw, options, headers);
    }
    const LLSD& http = reply[LLCoreHttpUtil::HttpCoroutineAdapter::HTTP_RESULTS];
    const LLCore::HttpStatus status = LLCoreHttpUtil::HttpCoroutineAdapter::getStatusFromLLSD(http);
    if (status)
    {
        answer.status = 200;
        const LLSD::Binary& raw = reply[LLCoreHttpUtil::HttpCoroutineAdapter::HTTP_RESULTS_RAW].asBinary();
        answer.body.assign(raw.begin(), raw.end());
    }
    else
    {
        answer.status = status.isHttpStatus() ? static_cast<int>(status.getType()) : 0;
        answer.body = http["error_body"].asString();
        // Never the body: it may hold the user's text.
        LL_INFOS("SW67KingJahmir") << method << " " << path.substr(0, path.find('/', 4)) << " failed: "
                                   << status.toString() << LL_ENDL;
    }
    return answer;
}

std::optional<Json> okJson(const Answer& a)
{
    if (a.status < 200 || a.status > 299)
    {
        const ErrorView v = describe(parseError(a.status, a.body));
        setStatus(v.role, v.text, false);
        return std::nullopt;
    }
    auto j = sw67::io::parseJson(a.body);
    if (!j || !j->isObject())
    {
        setStatus(StatusRole::Warning, "King Jahmir sent an answer this viewer could not read.");
        return std::nullopt;
    }
    return j;
}

std::map<std::string, std::string> prefsFrom(const Json& j)
{
    std::map<std::string, std::string> out;
    if (j["preferences"].isObject())
    {
        for (const auto& [k, v] : j["preferences"].members())
        {
            if (v.isString())
            {
                out[k] = v.asString();
            }
        }
    }
    return out;
}

bool readyOrSay()
{
    if (gatewayUrl().empty())
    {
        setStatus(StatusRole::Info, "King Jahmir isn't set up yet: add the gateway address in his Settings tab.");
        return false;
    }
    if (!signedIn())
    {
        setStatus(StatusRole::Info, "Sign in to 6|7 in King Jahmir's Settings tab to start talking.");
        return false;
    }
    return true;
}

void sendOutstanding()
{
    const auto& out = sConversation.outstanding();
    if (!out)
    {
        return;
    }
    ChatRequest req;
    req.message = out->first;
    req.conversationId = sConversation.id();
    req.flags.contextual = gSavedSettings.getBOOL("SW67KingJahmirContextual");
    req.flags.accountHelp = gSavedSettings.getBOOL("SW67KingJahmirAccountHelp");
    req.flags.memory = gSavedSettings.getBOOL("SW67KingJahmirMemory");
    if (req.flags.contextual)
    {
        req.screen = frontScreen();
        if (auto it = kScreenFeature.find(req.screen); it != kScreenFeature.end())
        {
            req.feature = it->second;
        }
    }
    req.viewerVersion = LLVersionInfo::instance().getVersion();
    const std::string body = sw67::io::writeJson(chatBody(req));
    const std::string key = out->second;
    setStatus(StatusRole::Info, "King Jahmir is thinking...");

    LLCoros::instance().launch("SW67KingJahmir::chat", [body, key]() {
        const Answer a = call("POST", "/v1/chat", body, key);
        // The user may have started over while this was running.
        if (!sConversation.outstanding() || sConversation.outstanding()->second != key)
        {
            return;
        }
        const ChatResult r = parseChatResponse(a.status, a.body);
        if (r.reply)
        {
            LLFloater* panel = LLFloaterReg::findInstance("sw67_king_jahmir");
            const bool visible = panel && panel->getVisible() && !panel->isMinimized();
            sConversation.received(*r.reply, visible);
            setStatus(StatusRole::Success, r.reply->developmentMode ? "Answered (development mode)" : "Answered");
            return;
        }
        const ServiceError err = r.error.value_or(ServiceError{});
        sConversation.failed(err);
        const ErrorView v = describe(err);
        setStatus(v.role, v.text, v.canRetry && sConversation.waiting());
    });
}

void loadBootstrap()
{
    if (gatewayUrl().empty() || !signedIn())
    {
        return;
    }
    LLCoros::instance().launch("SW67KingJahmir::bootstrap", []() {
        const Answer a = call("GET", "/v1/bootstrap", {});
        if (a.status != 200)
        {
            return; // the first message will say what is wrong
        }
        auto j = sw67::io::parseJson(a.body);
        if (!j || !j->isObject())
        {
            return;
        }
        Bootstrap b;
        b.loaded = true;
        b.greeting = (*j)["greeting"].asString(std::string(kGreeting)).substr(0, 600);
        if ((*j)["starters"].isArray())
        {
            for (const Json& s : (*j)["starters"].items())
            {
                if (s.isString() && b.starters.size() < 8)
                {
                    b.starters.push_back(s.asString().substr(0, 60));
                }
            }
        }
        b.developmentMode = (*j)["development_mode"].asBool(false);
        b.mockModel = (*j)["mock_model"].asBool(false);
        b.retentionDays = (*j)["retention_days"].asInt(0);
        b.owner = (*j)["owner"].asBool(false);
        sBootstrap = std::move(b);
        changed();
    });
}

/// Runs a small request when no other is running; `then` gets the JSON.
void simple(const char* name, std::string method, std::string path, std::string body,
            std::function<void(const Json&)> then)
{
    if (!readyOrSay() || sBusy)
    {
        return;
    }
    sBusy = true;
    LLCoros::instance().launch(name, [method = std::move(method), path = std::move(path), body = std::move(body),
                                      then = std::move(then)]() {
        const Answer a = call(method, path, body);
        sBusy = false;
        if (auto j = okJson(a))
        {
            then(*j);
        }
    });
}
} // namespace

Conversation& conversation() { return sConversation; }
boost::signals2::connection onChanged(const std::function<void()>& cb) { return sChanged.connect(cb); }
const Status& status() { return sStatus; }
const Bootstrap& bootstrap() { return sBootstrap; }

std::string gatewayUrl()
{
    std::string url = gSavedSettings.getString("SW67KingJahmirGatewayURL");
    while (!url.empty() && url.back() == '/')
    {
        url.pop_back();
    }
    if (url.size() > 200 || url.find_first_of(" \t\r\n?#@") != std::string::npos)
    {
        return {};
    }
    if (url.rfind("https://", 0) == 0 && url.size() > 8)
    {
        return url;
    }
    for (const char* dev : {"http://127.0.0.1:", "http://localhost:"})
    {
        if (url.rfind(dev, 0) == 0)
        {
            return url;
        }
    }
    return {};
}

bool signedIn() { return !token().empty(); }

void setToken(const std::string& tok)
{
    if (gSecAPIHandler.isNull())
    {
        return;
    }
    if (tok.empty())
    {
        gSecAPIHandler->deleteProtectedData(kStoreType, kStoreId);
    }
    else
    {
        gSecAPIHandler->setProtectedData(kStoreType, kStoreId, LLSD(tok));
    }
    gSecAPIHandler->syncProtectedMap();
    setStatus(StatusRole::Neutral, tok.empty() ? "Signed out of King Jahmir." : "Signed in.");
    reload();
}

void reload()
{
    sBootstrap = Bootstrap{};
    changed();
    loadBootstrap();
}

bool send(const std::string& text)
{
    if (!readyOrSay())
    {
        return false;
    }
    if (sConversation.waiting())
    {
        setStatus(StatusRole::Info, "Wait for King Jahmir's answer, or press Retry.", sStatus.canRetry);
        return false;
    }
    const std::string key = newIdempotencyKey(randomWord);
    if (!sConversation.send(text, key))
    {
        setStatus(StatusRole::Warning, "Type a message of up to 4000 characters.");
        return false;
    }
    changed();
    sendOutstanding();
    return true;
}

void retry()
{
    if (readyOrSay() && sConversation.waiting())
    {
        sendOutstanding();
    }
}

void abandon()
{
    sConversation.abandon();
    setStatus(StatusRole::Neutral, "");
}

void newConversation()
{
    sConversation.clear();
    setStatus(StatusRole::Neutral, "New conversation.");
}

void listConversations(std::function<void(std::vector<std::pair<std::string, std::string>>)> done)
{
    simple("SW67KingJahmir::list", "GET", "/v1/conversations", {}, [done](const Json& j) {
        std::vector<std::pair<std::string, std::string>> out;
        if (j["conversations"].isArray())
        {
            for (const Json& c : j["conversations"].items())
            {
                const std::string id = c["id"].asString("");
                if (id.rfind("conv_", 0) == 0 && id.size() <= 45)
                {
                    out.emplace_back(id, c["title"].asString("(untitled)").substr(0, 80));
                }
            }
        }
        done(std::move(out));
    });
}

void openConversation(const std::string& id)
{
    if (id.rfind("conv_", 0) != 0 || id.size() > 45 || id.find_first_of("/?#%") != std::string::npos)
    {
        return;
    }
    simple("SW67KingJahmir::open", "GET", "/v1/conversations/" + id, {}, [id](const Json& j) {
        std::vector<Message> msgs;
        if (j["messages"].isArray())
        {
            for (const Json& m : j["messages"].items())
            {
                const std::string role = m["role"].asString("");
                msgs.push_back(Message{role == "user" ? Message::Role::User : Message::Role::Assistant,
                                       m["text"].asString("")});
            }
        }
        sConversation.load(id, std::move(msgs));
        setStatus(StatusRole::Neutral, "Conversation loaded.");
    });
}

void deleteConversation()
{
    const std::string id = sConversation.id();
    if (id.empty())
    {
        newConversation();
        return;
    }
    simple("SW67KingJahmir::delete", "POST", "/v1/conversations/" + id + "/delete", "{}", [](const Json&) {
        sConversation.clear();
        setStatus(StatusRole::Success, "Conversation deleted from the King Jahmir server.");
    });
}

void deleteAllConversations()
{
    simple("SW67KingJahmir::deleteAll", "POST", "/v1/conversations/delete_all", R"({"confirm":true})",
           [](const Json& j) {
               sConversation.clear();
               setStatus(StatusRole::Success,
                         "Deleted " + std::to_string(j["deleted"].asInt(0)) + " conversation(s) from the server.");
           });
}

void resolveAction(const std::string& actionId, bool confirm)
{
    if (actionId.rfind("act_", 0) != 0 || actionId.find_first_of("/?#%") != std::string::npos)
    {
        return;
    }
    simple("SW67KingJahmir::action", "POST",
           "/v1/actions/" + actionId + (confirm ? "/confirm" : "/cancel"), "{}", [actionId, confirm](const Json& j) {
               sConversation.resolveAction(actionId);
               if (!confirm)
               {
                   sConversation.addNotice("Cancelled. Nothing was sent or saved.");
                   setStatus(StatusRole::Neutral, "Cancelled.");
                   return;
               }
               // Only what the server confirmed is reported as done.
               const std::string msg = j["result"]["message"].asString("");
               sConversation.addNotice(msg.empty() ? "The server did not confirm this." : msg);
               setStatus(msg.empty() ? StatusRole::Warning : StatusRole::Success,
                         msg.empty() ? "Not confirmed." : "Confirmed by the server.");
           });
}

void fetchPreferences(std::function<void(std::map<std::string, std::string>)> done)
{
    simple("SW67KingJahmir::prefs", "GET", "/v1/preferences", {},
           [done](const Json& j) { done(prefsFrom(j)); });
}

void setPreference(const std::string& key, const std::string& value,
                   std::function<void(std::map<std::string, std::string>)> done)
{
    Json body = Json::object();
    body.set("value", value);
    simple("SW67KingJahmir::prefSet", "POST", "/v1/preferences/" + LLURI::escape(key), sw67::io::writeJson(body),
           [done](const Json& j) { done(prefsFrom(j)); });
}

void deletePreference(const std::string& key, std::function<void(std::map<std::string, std::string>)> done)
{
    simple("SW67KingJahmir::prefDelete", "POST", "/v1/preferences/" + LLURI::escape(key) + "/delete", "{}",
           [done](const Json& j) { done(prefsFrom(j)); });
}

bool isKnownScreen(std::string_view id)
{
    for (std::string_view s : kScreens)
    {
        if (s == id)
        {
            return LLFloaterReg::isRegistered(id);
        }
    }
    return false;
}

std::string helpUrl(const std::string& helpId)
{
    const std::string base = gatewayUrl();
    return (base.empty() || !validHelpId(helpId)) ? std::string() : base + "/help/" + helpId;
}

std::string frontScreen()
{
    if (!gFloaterView)
    {
        return {};
    }
    for (LLView* view : *gFloaterView->getChildList()) // front to back
    {
        auto* floater = dynamic_cast<LLFloater*>(view);
        if (!floater || !floater->getVisible() || floater->isMinimized())
        {
            continue;
        }
        const std::string& name = floater->getInstanceName();
        if (name.rfind("sw67_king_jahmir", 0) == 0)
        {
            continue;
        }
        return validScreenId(name) ? name : std::string();
    }
    return {};
}

void onLogin()
{
    loadBootstrap();
    if (gSavedSettings.getBOOL("SW67KingJahmirBubble"))
    {
        LLFloaterReg::showInstance("sw67_king_jahmir_bubble");
    }
}
} // namespace sw67viewer::kingjahmir
