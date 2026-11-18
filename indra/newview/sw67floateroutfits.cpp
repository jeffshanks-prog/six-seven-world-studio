/**
 * @file sw67floateroutfits.cpp
 * @brief Outfit Planner floater.
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

#include "sw67floateroutfits.h"

#include "sw67accessibility.h"
#include "sw67rlvstate.h"
#include "sw67viewerfiles.h"

#include "llagentbenefits.h"
#include "llagentwearables.h"
#include "llcallbacklist.h"
#include "llappearancemgr.h"
#include "llfile.h"
#include "llfolderviewitem.h"
#include "llfolderviewmodelinventory.h"
#include "llformat.h"
#include "llinventorymodel.h"
#include "llinventorypanel.h"
#include "llscrolllistctrl.h"
#include "lltextbox.h"
#include "lluictrl.h"
#include "llviewerinventory.h"
#include "llviewerobject.h"
#include "llvoavatarself.h"
#include "llwearabletype.h"

#include <algorithm>

using namespace sw67::outfit;
using sw67::a11y::StatusRole;

namespace
{
std::optional<Item> itemFrom(const LLViewerInventoryItem* item)
{
    if (!item)
    {
        return std::nullopt;
    }
    Item out;
    out.id = item->getUUID().asString();
    out.name = item->getName();
    switch (item->getType())
    {
    case LLAssetType::AT_BODYPART:
        out.kind = ItemKind::BodyPart;
        out.wearableType = std::string(LLWearableType::getTypeName(item->getWearableType()));
        break;
    case LLAssetType::AT_CLOTHING:
        out.kind = ItemKind::Clothing;
        out.wearableType = std::string(LLWearableType::getTypeName(item->getWearableType()));
        break;
    case LLAssetType::AT_OBJECT:
    {
        out.kind = ItemKind::Object;
        std::string point;
        if (isAgentAvatarValid() && gAgentAvatarp->getAttachedPointName(item->getUUID(), point))
        {
            out.attachPoint = point;
        }
        break;
    }
    default: return std::nullopt;
    }
    return out;
}

/// The user's own item by ID, if it is in their inventory and not in the trash.
const LLViewerInventoryItem* ownedItem(const std::string& id)
{
    LLUUID uuid;
    if (!LLUUID::validate(id) || !uuid.set(id, false))
    {
        return nullptr;
    }
    const LLViewerInventoryItem* item = gInventory.getItem(uuid);
    if (!item || item->getIsLinkType())
    {
        return nullptr;
    }
    const LLUUID trash = gInventory.findCategoryUUIDForType(LLFolderType::FT_TRASH);
    if (trash.notNull() && gInventory.isObjectDescendentOf(uuid, trash))
    {
        return nullptr;
    }
    return item;
}

std::string kindLabel(const Item& i)
{
    if (i.kind == ItemKind::Object) return "attachment";
    return i.wearableType;
}
} // namespace

SW67FloaterOutfits::SW67FloaterOutfits(const LLSD& key)
:   LLFloater(key)
{
}

bool SW67FloaterOutfits::postBuild()
{
    getChild<LLUICtrl>("outfit_list")->setCommitCallback([this](LLUICtrl*, const LLSD&) { loadSelected(); });
    getChild<LLUICtrl>("capture_btn")->setCommitCallback([this](LLUICtrl*, const LLSD&) { onCapture(); });
    getChild<LLUICtrl>("add_selected_btn")->setCommitCallback([this](LLUICtrl*, const LLSD&) { onAddSelected(); });
    getChild<LLUICtrl>("remove_item_btn")->setCommitCallback([this](LLUICtrl*, const LLSD&) { onRemoveItem(); });
    getChild<LLUICtrl>("save_btn")->setCommitCallback([this](LLUICtrl*, const LLSD&) { onSave(); });
    getChild<LLUICtrl>("delete_btn")->setCommitCallback([this](LLUICtrl*, const LLSD&) { onDelete(); });
    getChild<LLUICtrl>("wear_btn")->setCommitCallback([this](LLUICtrl*, const LLSD&) { onWear(); });
    for (const char* name : {"replace_check", "keep_huds"})
    {
        getChild<LLUICtrl>(name)->setCommitCallback([this](LLUICtrl*, const LLSD&) {
            editorToOutfit();
            refreshPreview();
        });
    }
    return LLFloater::postBuild();
}

void SW67FloaterOutfits::onOpen(const LLSD& key)
{
    LLFloater::onOpen(key);
    refreshList();
    refreshItems();
    refreshPreview();
}

std::string SW67FloaterOutfits::outfitsDir()
{
    return sw67viewer::userDir("outfits");
}

std::vector<Item> SW67FloaterOutfits::wornItems()
{
    std::vector<Item> worn;
    const LLUUID cof = LLAppearanceMgr::instance().getCOF();
    if (cof.isNull())
    {
        return worn;
    }
    LLInventoryModel::cat_array_t cats;
    LLInventoryModel::item_array_t items;
    gInventory.collectDescendents(cof, cats, items, LLInventoryModel::EXCLUDE_TRASH);
    std::set<LLUUID> seen;
    for (const LLPointer<LLViewerInventoryItem>& link : items)
    {
        const LLViewerInventoryItem* item = link->getIsLinkType() ? link->getLinkedItem() : link.get();
        if (!item || !seen.insert(item->getUUID()).second)
        {
            continue;
        }
        if (auto i = itemFrom(item))
        {
            worn.push_back(std::move(*i));
        }
    }
    return worn;
}

std::set<std::string> SW67FloaterOutfits::wornHUDs(const std::vector<Item>& worn)
{
    std::set<std::string> huds;
    if (!isAgentAvatarValid())
    {
        return huds;
    }
    for (const Item& i : worn)
    {
        if (i.kind != ItemKind::Object) continue;
        const LLViewerObject* obj = gAgentAvatarp->getWornAttachment(LLUUID(i.id));
        if (obj && obj->isHUDAttachment())
        {
            huds.insert(i.id);
        }
    }
    return huds;
}

void SW67FloaterOutfits::refreshList()
{
    LLScrollListCtrl* list = getChild<LLScrollListCtrl>("outfit_list");
    list->deleteAllItems();
    const std::string dir = outfitsDir();
    for (const std::string& file : sw67viewer::listFiles(dir, ".sw67outfit.json"))
    {
        const auto text = sw67viewer::readFile(sw67viewer::joinPath(dir, file), 2 * 1024 * 1024);
        const ReadResult r = text ? readOutfit(*text) : ReadResult{};
        if (!r.outfit) continue;
        LLSD row;
        row["value"] = file;
        row["columns"][0]["column"] = "name";
        row["columns"][0]["value"] = r.outfit->name.empty() ? file : r.outfit->name;
        row["columns"][1]["column"] = "occasion";
        row["columns"][1]["value"] = r.outfit->occasion;
        list->addElement(row);
    }
}

void SW67FloaterOutfits::loadSelected()
{
    LLScrollListItem* sel = getChild<LLScrollListCtrl>("outfit_list")->getFirstSelected();
    if (!sel) return;
    const std::string file = sel->getValue().asString();
    const auto text = sw67viewer::readFile(sw67viewer::joinPath(outfitsDir(), file), 2 * 1024 * 1024);
    ReadResult r = text ? readOutfit(*text) : ReadResult{};
    if (!r.outfit)
    {
        showStatus(StatusRole::Error, "Could not read that outfit.");
        return;
    }
    mOutfit = std::move(*r.outfit);
    getChild<LLUICtrl>("outfit_name")->setValue(LLSD(mOutfit.name));
    getChild<LLUICtrl>("outfit_occasion")->setValue(LLSD(mOutfit.occasion));
    std::string tags;
    for (const std::string& t : mOutfit.tags) tags += (tags.empty() ? "" : ", ") + t;
    getChild<LLUICtrl>("outfit_tags")->setValue(LLSD(tags));
    getChild<LLUICtrl>("outfit_notes")->setValue(LLSD(mOutfit.notes));
    getChild<LLUICtrl>("replace_check")->setValue(LLSD(mOutfit.replace));
    refreshItems();
    refreshPreview();
    if (!r.notes.empty())
    {
        std::string notes;
        for (const std::string& n : r.notes) notes += "\n  - " + n;
        showStatus(StatusRole::Warning, "Opened with notes:" + notes);
    }
}

void SW67FloaterOutfits::editorToOutfit()
{
    mOutfit.name = getChild<LLUICtrl>("outfit_name")->getValue().asString();
    mOutfit.occasion = getChild<LLUICtrl>("outfit_occasion")->getValue().asString();
    mOutfit.notes = getChild<LLUICtrl>("outfit_notes")->getValue().asString();
    mOutfit.replace = getChild<LLUICtrl>("replace_check")->getValue().asBoolean();
    mOutfit.tags.clear();
    std::string tags = getChild<LLUICtrl>("outfit_tags")->getValue().asString();
    size_t start = 0;
    while (start <= tags.size())
    {
        const size_t comma = std::min(tags.find(',', start), tags.size());
        std::string t = tags.substr(start, comma - start);
        LLStringUtil::trim(t);
        if (!t.empty()) mOutfit.tags.push_back(t);
        start = comma + 1;
    }
}

void SW67FloaterOutfits::refreshItems()
{
    LLScrollListCtrl* list = getChild<LLScrollListCtrl>("item_list");
    list->deleteAllItems();
    for (const Item& i : mOutfit.items)
    {
        LLSD row;
        row["value"] = i.id;
        row["columns"][0]["column"] = "kind";
        row["columns"][0]["value"] = kindLabel(i);
        row["columns"][1]["column"] = "name";
        row["columns"][1]["value"] = i.name;
        list->addElement(row);
    }
}

Plan SW67FloaterOutfits::planNow() const
{
    const std::vector<Item> worn = wornItems();
    std::map<std::string, Item> owned;
    for (const Item& i : mOutfit.items)
    {
        if (auto fresh = itemFrom(ownedItem(i.id)))
        {
            // An unworn item's point is only known from when it was saved.
            if (fresh->attachPoint.empty()) fresh->attachPoint = i.attachPoint;
            owned[fresh->id] = std::move(*fresh);
        }
    }
    Limits limits;
    limits.attachments = LLAgentBenefitsMgr::current().getAttachmentLimit();
    std::set<std::string> keep;
    if (getChild<LLUICtrl>("keep_huds")->getValue().asBoolean())
    {
        keep = wornHUDs(worn);
    }
    return planOutfit(mOutfit, worn, owned, limits, keep);
}

void SW67FloaterOutfits::refreshPreview()
{
    if (mOutfit.items.empty())
    {
        getChild<LLUICtrl>("preview_text")
            ->setValue(LLSD("Choose a saved outfit, or start one from what you are wearing now."));
        getChild<LLUICtrl>("wear_btn")->setEnabled(false);
        return;
    }
    const Plan plan = planNow();
    getChild<LLUICtrl>("preview_text")
        ->setValue(LLSD(llformat("Wearing this outfit would make %d change(s):\n\n", static_cast<int>(plan.changeCount())) +
                        plan.describe()));
    getChild<LLUICtrl>("wear_btn")->setEnabled(plan.canWear() && plan.changeCount() > 0);
}

void SW67FloaterOutfits::onCapture()
{
    const std::vector<Item> worn = wornItems();
    if (worn.empty())
    {
        showStatus(StatusRole::Warning, "Nothing worn was found yet. Try again once your outfit has loaded.");
        return;
    }
    mOutfit = outfitFromWorn("What I'm wearing", worn);
    getChild<LLUICtrl>("outfit_name")->setValue(LLSD(mOutfit.name));
    getChild<LLUICtrl>("outfit_occasion")->setValue(LLSD(""));
    getChild<LLUICtrl>("outfit_tags")->setValue(LLSD(""));
    getChild<LLUICtrl>("outfit_notes")->setValue(LLSD(""));
    getChild<LLUICtrl>("replace_check")->setValue(LLSD(true));
    refreshItems();
    refreshPreview();
    showStatus(StatusRole::Info, llformat("Started an outfit from the %d items you wear. Name it and Save.",
                                          static_cast<int>(worn.size())));
}

void SW67FloaterOutfits::onAddSelected()
{
    LLInventoryPanel* panel = LLInventoryPanel::getActiveInventoryPanel(false);
    if (!panel)
    {
        showStatus(StatusRole::Info, "Open your inventory and select the items to add first.");
        return;
    }
    int added = 0;
    int skipped = 0;
    for (LLFolderViewItem* fv : panel->getSelectedItems())
    {
        const auto* model = fv ? static_cast<LLFolderViewModelItemInventory*>(fv->getViewModelItem()) : nullptr;
        if (!model) continue;
        const LLViewerInventoryItem* item = gInventory.getItem(model->getUUID());
        if (item && item->getIsLinkType()) item = item->getLinkedItem();
        const auto i = itemFrom(item);
        if (!i)
        {
            ++skipped;
            continue;
        }
        const bool already = std::any_of(mOutfit.items.begin(), mOutfit.items.end(),
                                         [&](const Item& x) { return x.id == i->id; });
        if (!already && mOutfit.items.size() < kMaxItems)
        {
            mOutfit.items.push_back(*i);
            ++added;
        }
    }
    refreshItems();
    refreshPreview();
    showStatus(StatusRole::Info, llformat("Added %d item(s) to the outfit", added) +
                                     (skipped ? llformat("; %d selected item(s) are not wearable.", skipped) : "."));
}

void SW67FloaterOutfits::onRemoveItem()
{
    LLScrollListItem* sel = getChild<LLScrollListCtrl>("item_list")->getFirstSelected();
    if (!sel) return;
    const std::string id = sel->getValue().asString();
    mOutfit.items.erase(std::remove_if(mOutfit.items.begin(), mOutfit.items.end(),
                                       [&](const Item& i) { return i.id == id; }),
                        mOutfit.items.end());
    refreshItems();
    refreshPreview();
    showStatus(StatusRole::Info, "Taken out of this outfit. The item stays in your inventory.");
}

void SW67FloaterOutfits::onSave()
{
    editorToOutfit();
    if (mOutfit.name.empty())
    {
        showStatus(StatusRole::Warning, "Give the outfit a name first.");
        return;
    }
    const std::string path = sw67viewer::joinPath(outfitsDir(), fileNameFor(mOutfit));
    if (!sw67viewer::writeFileAtomic(path, writeOutfit(mOutfit)))
    {
        showStatus(StatusRole::Error, "Could not save " + path + ".");
        return;
    }
    refreshList();
    showStatus(StatusRole::Success, "Saved \"" + mOutfit.name + "\". It is a plain file you keep: " + path);
}

void SW67FloaterOutfits::onDelete()
{
    LLScrollListItem* sel = getChild<LLScrollListCtrl>("outfit_list")->getFirstSelected();
    if (!sel) return;
    // Only the plan file goes, and it is kept as .deleted so it can be
    // recovered. No inventory item is touched.
    const std::string path = sw67viewer::joinPath(outfitsDir(), sel->getValue().asString());
    if (LLFile::rename(path, path + ".deleted") != 0)
    {
        showStatus(StatusRole::Error, "Could not remove that outfit plan.");
        return;
    }
    refreshList();
    showStatus(StatusRole::Success, "Removed the outfit plan (kept as " + path +
                                        ".deleted). Your inventory items are untouched.");
}

void SW67FloaterOutfits::onWear()
{
    editorToOutfit();
    const Plan plan = planNow();
    if (!plan.canWear())
    {
        showStatus(StatusRole::Warning, "This outfit cannot be worn as planned. See the notes in the preview.");
        return;
    }
    // Ask RLV about every item first; if anything is blocked, change nothing.
    const std::vector<sw67::rlv::ActionRequest> requests = gateRequests(plan);
    SW67RlvState state;
    const sw67::rlv::Gate gate(state);
    const std::vector<sw67::rlv::Decision> decisions = gate.evaluateBatch(requests);
    for (size_t n = 0; n < decisions.size(); ++n)
    {
        if (!decisions[n].allowed())
        {
            showStatus(StatusRole::Restricted, "Nothing was changed: " + decisions[n].explanation);
            return;
        }
    }
    uuid_vec_t wear;
    uuid_vec_t remove;
    for (const Step& s : plan.steps)
    {
        if (s.what == Step::What::Wear) wear.push_back(LLUUID(s.item.id));
        if (s.what == Step::What::Remove && !s.implicit) remove.push_back(LLUUID(s.item.id));
    }
    if (!remove.empty())
    {
        LLAppearanceMgr::instance().removeItemsFromAvatar(remove);
    }
    if (!wear.empty())
    {
        LLAppearanceMgr::instance().wearItemsOnAvatar(wear, true, false);
    }
    showStatus(StatusRole::Info,
               llformat("Wearing %d item(s) and taking off %d. Checking each item once your appearance settles...",
                        static_cast<int>(wear.size()), static_cast<int>(remove.size())));
    mPendingPlan = plan;
    mCheckStarted.reset();
    const U32 attempt = ++mCheckAttempt;
    scheduleRestoreCheck(attempt);
}

void SW67FloaterOutfits::scheduleRestoreCheck(U32 attempt)
{
    const LLHandle<LLFloater> handle = getHandle();
    doAfterInterval(
        [handle, attempt]() {
            SW67FloaterOutfits* self = ALViewType::as<SW67FloaterOutfits>(handle.get());
            if (self && self->mCheckAttempt == attempt)
            {
                self->checkRestore(attempt);
            }
        },
        3.f);
}

std::set<std::string> SW67FloaterOutfits::wornIds(const Plan& plan)
{
    std::set<std::string> worn;
    for (const Step& s : plan.steps)
    {
        LLUUID id;
        if (!LLUUID::validate(s.item.id) || !id.set(s.item.id, false)) continue;
        const bool on = s.item.kind == ItemKind::Object
                            ? (isAgentAvatarValid() && gAgentAvatarp->isWearingAttachment(id))
                            : gAgentWearables.isWearingItem(id);
        if (on) worn.insert(s.item.id);
    }
    return worn;
}

void SW67FloaterOutfits::checkRestore(U32 attempt)
{
    if (!mPendingPlan) return;
    LLAppearanceMgr& mgr = LLAppearanceMgr::instance();
    const bool busy = mgr.isOutfitLocked() || mgr.isInUpdateAppearanceFromCOF();
    const RestoreReport report = sw67::outfit::checkRestore(*mPendingPlan, wornIds(*mPendingPlan));
    // Attachments arrive from the region one by one: wait while the viewer is
    // still busy or anything is outstanding, up to 45 seconds.
    if ((busy || !report.complete()) && mCheckStarted.getElapsedTimeF32() < 45.f)
    {
        scheduleRestoreCheck(attempt);
        return;
    }
    getChild<LLUICtrl>("preview_text")->setValue(LLSD("Outfit Doctor's check after wearing:\n\n" + report.describe()));
    if (report.complete())
    {
        showStatus(StatusRole::Success, "Restored. Every item is where the outfit wants it.");
    }
    else
    {
        showStatus(StatusRole::Warning, llformat("%d item(s) were not restored; they are listed above. Nothing else "
                                                 "was changed, and nothing was bought or deleted.",
                                                 static_cast<int>(report.failed())));
    }
    mPendingPlan.reset();
}

void SW67FloaterOutfits::showStatus(StatusRole role, const std::string& text)
{
    LLTextBox* box = getChild<LLTextBox>("status_text");
    box->setText(sw67viewer::statusText(role) + "  " + text);
    box->setColor(LLUIColor(sw67viewer::statusColor(role)));
}
