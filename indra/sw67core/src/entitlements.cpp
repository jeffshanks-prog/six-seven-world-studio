/**
 * @file entitlements.cpp
 * @brief The plan catalogue and feature checks.
 *
 * Copyright (c) 2026, 6|7 World Studio contributors.
 *
 * The source code in this file is provided to you under the terms of the
 * GNU Lesser General Public License, version 2.1, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE. Terms of the LGPL can be found in doc/LGPL-license.txt
 * in this distribution, or online at http://www.gnu.org/licenses/lgpl-2.1.txt
 */

#include "sw67/plan/entitlements.h"

#include "sw67/io/json.h"

#include <cstdio>
#include <functional>

namespace sw67::plan
{

bool isAlwaysIncluded(std::string_view key)
{
    return key == feature::LocalData || key == feature::Accessibility;
}

const Plan* Catalogue::find(std::string_view id) const
{
    for (const Plan& p : plans)
    {
        if (p.id == id)
        {
            return &p;
        }
    }
    return nullptr;
}

CatalogueResult readCatalogue(std::string_view json)
{
    CatalogueResult result;
    io::JsonError err;
    const auto doc = io::parseJson(json, &err);
    if (!doc || !doc->isObject() || (*doc)["format"].asString() != "sw67.plans")
    {
        result.problems.push_back(doc ? "Not a 6|7 plan catalogue." : "The catalogue is not valid JSON (" + err.what + ").");
        return result;
    }
    Catalogue c;
    c.version = (*doc)["version"].asInt(0);
    c.sandbox = (*doc)["sandbox"].asBool(true);
    c.currency = (*doc)["currency"].asString("USD");
    if ((*doc)["trialDays"].isNumber())
    {
        const int days = (*doc)["trialDays"].asInt(-1);
        if (days >= 0 && days <= 90)
        {
            c.trialDays = days;
        }
        else
        {
            result.problems.push_back("trialDays must be a whole number from 0 to 90, or null.");
        }
    }
    for (const io::Json& j : (*doc)["plans"].items())
    {
        Plan p;
        p.id = j["id"].asString("");
        p.name = j["name"].asString(p.id);
        p.priceCentsPerMonth = j["priceCentsPerMonth"].asInt(-1);
        p.currency = c.currency;
        p.isAddOn = j["addOn"].asBool(false);
        p.aiCreditsPerMonth = j["aiCreditsPerMonth"].asInt(0);
        if (j["aiCreditsPerMonth"].isString() || j["unlimitedAI"].asBool(false))
        {
            result.problems.push_back("Plan \"" + p.id + "\" offers unlimited AI, which is not allowed.");
            continue;
        }
        for (const io::Json& f : j["features"].items())
        {
            if (f.isString()) p.features.insert(f.asString());
        }
        for (const io::Json& inc : j["includes"].items())
        {
            if (inc.isString()) p.includesPlans.push_back(inc.asString());
        }
        if (p.id.empty() || p.priceCentsPerMonth < 0 || p.aiCreditsPerMonth < 0)
        {
            result.problems.push_back("A plan has no id, or a negative price or allowance.");
            continue;
        }
        if (p.isAddOn && !p.includesPlans.empty())
        {
            result.problems.push_back("Add-on \"" + p.id + "\" cannot include other plans.");
            continue;
        }
        if (c.find(p.id))
        {
            result.problems.push_back("Plan \"" + p.id + "\" is listed twice.");
            continue;
        }
        c.plans.push_back(std::move(p));
    }
    // Every include must name a known plan, with no loops.
    for (const Plan& p : c.plans)
    {
        std::set<std::string> seen;
        std::function<bool(const Plan&)> walk = [&](const Plan& q) -> bool {
            if (!seen.insert(q.id).second) return false;
            for (const std::string& inc : q.includesPlans)
            {
                const Plan* next = c.find(inc);
                if (!next || next->isAddOn || !walk(*next)) return false;
            }
            seen.erase(q.id);
            return true;
        };
        if (!walk(p))
        {
            result.problems.push_back("Plan \"" + p.id + "\" includes an unknown plan, an add-on, or itself.");
        }
    }
    for (const io::Json& j : (*doc)["creditPacks"].items())
    {
        CreditPack pack{j["id"].asString(""), j["credits"].asInt(0), j["priceCents"].asInt(-1)};
        if (pack.id.empty() || pack.credits <= 0 || pack.priceCents < 0)
        {
            result.problems.push_back("A credit pack has no id, credits or price.");
            continue;
        }
        c.creditPacks.push_back(std::move(pack));
    }
    if (result.problems.empty())
    {
        result.catalogue = std::move(c);
    }
    return result;
}

namespace
{
void collect(const Catalogue& c, const Plan& p, std::set<std::string>& features, int& credits, std::set<std::string>& visited)
{
    if (!visited.insert(p.id).second)
    {
        return;
    }
    features.insert(p.features.begin(), p.features.end());
    credits += p.aiCreditsPerMonth;
    for (const std::string& inc : p.includesPlans)
    {
        if (const Plan* q = c.find(inc))
        {
            // An included plan's features, but not its credits again: the
            // allowance is the top plan's own (see the catalogue's notes).
            int ignored = 0;
            collect(c, *q, features, ignored, visited);
        }
    }
}
} // namespace

std::set<std::string> featuresOf(const Catalogue& c, const Subscription& sub)
{
    std::set<std::string> features{std::string(feature::LocalData), std::string(feature::Accessibility)};
    if (!sub.active)
    {
        return features;
    }
    std::set<std::string> visited;
    int credits = 0;
    if (const Plan* p = c.find(sub.planId); p && !p->isAddOn)
    {
        collect(c, *p, features, credits, visited);
        for (const std::string& a : sub.addOns)
        {
            if (const Plan* addOn = c.find(a); addOn && addOn->isAddOn)
            {
                collect(c, *addOn, features, credits, visited);
            }
        }
    }
    return features;
}

bool hasFeature(const Catalogue& c, const Subscription& sub, std::string_view key)
{
    if (isAlwaysIncluded(key))
    {
        return true;
    }
    return featuresOf(c, sub).count(std::string(key)) > 0;
}

int monthlyAICredits(const Catalogue& c, const Subscription& sub)
{
    if (!sub.active)
    {
        return 0;
    }
    const Plan* p = c.find(sub.planId);
    if (!p || p->isAddOn)
    {
        return 0;
    }
    int credits = p->aiCreditsPerMonth;
    for (const std::string& a : sub.addOns)
    {
        if (const Plan* addOn = c.find(a); addOn && addOn->isAddOn)
        {
            credits += addOn->aiCreditsPerMonth;
        }
    }
    return credits;
}

std::string formatPrice(int cents, std::string_view currency)
{
    char buf[32];
    const char* symbol = currency == "USD" ? "$" : "";
    std::snprintf(buf, sizeof(buf), "%s%d.%02d", symbol, cents / 100, cents % 100);
    std::string out = buf;
    if (*symbol == '\0')
    {
        out += " " + std::string(currency);
    }
    return out;
}

} // namespace sw67::plan
