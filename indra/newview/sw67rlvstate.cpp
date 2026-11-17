/**
 * @file sw67rlvstate.cpp
 * @brief Answers sw67::rlv::IRlvState from the viewer's RLVa.
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

#include "sw67rlvstate.h"

#include "llinventorymodel.h"
#include "llstartup.h"
#include "llviewercontrol.h"
#include "llviewerinventory.h"
#include "llviewerobjectlist.h"

#include "rlvactions.h"
#include "rlvcommon.h"
#include "rlvhandler.h"
#include "rlvhelper.h"
#include "rlvlocks.h"

#include <algorithm>
#include <string_view>

using sw67::rlv::ActionKind;
using sw67::rlv::ActionRequest;
using sw67::rlv::Restriction;

namespace
{
LLUUID toId(const std::string& s)
{
    return LLUUID::validate(s) ? LLUUID(s) : LLUUID::null;
}

/// True when any of the behaviours an action names is held at all. The
/// conservative fallback where RLVa has no check of its own.
bool anyHeld(ActionKind kind)
{
    for (std::string_view name : sw67::rlv::behavioursFor(kind))
    {
        for (const auto& [id, object] : gRlvHandler.getObjectMap())
        {
            for (const RlvCommand& cmd : object.getCommandList())
            {
                if (cmd.getBehaviour() == name)
                {
                    return true;
                }
            }
        }
    }
    return false;
}

Restriction toRestriction(const RlvCommand& cmd)
{
    Restriction r;
    r.behaviour = cmd.getBehaviour();
    r.option = cmd.getOption();
    r.sourceId = cmd.getObjectID().asString();
    // Object names need a lookup the viewer may not have made yet; the
    // restrictions floater (rlv_behaviours) shows them where known.
    return r;
}
} // namespace

bool SW67RlvState::isEnabled() const
{
    return gSavedSettings.getBOOL(RlvSettingNames::Main);
}

bool SW67RlvState::isReady() const
{
    // RLVa enables itself at startup only; a preference turned on during the
    // session is not live until the next login, so it is not ready either.
    return RlvHandler::isEnabled() && LLStartUp::getStartupState() >= STATE_STARTED;
}

bool SW67RlvState::allows(const ActionRequest& request) const
{
    const LLUUID target = toId(request.targetId);
    switch (request.kind)
    {
    case ActionKind::Teleport:
        return RlvActions::canTeleportToLocation();
    case ActionKind::TeleportLocal:
        return !gRlvHandler.hasBehaviour(RLV_BHVR_TPLOCAL) && RlvActions::canTeleportToLocation();
    case ActionKind::Sit:
    {
        const LLViewerObject* obj = gObjectList.findObject(target);
        return obj ? RlvActions::canSit(obj) : !anyHeld(ActionKind::Sit);
    }
    case ActionKind::Stand:
        return RlvActions::canStand();
    case ActionKind::WearItem:
    {
        const LLViewerInventoryItem* item = gInventory.getItem(target);
        return item && rlvPredCanWearItem(item, RLV_WEAR);
    }
    case ActionKind::RemoveItem:
    {
        const LLViewerInventoryItem* item = gInventory.getItem(target);
        return item && rlvPredCanRemoveItem(item);
    }
    case ActionKind::MoveInventory:
    {
        const LLUUID dest = toId(request.destinationId);
        if (gInventory.getCategory(target))
        {
            return RlvFolderLocks::instance().canMoveFolder(target, dest);
        }
        return gInventory.getItem(target) && RlvFolderLocks::instance().canMoveItem(target, dest);
    }
    case ActionKind::RenameInventory:
        if (gInventory.getCategory(target))
        {
            return RlvFolderLocks::instance().canRenameFolder(target);
        }
        return gInventory.getItem(target) && RlvFolderLocks::instance().canRemoveItem(target);
    case ActionKind::DeleteInventory:
        if (gInventory.getCategory(target))
        {
            return RlvFolderLocks::instance().canRemoveFolder(target);
        }
        return gInventory.getItem(target) && RlvFolderLocks::instance().canRemoveItem(target);
    case ActionKind::SendIM:
        return target.notNull() && RlvActions::canSendIM(target);
    case ActionKind::StartIM:
        return target.notNull() && RlvActions::canStartIM(target);
    case ActionKind::SendChat:
        if (request.channel == 0)
        {
            return !gRlvHandler.hasBehaviour(RLV_BHVR_SENDCHAT) && !gRlvHandler.hasBehaviour(RLV_BHVR_REDIRCHAT);
        }
        return RlvActions::canSendChannel(request.channel);
    case ActionKind::ChangeEnvironment:
        return RlvActions::canChangeEnvironment();
    case ActionKind::ChangeCamera:
        return RlvActions::canChangeCameraPreset(LLUUID::null) && !gRlvHandler.hasBehaviour(RLV_BHVR_SETCAM);
    case ActionKind::TakeSnapshot:
        return true; // No RLVa behaviour governs snapshots in this foundation.
    case ActionKind::Touch:
    {
        const LLViewerObject* obj = gObjectList.findObject(target);
        return obj && RlvActions::canTouch(obj);
    }
    case ActionKind::Rez:
        return RlvActions::canRez();
    case ActionKind::Edit:
    {
        const LLViewerObject* obj = gObjectList.findObject(target);
        return obj && RlvActions::canEdit(obj);
    }
    case ActionKind::PayAvatar:
        return target.notNull() && RlvActions::canPayAvatar(target);
    case ActionKind::PayObject:
        return target.notNull() && RlvActions::canPayObject(target);
    case ActionKind::GiveInventory:
        return target.notNull() && RlvActions::canGiveInventory(target);
    case ActionKind::Count:
        break;
    }
    return false;
}

std::vector<Restriction> SW67RlvState::restrictionsFor(const ActionRequest& request) const
{
    std::vector<Restriction> out;
    const auto names = sw67::rlv::behavioursFor(request.kind);
    for (const auto& [id, object] : gRlvHandler.getObjectMap())
    {
        for (const RlvCommand& cmd : object.getCommandList())
        {
            if (std::find(names.begin(), names.end(), std::string_view(cmd.getBehaviour())) != names.end())
            {
                out.push_back(toRestriction(cmd));
            }
        }
    }
    return out;
}

std::vector<Restriction> SW67RlvState::activeRestrictions() const
{
    std::vector<Restriction> out;
    for (const auto& [id, object] : gRlvHandler.getObjectMap())
    {
        for (const RlvCommand& cmd : object.getCommandList())
        {
            out.push_back(toRestriction(cmd));
        }
    }
    return out;
}

sw67::rlv::Decision SW67RlvState::check(const ActionRequest& request)
{
    static const SW67RlvState state;
    return sw67::rlv::Gate(state).evaluate(request);
}
