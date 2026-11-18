/**
 * @file sw67partycontacts.cpp
 * @brief Party Contacts in the viewer. See sw67partycontacts.h.
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

#include "sw67partycontacts.h"

#include "sw67rlvstate.h"
#include "sw67viewerfiles.h"

#include "llagent.h"
#include "llavatarnamecache.h"
#include "llcallbacklist.h"
#include "lldate.h"
#include "llnotificationsutil.h"
#include "llviewermessage.h"

#include <ctime>
#include <memory>

using namespace sw67::contacts;

namespace sw67viewer::party
{
namespace
{
std::unique_ptr<Book> sBook;
boost::signals2::signal<void()> sChanged;

struct Job
{
    std::vector<Recipient> queue;
    std::size_t next = 0;
    std::string text;
    SendOutcome outcome;
    std::function<void(const SendOutcome&)> done;
    bool cancelled = false;
};
std::shared_ptr<Job> sJob;

std::string filePath()
{
    return sw67viewer::joinPath(sw67viewer::userDir("party"), "contacts.sw67party.json");
}

void finish()
{
    std::shared_ptr<Job> job = std::move(sJob);
    sJob.reset();
    if (job && job->done) job->done(job->outcome);
}

void sendNext(std::shared_ptr<Job> job)
{
    if (job != sJob) return; // Replaced or cancelled.
    if (job->cancelled || job->next >= job->queue.size())
    {
        for (std::size_t i = job->next; i < job->queue.size(); ++i)
        {
            job->outcome.notSent.push_back({job->queue[i].accountId, job->queue[i].displayName, "the send was stopped"});
        }
        job->next = job->queue.size();
        finish();
        return;
    }
    const Recipient& r = job->queue[job->next++];
    sw67::rlv::ActionRequest req;
    req.kind = sw67::rlv::ActionKind::SendIM;
    req.targetId = r.accountId;
    const sw67::rlv::Decision d = SW67RlvState::check(req);
    LLUUID id;
    if (!d.allowed())
    {
        job->outcome.notSent.push_back({r.accountId, r.displayName, d.explanation.empty() ? "RLV blocks IMs to them" : d.explanation});
    }
    else if (!id.set(r.accountId, false) || id.isNull())
    {
        job->outcome.notSent.push_back({r.accountId, r.displayName, "not a valid account ID"});
    }
    else
    {
        send_simple_im(id, job->text);
        job->outcome.sent.push_back(r.accountId);
    }
    doAfterInterval([job]() { sendNext(job); }, static_cast<F32>(kSendSpacingSeconds));
}
} // namespace

Book& book()
{
    if (!sBook)
    {
        sBook = std::make_unique<Book>();
        if (const auto text = sw67viewer::readFile(filePath(), 16u * 1024u * 1024u))
        {
            ReadResult r = readBook(*text);
            if (r.book) *sBook = std::move(*r.book);
            else LL_WARNS("SW67Party") << "Party Contacts file could not be read; starting empty" << LL_ENDL;
        }
        else
        {
            sBook->lists = starterLists();
        }
    }
    return *sBook;
}

bool save()
{
    const bool ok = sw67viewer::writeFileAtomic(filePath(), writeBook(book()));
    sChanged();
    return ok;
}

boost::signals2::connection onChanged(const std::function<void()>& cb) { return sChanged.connect(cb); }

std::string selfId()
{
    std::string s = gAgentID.asString();
    LLStringUtil::toLower(s);
    return s;
}

std::string nowUtc() { return LLDate::now().toHTTPDateString(std::string("%Y-%m-%d %H:%M:%S")); }

void saveResidents(const uuid_vec_t& ids, Source source)
{
    for (const LLUUID& id : ids)
    {
        if (id.isNull() || id == gAgentID) continue;
        LLAvatarNameCache::get(id, [source](const LLUUID& agent, const LLAvatarName& name) {
            const Result r = saveContact(book(), agent.asString(), name.getDisplayName(), name.getUserName(), source, nowUtc());
            if (r.ok) save();
            LLNotificationsUtil::add("SystemMessageTip", LLSD().with("MESSAGE", r.message));
        });
    }
}

bool startSend(const std::vector<Recipient>& recipients, const std::string& text,
               std::function<void(const SendOutcome&)> done, std::string& whyNot)
{
    if (sJob)
    {
        whyNot = "Another send is still going.";
        return false;
    }
    if (recipients.empty())
    {
        whyNot = "Nobody to send to.";
        return false;
    }
    if (recipients.size() > kMaxRecipientsPerSend)
    {
        whyNot = "One send goes to at most 40 residents.";
        return false;
    }
    if (text.find_first_not_of(" \t\r\n") == std::string::npos || text.size() > 1023)
    {
        whyNot = "The message must be one IM: not empty and at most 1,023 bytes.";
        return false;
    }
    auto job = std::make_shared<Job>();
    job->queue = recipients;
    job->text = text;
    job->done = std::move(done);
    sJob = job;
    sendNext(job);
    return true;
}

bool sending() { return sJob != nullptr; }

void cancelSend()
{
    if (sJob) sJob->cancelled = true;
}

} // namespace sw67viewer::party

namespace sw67viewer::events
{
namespace
{
std::unique_ptr<sw67::event::Schedule> sSchedule;

std::string schedulePath()
{
    return sw67viewer::joinPath(sw67viewer::userDir("events"), "schedule.sw67events.json");
}
} // namespace

sw67::event::Schedule& schedule()
{
    if (!sSchedule)
    {
        sSchedule = std::make_unique<sw67::event::Schedule>();
        if (const auto text = sw67viewer::readFile(schedulePath(), 8u * 1024u * 1024u))
        {
            auto r = sw67::event::readSchedule(*text);
            if (r.schedule) *sSchedule = std::move(*r.schedule);
        }
    }
    return *sSchedule;
}

bool saveSchedule()
{
    return sw67viewer::writeFileAtomic(schedulePath(), sw67::event::writeSchedule(schedule()));
}

std::int64_t nowEpoch() { return static_cast<std::int64_t>(std::time(nullptr)); }

std::optional<std::int64_t> epochFromLocal(const std::string& text)
{
    const auto t = sw67::event::parseDateTime(text);
    if (!t) return std::nullopt;
    std::tm tm{};
    tm.tm_year = t->year - 1900;
    tm.tm_mon = t->month - 1;
    tm.tm_mday = t->day;
    tm.tm_hour = t->hour;
    tm.tm_min = t->minute;
    tm.tm_isdst = -1;
    const std::time_t e = std::mktime(&tm);
    if (e == static_cast<std::time_t>(-1)) return std::nullopt;
    return static_cast<std::int64_t>(e);
}

std::string localFromEpoch(std::int64_t epoch)
{
    const std::time_t e = static_cast<std::time_t>(epoch);
    std::tm tm{};
#if LL_WINDOWS
    localtime_s(&tm, &e);
#else
    localtime_r(&e, &tm);
#endif
    sw67::event::DateTime t;
    t.year = tm.tm_year + 1900;
    t.month = tm.tm_mon + 1;
    t.day = tm.tm_mday;
    t.hour = tm.tm_hour;
    t.minute = tm.tm_min;
    return sw67::event::formatDateTime(t);
}
} // namespace sw67viewer::events

