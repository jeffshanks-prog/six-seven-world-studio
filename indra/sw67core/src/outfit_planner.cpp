/**
 * @file outfit_planner.cpp
 * @brief Outfit Planner.
 *
 * Copyright (c) 2026, 6|7 World Studio contributors.
 *
 * The source code in this file is provided to you under the terms of the
 * GNU Lesser General Public License, version 2.1, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE. Terms of the LGPL can be found in doc/LGPL-license.txt
 * in this distribution, or online at http://www.gnu.org/licenses/lgpl-2.1.txt
 */

#include "sw67/outfit/planner.h"

#include "sw67/creator/project.h"
#include "sw67/io/json.h"
#include "sw67/photo/product.h"

#include <algorithm>
#include <cctype>

namespace sw67::outfit
{
namespace
{
using io::Json;

std::string lowerId(std::string_view id)
{
    std::string out(id);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

bool isBodyPartType(std::string_view t)
{
    const auto& b = bodyPartTypes();
    return std::find(b.begin(), b.end(), t) != b.end();
}

// An item is well formed when its ID is a UUID and its kind and type agree.
std::optional<std::string> itemProblem(const Item& item)
{
    if (!creator::isUuid(item.id)) return std::string("its inventory ID is not valid");
    if (item.name.size() > 255) return std::string("its name is too long");
    switch (item.kind)
    {
    case ItemKind::BodyPart:
        if (!isBodyPartType(item.wearableType)) return std::string("it is marked a body part but its type is not one");
        break;
    case ItemKind::Clothing:
        if (!isWearableTypeName(item.wearableType) || isBodyPartType(item.wearableType))
            return std::string("it is marked clothing but its type is not a clothing type");
        break;
    case ItemKind::Object:
        if (!item.wearableType.empty()) return std::string("it is marked an attachment but has a wearable type");
        break;
    }
    return std::nullopt;
}

std::string plural(int n, const char* one, const char* many)
{
    return std::to_string(n) + " " + (n == 1 ? one : many);
}
} // namespace

std::string_view kindKey(ItemKind k)
{
    switch (k)
    {
    case ItemKind::BodyPart: return "bodypart";
    case ItemKind::Clothing: return "clothing";
    case ItemKind::Object: return "object";
    }
    return "object";
}

std::optional<ItemKind> parseKind(std::string_view key)
{
    if (key == "bodypart") return ItemKind::BodyPart;
    if (key == "clothing") return ItemKind::Clothing;
    if (key == "object") return ItemKind::Object;
    return std::nullopt;
}

const std::vector<std::string>& bodyPartTypes()
{
    static const std::vector<std::string> t = {"shape", "skin", "hair", "eyes"};
    return t;
}

bool isWearableTypeName(std::string_view name)
{
    static const std::vector<std::string_view> all = {
        "shape", "skin",       "hair",       "eyes",  "shirt", "pants",  "shoes",    "socks",    "jacket",
        "gloves", "undershirt", "underpants", "skirt", "alpha", "tattoo", "physics", "universal"};
    return std::find(all.begin(), all.end(), name) != all.end();
}

bool Plan::canWear() const
{
    return std::none_of(problems.begin(), problems.end(), [](const Problem& p) { return p.blocking; });
}

std::size_t Plan::changeCount() const
{
    return static_cast<std::size_t>(std::count_if(steps.begin(), steps.end(), [](const Step& s) {
        return s.what == Step::What::Wear || s.what == Step::What::Remove;
    }));
}

std::string Plan::describe() const
{
    std::string out;
    for (const Step& s : steps)
    {
        switch (s.what)
        {
        case Step::What::Wear: out += "Wear "; break;
        case Step::What::Remove: out += "Take off "; break;
        case Step::What::Keep: out += "Keep "; break;
        case Step::What::Missing: out += "Missing "; break;
        }
        out += s.item.name.empty() ? s.item.id : s.item.name;
        if (!s.reason.empty()) out += " (" + s.reason + ")";
        out += "\n";
    }
    out += "\nAfter: " + plural(attachmentsAfter, "attachment", "attachments") + ", " +
           plural(clothingAfter, "clothing layer", "clothing layers") + ".\n";
    for (const Problem& p : problems)
    {
        out += (p.blocking ? "Cannot wear as planned: " : "Note: ") + p.text + "\n";
    }
    return out;
}

Plan planOutfit(const Outfit& outfit, const std::vector<Item>& worn, const std::map<std::string, Item>& owned,
                const Limits& limits, const std::set<std::string>& alwaysKeep)
{
    Plan plan;
    std::map<std::string, const Item*> wornById;
    for (const Item& w : worn) wornById[lowerId(w.id)] = &w;
    std::set<std::string> keep;
    for (const std::string& k : alwaysKeep) keep.insert(lowerId(k));

    // The outfit's items, one each, and one body part of each type.
    std::set<std::string> inOutfit;
    std::map<std::string, std::string> bodyPartWorn; // type -> id that will be worn after
    std::map<std::string, std::string> bodyPartNew;  // type -> name of the new one
    int missing = 0;
    for (const Item& item : outfit.items)
    {
        const std::string id = lowerId(item.id);
        if (!inOutfit.insert(id).second) continue;
        if (item.kind == ItemKind::BodyPart)
        {
            if (bodyPartWorn.count(item.wearableType) || bodyPartNew.count(item.wearableType))
            {
                plan.problems.push_back({false, "The outfit has more than one " + item.wearableType + "; only " +
                                                    "the first is worn."});
                continue;
            }
        }
        Step step;
        step.item = item;
        if (wornById.count(id))
        {
            step.what = Step::What::Keep;
            step.reason = "already worn";
            if (item.kind == ItemKind::BodyPart) bodyPartWorn[item.wearableType] = id;
        }
        else if (!owned.count(id))
        {
            step.what = Step::What::Missing;
            step.reason = "not in your inventory; nothing is bought or replaced for you";
            ++missing;
        }
        else
        {
            step.what = Step::What::Wear;
            if (item.kind == ItemKind::BodyPart)
            {
                bodyPartWorn[item.wearableType] = id;
                bodyPartNew[item.wearableType] = item.name;
            }
        }
        plan.steps.push_back(std::move(step));
    }

    // What is worn now and not in the outfit.
    for (const Item& w : worn)
    {
        const std::string id = lowerId(w.id);
        if (inOutfit.count(id)) continue;
        Step step;
        step.item = w;
        if (w.kind == ItemKind::BodyPart)
        {
            const auto replacing = bodyPartNew.find(w.wearableType);
            if (replacing != bodyPartNew.end())
            {
                step.what = Step::What::Remove;
                step.implicit = true;
                step.reason = "replaced by " + replacing->second;
            }
            else
            {
                step.what = Step::What::Keep;
                step.reason = "an avatar always wears a " + w.wearableType;
                bodyPartWorn.emplace(w.wearableType, id);
            }
        }
        else if (keep.count(id))
        {
            step.what = Step::What::Keep;
            step.reason = "you keep it with every outfit";
        }
        else if (outfit.replace)
        {
            step.what = Step::What::Remove;
            step.reason = "not in this outfit";
        }
        else
        {
            step.what = Step::What::Keep;
            step.reason = "this outfit adds to what you wear";
        }
        plan.steps.push_back(std::move(step));
    }

    for (const Step& s : plan.steps)
    {
        if (s.what != Step::What::Wear && s.what != Step::What::Keep) continue;
        if (s.item.kind == ItemKind::Object) ++plan.attachmentsAfter;
        if (s.item.kind == ItemKind::Clothing) ++plan.clothingAfter;
    }
    if (plan.attachmentsAfter > limits.attachments)
    {
        plan.problems.push_back({true, "This would wear " + std::to_string(plan.attachmentsAfter) +
                                           " attachments; your limit is " + std::to_string(limits.attachments) +
                                           ". Take some out of the outfit, or turn on replacing what you wear."});
    }
    if (plan.clothingAfter > limits.clothingLayers)
    {
        plan.problems.push_back({true, "This would wear " + std::to_string(plan.clothingAfter) +
                                           " clothing layers; the limit is " + std::to_string(limits.clothingLayers) +
                                           "."});
    }
    for (const auto& [point, items] : sharedPoints(plan))
    {
        std::string names;
        for (const Item& i : items) names += (names.empty() ? "" : ", ") + (i.name.empty() ? i.id : i.name);
        plan.problems.push_back({false, std::to_string(items.size()) + " items share the " + point +
                                            " attachment point (" + names +
                                            "). Second Life wears them together; check they do not overlap."});
    }
    for (const std::string& type : bodyPartTypes())
    {
        if (!bodyPartWorn.count(type))
        {
            plan.problems.push_back({false, "No " + type + " is worn or in the outfit."});
        }
    }
    if (missing > 0)
    {
        plan.problems.push_back({false, plural(missing, "item is", "items are") +
                                            " no longer in your inventory. They are left out; nothing is bought or "
                                            "replaced for you."});
    }
    return plan;
}

std::vector<rlv::ActionRequest> gateRequests(const Plan& plan)
{
    std::vector<rlv::ActionRequest> out;
    for (const Step& s : plan.steps)
    {
        if (s.implicit) continue;
        rlv::ActionRequest r;
        if (s.what == Step::What::Wear)
            r.kind = rlv::ActionKind::WearItem;
        else if (s.what == Step::What::Remove)
            r.kind = rlv::ActionKind::RemoveItem;
        else
            continue;
        r.targetId = lowerId(s.item.id);
        out.push_back(std::move(r));
    }
    return out;
}

Outfit outfitFromWorn(std::string name, const std::vector<Item>& worn)
{
    Outfit o;
    o.name = std::move(name);
    std::set<std::string> seen;
    for (const Item& w : worn)
    {
        if (seen.insert(lowerId(w.id)).second) o.items.push_back(w);
    }
    return o;
}

Difference compare(const Outfit& a, const Outfit& b)
{
    std::set<std::string> ia, ib;
    for (const Item& i : a.items) ia.insert(lowerId(i.id));
    for (const Item& i : b.items) ib.insert(lowerId(i.id));
    Difference d;
    for (const Item& i : a.items)
        if (!ib.count(lowerId(i.id))) d.onlyInA.push_back(i);
    for (const Item& i : b.items)
        if (!ia.count(lowerId(i.id))) d.onlyInB.push_back(i);
    return d;
}

std::string writeOutfit(const Outfit& o)
{
    Json root = Json::object();
    root.set("format", Json(kFormat));
    root.set("version", Json(kFormatVersion));
    root.set("name", Json(o.name));
    root.set("occasion", Json(o.occasion));
    root.set("notes", Json(o.notes));
    root.set("replace", Json(o.replace));
    Json tags = Json::array();
    for (const std::string& t : o.tags) tags.push(Json(t));
    root.set("tags", std::move(tags));
    Json items = Json::array();
    for (const Item& i : o.items)
    {
        Json e = Json::object();
        e.set("id", Json(lowerId(i.id)));
        e.set("name", Json(i.name));
        e.set("kind", Json(kindKey(i.kind)));
        if (!i.wearableType.empty()) e.set("type", Json(i.wearableType));
        if (!i.attachPoint.empty()) e.set("point", Json(i.attachPoint));
        items.push(std::move(e));
    }
    root.set("items", std::move(items));
    return io::writeJson(root);
}

ReadResult readOutfit(std::string_view text)
{
    ReadResult r;
    io::JsonError err;
    const auto root = io::parseJson(text, &err);
    if (!root || !root->isObject() || (*root)["format"].asString("") != kFormat)
    {
        r.notes.push_back("This is not a 6|7 outfit file.");
        return r;
    }
    const int version = (*root)["version"].asInt(0);
    if (version < 1 || version > kFormatVersion)
    {
        r.notes.push_back("This outfit was saved by a newer 6|7 World Studio. Update the viewer to open it.");
        return r;
    }
    Outfit o;
    o.name = (*root)["name"].asString("").substr(0, 120);
    o.occasion = (*root)["occasion"].asString("").substr(0, 120);
    o.notes = (*root)["notes"].asString("").substr(0, 4000);
    o.replace = (*root)["replace"].asBool(true);
    for (const Json& t : (*root)["tags"].items())
    {
        if (t.isString() && !t.asString().empty() && o.tags.size() < 32) o.tags.push_back(t.asString().substr(0, 40));
    }
    std::set<std::string> seen;
    for (const Json& e : (*root)["items"].items())
    {
        if (o.items.size() >= kMaxItems)
        {
            r.notes.push_back("Only the first " + std::to_string(kMaxItems) + " items were read.");
            break;
        }
        const auto kind = parseKind(e["kind"].asString(""));
        Item item{lowerId(e["id"].asString("")), e["name"].asString(""), kind.value_or(ItemKind::Object),
                  e["type"].asString(""), e["point"].asString("").substr(0, 40)};
        if (item.kind != ItemKind::Object) item.attachPoint.clear();
        const std::string label = item.name.empty() ? std::string("an item") : "\"" + item.name + "\"";
        if (!kind)
        {
            r.notes.push_back("Left out " + label + ": its kind is not known.");
            continue;
        }
        if (const auto why = itemProblem(item))
        {
            r.notes.push_back("Left out " + label + ": " + *why + ".");
            continue;
        }
        if (!seen.insert(item.id).second)
        {
            r.notes.push_back("Left out a second copy of " + label + ".");
            continue;
        }
        o.items.push_back(std::move(item));
    }
    r.outfit = std::move(o);
    return r;
}

std::string fileNameFor(const Outfit& outfit)
{
    const std::string stem = outfit.name.empty() ? std::string("outfit") : photo::safeFileStem(outfit.name);
    return stem + ".sw67outfit.json";
}

} // namespace sw67::outfit

namespace sw67::outfit
{

std::map<std::string, std::vector<Item>> sharedPoints(const Plan& plan)
{
    std::map<std::string, std::vector<Item>> byPoint;
    for (const Step& s : plan.steps)
    {
        if (s.what != Step::What::Wear && s.what != Step::What::Keep) continue;
        if (s.item.kind != ItemKind::Object || s.item.attachPoint.empty()) continue;
        byPoint[s.item.attachPoint].push_back(s.item);
    }
    for (auto it = byPoint.begin(); it != byPoint.end();)
    {
        it = it->second.size() > 1 ? std::next(it) : byPoint.erase(it);
    }
    return byPoint;
}

std::size_t RestoreReport::done() const
{
    return static_cast<std::size_t>(std::count_if(results.begin(), results.end(), [](const ItemResult& r) {
        return r.outcome == ItemResult::Outcome::Done;
    }));
}

std::size_t RestoreReport::failed() const
{
    return results.size() - done();
}

std::string RestoreReport::describe() const
{
    std::string out;
    if (results.empty())
    {
        return "Nothing needed to change.";
    }
    out = std::to_string(done()) + " of " + std::to_string(results.size()) + " change(s) went as planned.";
    if (complete())
    {
        return out + " The outfit is restored.";
    }
    out += " " + std::to_string(failed()) + " did not:";
    for (const ItemResult& r : results)
    {
        if (r.outcome == ItemResult::Outcome::Done) continue;
        const std::string name = r.item.name.empty() ? r.item.id : r.item.name;
        out += "\n  - " + name + ": " + r.reason;
    }
    return out;
}

RestoreReport checkRestore(const Plan& plan, const std::set<std::string>& wornNow)
{
    RestoreReport report;
    for (const Step& s : plan.steps)
    {
        ItemResult r;
        r.item = s.item;
        const bool worn = wornNow.count(lowerId(s.item.id)) > 0 || wornNow.count(s.item.id) > 0;
        switch (s.what)
        {
        case Step::What::Keep: continue;
        case Step::What::Missing:
            r.outcome = ItemResult::Outcome::Missing;
            r.reason = "not in your inventory, so it was not asked for. Nothing was bought or replaced.";
            break;
        case Step::What::Wear:
            r.wanted = true;
            r.outcome = worn ? ItemResult::Outcome::Done : ItemResult::Outcome::NotWorn;
            if (!worn)
            {
                r.reason = s.item.kind == ItemKind::Object
                               ? "it did not attach. The region may not allow it, it may be in use, or the "
                                 "request timed out. Try wearing it from your inventory."
                               : "it was not put on. Try wearing it from your inventory.";
            }
            break;
        case Step::What::Remove:
            r.wanted = false;
            r.outcome = worn ? ItemResult::Outcome::StillWorn : ItemResult::Outcome::Done;
            if (worn)
            {
                r.reason = s.implicit ? "it is still worn; the new " + s.item.wearableType + " did not replace it."
                                      : "it is still worn. It may be locked by a script or RLV, or the request "
                                        "timed out. Take it off from your inventory.";
            }
            break;
        }
        report.results.push_back(std::move(r));
    }
    return report;
}

} // namespace sw67::outfit
