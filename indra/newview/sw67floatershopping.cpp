/**
 * @file sw67floatershopping.cpp
 * @brief Shopping and Demo Organizer floater.
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

#include "sw67floatershopping.h"

#include "sw67accessibility.h"
#include "sw67rlvstate.h"
#include "sw67viewerfiles.h"

#include "llagent.h"
#include "llappearancemgr.h"
#include "llcombobox.h"
#include "lldate.h"
#include "llfloaterreg.h"
#include "llfloaterworldmap.h"
#include "llfolderviewitem.h"
#include "llfolderviewmodelinventory.h"
#include "llformat.h"
#include "llinventorymodel.h"
#include "llinventorypanel.h"
#include "llscrolllistctrl.h"
#include "lltextbox.h"
#include "lluictrl.h"
#include "llviewerinventory.h"
#include "llviewerregion.h"

#include <algorithm>
#include <cstdlib>

using namespace sw67::shop;
using sw67::a11y::StatusRole;

namespace
{
std::string today()
{
    return LLDate::now().toHTTPDateString(std::string("%Y-%m-%d"));
}

LLUUID toId(const std::string& id)
{
    LLUUID out;
    if (LLUUID::validate(id)) out.set(id, false);
    return out;
}

void addRow(LLScrollListCtrl* list, S32 value, const std::string& a, const std::string& b, const std::string& c,
            const std::string& d)
{
    LLSD row;
    row["value"] = value;
    const char* cols[] = {"name", "where", "status", "tags"};
    const std::string vals[] = {a, b, c, d};
    for (S32 n = 0; n < 4; ++n)
    {
        row["columns"][n]["column"] = cols[n];
        row["columns"][n]["value"] = vals[n];
    }
    list->addElement(row);
}
} // namespace

SW67FloaterShopping::SW67FloaterShopping(const LLSD& key)
:   LLFloater(key)
{
}

bool SW67FloaterShopping::postBuild()
{
    auto bind = [this](const char* name, void (SW67FloaterShopping::*fn)()) {
        getChild<LLUICtrl>(name)->setCommitCallback([this, fn](LLUICtrl*, const LLSD&) { (this->*fn)(); });
    };
    bind("view_combo", &SW67FloaterShopping::onViewChanged);
    bind("filter", &SW67FloaterShopping::refresh);
    bind("entry_list", &SW67FloaterShopping::loadEntry);
    bind("add_btn", &SW67FloaterShopping::onAdd);
    bind("update_btn", &SW67FloaterShopping::onUpdate);
    bind("remove_btn", &SW67FloaterShopping::onRemove);
    bind("here_btn", &SW67FloaterShopping::onHere);
    bind("from_inventory_btn", &SW67FloaterShopping::onFromInventory);
    bind("map_btn", &SW67FloaterShopping::onShowOnMap);
    bind("wear_btn", &SW67FloaterShopping::onWearDemo);
    return LLFloater::postBuild();
}

void SW67FloaterShopping::onOpen(const LLSD& key)
{
    LLFloater::onOpen(key);
    if (const auto text = sw67viewer::readFile(filePath(), 4 * 1024 * 1024))
    {
        ReadResult r = readOrganizer(*text);
        if (r.organizer) mData = std::move(*r.organizer);
    }
    onViewChanged();
}

std::string SW67FloaterShopping::filePath()
{
    return sw67viewer::joinPath(sw67viewer::userDir("shopping"), "organizer.sw67shopping.json");
}

bool SW67FloaterShopping::save()
{
    if (!sw67viewer::writeFileAtomic(filePath(), writeOrganizer(mData)))
    {
        showStatus(StatusRole::Error, "Could not save " + filePath() + ".");
        return false;
    }
    return true;
}

SW67FloaterShopping::View SW67FloaterShopping::view() const
{
    const std::string v = getChild<LLUICtrl>("view_combo")->getValue().asString();
    return v == "list" ? View::List : v == "demos" ? View::Demos : View::Stores;
}

S32 SW67FloaterShopping::selected() const
{
    LLScrollListItem* sel = getChild<LLScrollListCtrl>("entry_list")->getFirstSelected();
    return sel ? sel->getValue().asInteger() : -1;
}

void SW67FloaterShopping::onViewChanged()
{
    LLComboBox* status = getChild<LLComboBox>("entry_status");
    status->removeall();
    const View v = view();
    if (v == View::List)
    {
        for (const char* s : {"want", "bought", "skip"}) status->add(s, LLSD(s));
        getChild<LLUICtrl>("extra_label")->setValue(LLSD("Price (L$, a note):"));
    }
    else if (v == View::Demos)
    {
        for (const char* s : {"untried", "like", "maybe", "no"}) status->add(s, LLSD(s));
        getChild<LLUICtrl>("extra_label")->setValue(LLSD("Tried on:"));
    }
    else
    {
        status->add("-", LLSD(""));
        getChild<LLUICtrl>("extra_label")->setValue(LLSD("Location:"));
    }
    status->selectFirstItem();
    getChild<LLUICtrl>("here_btn")->setEnabled(v == View::Stores);
    getChild<LLUICtrl>("map_btn")->setEnabled(v == View::Stores);
    getChild<LLUICtrl>("wear_btn")->setEnabled(v == View::Demos);
    getChild<LLUICtrl>("from_inventory_btn")->setEnabled(v != View::List);
    refresh();
}

void SW67FloaterShopping::refresh()
{
    LLScrollListCtrl* list = getChild<LLScrollListCtrl>("entry_list");
    list->deleteAllItems();
    const std::string q = getChild<LLUICtrl>("filter")->getValue().asString();
    const View v = view();
    if (v == View::Stores)
    {
        for (size_t i = 0; i < mData.stores.size(); ++i)
        {
            const Store& s = mData.stores[i];
            if (!matches(q, s.name, s.notes, s.tags)) continue;
            addRow(list, static_cast<S32>(i), s.name,
                   s.location ? s.location->region : (s.landmarkItemId.empty() ? "" : "landmark"), "",
                   formatTags(s.tags));
        }
    }
    else if (v == View::List)
    {
        for (size_t i = 0; i < mData.list.size(); ++i)
        {
            const ListItem& it = mData.list[i];
            if (!matches(q, it.name + " " + it.store, it.notes, it.tags)) continue;
            addRow(list, static_cast<S32>(i), it.name, it.store,
                   std::string(statusKey(it.status)) + (it.priceL ? llformat(" (L$%d)", it.priceL) : ""),
                   formatTags(it.tags));
        }
    }
    else
    {
        for (size_t i = 0; i < mData.demos.size(); ++i)
        {
            const Demo& d = mData.demos[i];
            if (!matches(q, d.name + " " + d.store, d.notes, d.tags)) continue;
            addRow(list, static_cast<S32>(i), d.name, d.store, std::string(verdictKey(d.verdict)), formatTags(d.tags));
        }
    }
    std::string summary = llformat("%d store(s), %d on the list (L$%d still wanted, as a note), %d demo(s).",
                                   static_cast<int>(mData.stores.size()), static_cast<int>(mData.list.size()),
                                   mData.wantedTotalL(), static_cast<int>(mData.demos.size()));
    const auto liked = likedNotBought(mData);
    if (!liked.empty())
    {
        summary += llformat(" %d liked demo(s) not marked bought:", static_cast<int>(liked.size()));
        for (const Demo* d : liked) summary += " " + d->name + ";";
    }
    getChild<LLUICtrl>("summary_text")->setValue(LLSD(summary));
}

void SW67FloaterShopping::loadEntry()
{
    const S32 n = selected();
    if (n < 0) return;
    const auto set = [this](const char* name, const std::string& v) { getChild<LLUICtrl>(name)->setValue(LLSD(v)); };
    const View v = view();
    if (v == View::Stores && static_cast<size_t>(n) < mData.stores.size())
    {
        const Store& s = mData.stores[static_cast<size_t>(n)];
        set("entry_name", s.name);
        set("entry_store", "");
        set("entry_extra", s.location ? s.location->mapUrl() : "");
        set("entry_tags", formatTags(s.tags));
        set("entry_notes", s.notes);
    }
    else if (v == View::List && static_cast<size_t>(n) < mData.list.size())
    {
        const ListItem& it = mData.list[static_cast<size_t>(n)];
        set("entry_name", it.name);
        set("entry_store", it.store);
        set("entry_extra", llformat("%d", it.priceL));
        getChild<LLUICtrl>("entry_status")->setValue(LLSD(std::string(statusKey(it.status))));
        set("entry_tags", formatTags(it.tags));
        set("entry_notes", it.notes);
    }
    else if (v == View::Demos && static_cast<size_t>(n) < mData.demos.size())
    {
        const Demo& d = mData.demos[static_cast<size_t>(n)];
        set("entry_name", d.name);
        set("entry_store", d.store);
        set("entry_extra", d.triedOn);
        getChild<LLUICtrl>("entry_status")->setValue(LLSD(std::string(verdictKey(d.verdict))));
        set("entry_tags", formatTags(d.tags));
        set("entry_notes", d.notes);
    }
}

void SW67FloaterShopping::onAdd()
{
    const View v = view();
    const std::string name = getChild<LLUICtrl>("entry_name")->getValue().asString();
    if (name.empty())
    {
        showStatus(StatusRole::Warning, "Give it a name first.");
        return;
    }
    if (v == View::Stores && mData.stores.size() < kMaxEntries) mData.stores.push_back(Store{name, std::nullopt, "", "", {}});
    else if (v == View::List && mData.list.size() < kMaxEntries) mData.list.push_back(ListItem{name, "", 0, ListItem::Status::Want, "", {}});
    else if (v == View::Demos && mData.demos.size() < kMaxEntries) mData.demos.push_back(Demo{"", name, "", today(), Demo::Verdict::Untried, "", {}});
    else return;
    const S32 last = static_cast<S32>((v == View::Stores ? mData.stores.size() : v == View::List ? mData.list.size() : mData.demos.size()) - 1);
    refresh();
    getChild<LLScrollListCtrl>("entry_list")->selectByValue(LLSD(last));
    onUpdate();
}

void SW67FloaterShopping::onUpdate()
{
    const S32 n = selected();
    if (n < 0)
    {
        showStatus(StatusRole::Info, "Select an entry to update, or Add a new one.");
        return;
    }
    const std::string name = getChild<LLUICtrl>("entry_name")->getValue().asString();
    const std::string store = getChild<LLUICtrl>("entry_store")->getValue().asString();
    const std::string extra = getChild<LLUICtrl>("entry_extra")->getValue().asString();
    const std::string status = getChild<LLUICtrl>("entry_status")->getValue().asString();
    const std::vector<Tag> tags = parseTags(getChild<LLUICtrl>("entry_tags")->getValue().asString());
    const std::string notes = getChild<LLUICtrl>("entry_notes")->getValue().asString();
    const View v = view();
    if (v == View::Stores && static_cast<size_t>(n) < mData.stores.size())
    {
        Store& s = mData.stores[static_cast<size_t>(n)];
        s.name = name;
        if (!extra.empty())
        {
            const auto loc = parseLocation(extra);
            if (!loc)
            {
                showStatus(StatusRole::Warning, "That location is not a Second Life address (a SLURL or maps.secondlife.com link).");
                return;
            }
            s.location = loc;
        }
        s.tags = tags;
        s.notes = notes;
    }
    else if (v == View::List && static_cast<size_t>(n) < mData.list.size())
    {
        ListItem& it = mData.list[static_cast<size_t>(n)];
        it.name = name;
        it.store = store;
        it.priceL = std::max(0, std::atoi(extra.c_str()));
        it.status = status == "bought" ? ListItem::Status::Bought : status == "skip" ? ListItem::Status::Skip : ListItem::Status::Want;
        it.tags = tags;
        it.notes = notes;
    }
    else if (v == View::Demos && static_cast<size_t>(n) < mData.demos.size())
    {
        Demo& d = mData.demos[static_cast<size_t>(n)];
        d.name = name;
        d.store = store;
        d.triedOn = extra.substr(0, 10);
        d.verdict = status == "like" ? Demo::Verdict::Like : status == "maybe" ? Demo::Verdict::Maybe
                  : status == "no"   ? Demo::Verdict::No   : Demo::Verdict::Untried;
        d.tags = tags;
        d.notes = notes;
    }
    if (save())
    {
        refresh();
        showStatus(StatusRole::Success, "Saved.");
    }
}

void SW67FloaterShopping::onRemove()
{
    const S32 n = selected();
    if (n < 0) return;
    const View v = view();
    if (v == View::Stores && static_cast<size_t>(n) < mData.stores.size()) mData.stores.erase(mData.stores.begin() + n);
    else if (v == View::List && static_cast<size_t>(n) < mData.list.size()) mData.list.erase(mData.list.begin() + n);
    else if (v == View::Demos && static_cast<size_t>(n) < mData.demos.size()) mData.demos.erase(mData.demos.begin() + n);
    if (save())
    {
        refresh();
        showStatus(StatusRole::Success, "Removed from the organizer. Nothing in your inventory changed.");
    }
}

void SW67FloaterShopping::onHere()
{
    const LLViewerRegion* region = gAgent.getRegion();
    if (!region) return;
    const LLVector3& pos = gAgent.getPositionAgent();
    Location loc;
    loc.region = region->getName();
    loc.x = static_cast<int>(pos.mV[VX]);
    loc.y = static_cast<int>(pos.mV[VY]);
    loc.z = static_cast<int>(pos.mV[VZ]);
    getChild<LLUICtrl>("entry_extra")->setValue(LLSD(loc.mapUrl()));
    if (getChild<LLUICtrl>("entry_name")->getValue().asString().empty())
    {
        getChild<LLUICtrl>("entry_name")->setValue(LLSD(loc.region));
    }
    showStatus(StatusRole::Info, "Location filled in from where you stand. Add or Update to keep it.");
}

void SW67FloaterShopping::onFromInventory()
{
    LLInventoryPanel* panel = LLInventoryPanel::getActiveInventoryPanel(false);
    if (!panel)
    {
        showStatus(StatusRole::Info, "Open your inventory and select items first.");
        return;
    }
    const View v = view();
    int added = 0;
    for (LLFolderViewItem* fv : panel->getSelectedItems())
    {
        const auto* model = fv ? static_cast<LLFolderViewModelItemInventory*>(fv->getViewModelItem()) : nullptr;
        const LLViewerInventoryItem* item = model ? gInventory.getItem(model->getUUID()) : nullptr;
        if (item && item->getIsLinkType()) item = item->getLinkedItem();
        if (!item) continue;
        if (v == View::Stores && item->getType() == LLAssetType::AT_LANDMARK && mData.stores.size() < kMaxEntries)
        {
            mData.stores.push_back(Store{item->getName(), std::nullopt, item->getUUID().asString(), "", {}});
            ++added;
        }
        else if (v == View::Demos && mData.demos.size() < kMaxEntries)
        {
            const LLAssetType::EType t = item->getType();
            if (t != LLAssetType::AT_OBJECT && t != LLAssetType::AT_CLOTHING && t != LLAssetType::AT_BODYPART) continue;
            mData.demos.push_back(Demo{item->getUUID().asString(), item->getName(), "", today(), Demo::Verdict::Untried, "", {}});
            ++added;
        }
    }
    if (added && save()) refresh();
    showStatus(added ? StatusRole::Success : StatusRole::Info,
               added ? llformat("Added %d.", added)
                     : (v == View::Stores ? "Select landmarks in your inventory." : "Select wearable demos in your inventory."));
}

void SW67FloaterShopping::onShowOnMap()
{
    const S32 n = selected();
    if (view() != View::Stores || n < 0 || static_cast<size_t>(n) >= mData.stores.size()) return;
    const Store& s = mData.stores[static_cast<size_t>(n)];
    LLFloaterWorldMap* map = LLFloaterWorldMap::getInstance();
    if (!map) return;
    if (s.location)
    {
        map->trackURL(s.location->region, s.location->x, s.location->y, s.location->z);
    }
    else if (!s.landmarkItemId.empty())
    {
        map->trackLandmark(toId(s.landmarkItemId));
    }
    else
    {
        showStatus(StatusRole::Info, "This store has no location yet.");
        return;
    }
    LLFloaterReg::showInstance("world_map", LLSD("center"));
    showStatus(StatusRole::Info, "Shown on the map. Teleport from there if you want to go.");
}

void SW67FloaterShopping::onWearDemo()
{
    const S32 n = selected();
    if (view() != View::Demos || n < 0 || static_cast<size_t>(n) >= mData.demos.size()) return;
    Demo& d = mData.demos[static_cast<size_t>(n)];
    const LLUUID id = toId(d.itemId);
    if (id.isNull() || !gInventory.getItem(id))
    {
        showStatus(StatusRole::Info, "This demo is not in your inventory.");
        return;
    }
    sw67::rlv::ActionRequest req;
    req.kind = sw67::rlv::ActionKind::WearItem;
    req.targetId = d.itemId;
    const sw67::rlv::Decision dec = SW67RlvState::check(req);
    if (!dec.allowed())
    {
        showStatus(StatusRole::Restricted, dec.explanation);
        return;
    }
    LLAppearanceMgr::instance().wearItemOnAvatar(id, true, false);
    d.triedOn = today();
    save();
    refresh();
    showStatus(StatusRole::Success, "Wearing \"" + d.name + "\" (added to what you wear). Mark it like, maybe or no.");
}

void SW67FloaterShopping::showStatus(StatusRole role, const std::string& text)
{
    LLTextBox* box = getChild<LLTextBox>("status_text");
    box->setText(sw67viewer::statusText(role) + "  " + text);
    box->setColor(LLUIColor(sw67viewer::statusColor(role)));
}
