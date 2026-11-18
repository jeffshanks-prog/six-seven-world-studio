/**
 * @file shop_organizer.cpp
 * @brief Shopping and Demo Organizer. See organizer.h.
 *
 * Copyright (c) 2026, 6|7 World Studio contributors.
 *
 * The source code in this file is provided to you under the terms of the
 * GNU Lesser General Public License, version 2.1, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE. Terms of the LGPL can be found in doc/LGPL-license.txt
 * in this distribution, or online at http://www.gnu.org/licenses/lgpl-2.1.txt
 */

#include "sw67/shop/organizer.h"

#include "sw67/io/json.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <set>

namespace sw67::shop
{
namespace
{
using io::Json;

std::string lower(std::string_view s)
{
    std::string out(s);
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

std::string trim(std::string_view s)
{
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return std::string(s.substr(a, b - a));
}

bool validId(const std::string& id)
{
    if (id.size() != 36) return false;
    for (size_t i = 0; i < id.size(); ++i)
    {
        const char c = id[i];
        if (i == 8 || i == 13 || i == 18 || i == 23) { if (c != '-') return false; }
        else if (!std::isxdigit(static_cast<unsigned char>(c))) return false;
    }
    return true;
}

std::string urlDecode(std::string_view s)
{
    std::string out;
    for (size_t i = 0; i < s.size(); ++i)
    {
        if (s[i] == '%' && i + 2 < s.size() && std::isxdigit(static_cast<unsigned char>(s[i + 1])) &&
            std::isxdigit(static_cast<unsigned char>(s[i + 2])))
        {
            out.push_back(static_cast<char>(std::stoi(std::string(s.substr(i + 1, 2)), nullptr, 16)));
            i += 2;
        }
        else if (s[i] == '+')
        {
            out.push_back(' ');
        }
        else
        {
            out.push_back(s[i]);
        }
    }
    return out;
}

std::optional<int> coord(std::string_view s, int max)
{
    if (s.empty() || s.size() > 6) return std::nullopt;
    double v = 0;
    try
    {
        v = std::stod(std::string(s));
    }
    catch (...)
    {
        return std::nullopt;
    }
    if (!(v >= 0.0) || v > max) return std::nullopt;
    return static_cast<int>(v);
}

Json tagsJson(const std::vector<Tag>& tags)
{
    return Json(formatTags(tags));
}

std::vector<Tag> tagsFrom(const Json& j)
{
    std::vector<Tag> t = parseTags(j.asString("").substr(0, 1000));
    if (t.size() > kMaxTags) t.resize(kMaxTags);
    return t;
}
} // namespace

const std::vector<std::string>& tagCategories()
{
    static const std::vector<std::string> c = {"body", "size", "style", "color"};
    return c;
}

std::vector<Tag> parseTags(std::string_view text)
{
    std::vector<Tag> out;
    std::set<std::pair<std::string, std::string>> seen;
    size_t start = 0;
    while (start <= text.size())
    {
        const size_t comma = std::min(text.find(',', start), text.size());
        const std::string part = trim(text.substr(start, comma - start));
        start = comma + 1;
        if (part.empty()) continue;
        Tag t;
        const size_t colon = part.find(':');
        if (colon != std::string::npos)
        {
            std::string cat = lower(trim(std::string_view(part).substr(0, colon)));
            if (cat == "colour") cat = "color";
            if (cat == "bodies" || cat == "brand") cat = "body";
            const std::string value = trim(std::string_view(part).substr(colon + 1));
            if (std::find(tagCategories().begin(), tagCategories().end(), cat) != tagCategories().end() && !value.empty())
            {
                t.category = cat;
                t.value = value.substr(0, 60);
            }
            else
            {
                t.value = part.substr(0, 60);
            }
        }
        else
        {
            t.value = part.substr(0, 60);
        }
        if (seen.insert({t.category, lower(t.value)}).second && out.size() < kMaxTags) out.push_back(std::move(t));
    }
    return out;
}

std::string formatTags(const std::vector<Tag>& tags)
{
    std::string out;
    for (const Tag& t : tags)
    {
        out += (out.empty() ? "" : ", ") + (t.category.empty() ? t.value : t.category + ":" + t.value);
    }
    return out;
}

std::string Location::mapUrl() const
{
    std::string enc;
    for (const char ch : region)
    {
        const auto c = static_cast<unsigned char>(ch);
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.') enc.push_back(ch);
        else
        {
            char buf[4];
            std::snprintf(buf, sizeof(buf), "%%%02X", c);
            enc += buf;
        }
    }
    return "https://maps.secondlife.com/secondlife/" + enc + "/" + std::to_string(x) + "/" + std::to_string(y) + "/" +
           std::to_string(z);
}

std::optional<Location> parseLocation(std::string_view text)
{
    std::string s = trim(text);
    const std::string l = lower(s);
    for (std::string_view prefix : {"https://maps.secondlife.com/secondlife/", "http://maps.secondlife.com/secondlife/",
                                    "http://slurl.com/secondlife/", "https://slurl.com/secondlife/",
                                    "secondlife:///app/teleport/", "secondlife://"})
    {
        if (l.rfind(prefix, 0) == 0)
        {
            s = s.substr(prefix.size());
            break;
        }
    }
    if (s.empty() || s.size() > 300) return std::nullopt;
    std::vector<std::string> parts;
    size_t start = 0;
    while (start <= s.size())
    {
        const size_t slash = std::min(s.find('/', start), s.size());
        parts.push_back(s.substr(start, slash - start));
        start = slash + 1;
    }
    while (!parts.empty() && parts.back().empty()) parts.pop_back();
    if (parts.empty()) return std::nullopt;
    Location loc;
    loc.region = trim(urlDecode(parts[0]));
    if (loc.region.empty() || loc.region.size() > 64) return std::nullopt;
    for (char c : loc.region)
        if (static_cast<unsigned char>(c) < 32 || c == '/') return std::nullopt;
    const int maxes[3] = {256, 256, 8192};
    int* outs[3] = {&loc.x, &loc.y, &loc.z};
    for (size_t i = 1; i < parts.size() && i <= 3; ++i)
    {
        const auto v = coord(parts[i], maxes[i - 1]);
        if (!v) return std::nullopt;
        *outs[i - 1] = *v;
    }
    return loc;
}

std::string_view statusKey(ListItem::Status s)
{
    switch (s)
    {
    case ListItem::Status::Want: return "want";
    case ListItem::Status::Bought: return "bought";
    case ListItem::Status::Skip: return "skip";
    }
    return "want";
}

std::string_view verdictKey(Demo::Verdict v)
{
    switch (v)
    {
    case Demo::Verdict::Untried: return "untried";
    case Demo::Verdict::Like: return "like";
    case Demo::Verdict::Maybe: return "maybe";
    case Demo::Verdict::No: return "no";
    }
    return "untried";
}

int Organizer::wantedTotalL() const
{
    long long total = 0;
    for (const ListItem& i : list)
        if (i.status == ListItem::Status::Want) total += i.priceL;
    return static_cast<int>(std::min<long long>(total, 2000000000LL));
}

bool matches(std::string_view query, const std::string& name, const std::string& notes, const std::vector<Tag>& tags)
{
    const std::string hay = lower(name + " " + notes + " " + formatTags(tags));
    size_t start = 0;
    const std::string q = lower(query);
    while (start < q.size())
    {
        while (start < q.size() && std::isspace(static_cast<unsigned char>(q[start]))) ++start;
        size_t end = start;
        while (end < q.size() && !std::isspace(static_cast<unsigned char>(q[end]))) ++end;
        if (end == start) break;
        const std::string term = q.substr(start, end - start);
        start = end;
        const size_t colon = term.find(':');
        if (colon != std::string::npos && colon > 0)
        {
            std::string cat = term.substr(0, colon);
            if (cat == "colour") cat = "color";
            const std::string value = term.substr(colon + 1);
            const bool hit = std::any_of(tags.begin(), tags.end(), [&](const Tag& t) {
                return t.category == cat && lower(t.value).find(value) != std::string::npos;
            });
            if (!hit) return false;
        }
        else if (hay.find(term) == std::string::npos)
        {
            return false;
        }
    }
    return true;
}

std::vector<const Demo*> likedNotBought(const Organizer& o)
{
    std::set<std::string> bought;
    for (const ListItem& i : o.list)
        if (i.status == ListItem::Status::Bought) bought.insert(lower(trim(i.name)));
    std::vector<const Demo*> out;
    for (const Demo& d : o.demos)
    {
        if (d.verdict != Demo::Verdict::Like) continue;
        std::string base = lower(trim(d.name));
        for (std::string_view w : {"demo ", "[demo] ", "(demo) "})
            if (base.rfind(w, 0) == 0) base = trim(base.substr(w.size()));
        for (std::string_view w : {" demo", " [demo]", " (demo)"})
            if (base.size() > w.size() && base.compare(base.size() - w.size(), w.size(), w) == 0)
                base = trim(base.substr(0, base.size() - w.size()));
        if (!bought.count(base)) out.push_back(&d);
    }
    return out;
}

std::string writeOrganizer(const Organizer& o)
{
    Json root = Json::object();
    root.set("format", Json(kFormat));
    root.set("version", Json(kFormatVersion));
    Json stores = Json::array();
    for (const Store& s : o.stores)
    {
        Json e = Json::object();
        e.set("name", Json(s.name));
        if (s.location) e.set("location", Json(s.location->mapUrl()));
        if (!s.landmarkItemId.empty()) e.set("landmark", Json(s.landmarkItemId));
        e.set("notes", Json(s.notes));
        e.set("tags", tagsJson(s.tags));
        stores.push(std::move(e));
    }
    root.set("stores", std::move(stores));
    Json list = Json::array();
    for (const ListItem& i : o.list)
    {
        Json e = Json::object();
        e.set("name", Json(i.name));
        e.set("store", Json(i.store));
        e.set("price", Json(i.priceL));
        e.set("status", Json(statusKey(i.status)));
        e.set("notes", Json(i.notes));
        e.set("tags", tagsJson(i.tags));
        list.push(std::move(e));
    }
    root.set("list", std::move(list));
    Json demos = Json::array();
    for (const Demo& d : o.demos)
    {
        Json e = Json::object();
        if (!d.itemId.empty()) e.set("item", Json(d.itemId));
        e.set("name", Json(d.name));
        e.set("store", Json(d.store));
        e.set("tried", Json(d.triedOn));
        e.set("verdict", Json(verdictKey(d.verdict)));
        e.set("notes", Json(d.notes));
        e.set("tags", tagsJson(d.tags));
        demos.push(std::move(e));
    }
    root.set("demos", std::move(demos));
    return io::writeJson(root);
}

ReadResult readOrganizer(std::string_view text)
{
    ReadResult r;
    io::JsonError err;
    const auto root = io::parseJson(text, &err);
    if (!root || !root->isObject() || (*root)["format"].asString("") != kFormat)
    {
        r.notes.push_back("This is not a 6|7 shopping file.");
        return r;
    }
    const int version = (*root)["version"].asInt(0);
    if (version < 1 || version > kFormatVersion)
    {
        r.notes.push_back("This shopping file was saved by a newer 6|7 World Studio.");
        return r;
    }
    Organizer o;
    for (const Json& e : (*root)["stores"].items())
    {
        if (o.stores.size() >= kMaxEntries) break;
        Store s;
        s.name = e["name"].asString("").substr(0, 120);
        const std::string loc = e["location"].asString("");
        if (!loc.empty())
        {
            s.location = parseLocation(loc);
            if (!s.location) r.notes.push_back("\"" + s.name + "\": its location was not a valid Second Life address.");
        }
        const std::string lm = e["landmark"].asString("");
        if (validId(lower(lm))) s.landmarkItemId = lower(lm);
        s.notes = e["notes"].asString("").substr(0, 2000);
        s.tags = tagsFrom(e["tags"]);
        o.stores.push_back(std::move(s));
    }
    for (const Json& e : (*root)["list"].items())
    {
        if (o.list.size() >= kMaxEntries) break;
        ListItem i;
        i.name = e["name"].asString("").substr(0, 120);
        i.store = e["store"].asString("").substr(0, 120);
        i.priceL = std::clamp(e["price"].asInt(0), 0, 1000000);
        const std::string st = e["status"].asString("want");
        i.status = st == "bought" ? ListItem::Status::Bought : st == "skip" ? ListItem::Status::Skip : ListItem::Status::Want;
        i.notes = e["notes"].asString("").substr(0, 2000);
        i.tags = tagsFrom(e["tags"]);
        o.list.push_back(std::move(i));
    }
    for (const Json& e : (*root)["demos"].items())
    {
        if (o.demos.size() >= kMaxEntries) break;
        Demo d;
        const std::string id = lower(e["item"].asString(""));
        if (validId(id)) d.itemId = id;
        d.name = e["name"].asString("").substr(0, 120);
        d.store = e["store"].asString("").substr(0, 120);
        d.triedOn = e["tried"].asString("").substr(0, 10);
        const std::string v = e["verdict"].asString("untried");
        d.verdict = v == "like" ? Demo::Verdict::Like : v == "maybe" ? Demo::Verdict::Maybe
                  : v == "no"   ? Demo::Verdict::No   : Demo::Verdict::Untried;
        d.notes = e["notes"].asString("").substr(0, 2000);
        d.tags = tagsFrom(e["tags"]);
        o.demos.push_back(std::move(d));
    }
    r.organizer = std::move(o);
    return r;
}

} // namespace sw67::shop
