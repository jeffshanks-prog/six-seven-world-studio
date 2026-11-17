/**
 * @file gate.cpp
 * @brief The RLV gate every 6|7 World Studio automation passes through.
 *
 * Copyright (c) 2026, 6|7 World Studio contributors.
 *
 * The source code in this file is provided to you under the terms of the
 * GNU Lesser General Public License, version 2.1, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE. Terms of the LGPL can be found in doc/LGPL-license.txt
 * in this distribution, or online at http://www.gnu.org/licenses/lgpl-2.1.txt
 */

#include "sw67/rlv/gate.h"

namespace sw67::rlv
{

ActionTraits traitsOf(ActionKind kind)
{
    ActionTraits t;
    switch (kind)
    {
    case ActionKind::PayAvatar:
    case ActionKind::PayObject:
        t.spendsMoney = true;
        break;
    case ActionKind::SendIM:
    case ActionKind::StartIM:
        t.sendsMessage = true;
        break;
    case ActionKind::SendChat:
        t.sendsMessage = true;
        t.publishes = true;
        break;
    case ActionKind::Rez:
    case ActionKind::GiveInventory:
        t.publishes = true;
        break;
    case ActionKind::DeleteInventory:
        t.destructive = true;
        break;
    default:
        break;
    }
    return t;
}

bool requiresExplicitAuthorization(ActionKind kind)
{
    const ActionTraits t = traitsOf(kind);
    return t.spendsMoney || t.sendsMessage || t.publishes || t.destructive;
}

std::vector<std::string_view> behavioursFor(ActionKind kind)
{
    switch (kind)
    {
    case ActionKind::Teleport: return {"tploc", "tplm", "unsit", "sittp"};
    case ActionKind::TeleportLocal: return {"tplocal", "unsit", "sittp"};
    case ActionKind::Sit: return {"sit", "sittp", "unsit"};
    case ActionKind::Stand: return {"unsit"};
    case ActionKind::WearItem: return {"addoutfit", "addattach", "attachthis", "attachallthis", "unsharedwear", "sharedwear"};
    case ActionKind::RemoveItem: return {"remoutfit", "remattach", "detach", "detachthis", "detachallthis", "unsharedunwear", "sharedunwear"};
    case ActionKind::MoveInventory:
    case ActionKind::RenameInventory:
    case ActionKind::DeleteInventory: return {"detachthis", "detachallthis", "attachthis", "attachallthis", "showinv"};
    case ActionKind::SendIM: return {"sendim", "sendimto"};
    case ActionKind::StartIM: return {"startim", "startimto", "sendim"};
    case ActionKind::SendChat: return {"sendchat", "sendchannel", "sendchannel_except", "redirchat", "chatshout", "chatnormal", "chatwhisper"};
    case ActionKind::ChangeEnvironment: return {"setenv"};
    case ActionKind::ChangeCamera: return {"setcam", "setcam_unlock", "camunlock", "camdistmax", "camdistmin", "setcam_fovmin", "setcam_fovmax"};
    case ActionKind::TakeSnapshot: return {}; // No RLVa behaviour governs snapshots in this foundation.
    case ActionKind::Touch: return {"touchall", "touchfar", "fartouch", "touchworld", "touchattach"};
    case ActionKind::Rez: return {"rez"};
    case ActionKind::Edit: return {"edit", "editobj", "editworld", "editattach"};
    case ActionKind::PayAvatar: return {"pay"};
    case ActionKind::PayObject: return {"buy", "pay"};
    case ActionKind::GiveInventory: return {"share"};
    case ActionKind::Count: break;
    }
    return {};
}

std::string_view actionName(ActionKind kind)
{
    switch (kind)
    {
    case ActionKind::Teleport: return "Teleport";
    case ActionKind::TeleportLocal: return "Local teleport";
    case ActionKind::Sit: return "Sit";
    case ActionKind::Stand: return "Stand up";
    case ActionKind::WearItem: return "Wear item";
    case ActionKind::RemoveItem: return "Take off item";
    case ActionKind::MoveInventory: return "Move inventory";
    case ActionKind::RenameInventory: return "Rename inventory";
    case ActionKind::DeleteInventory: return "Move to trash";
    case ActionKind::SendIM: return "Send IM";
    case ActionKind::StartIM: return "Start IM";
    case ActionKind::SendChat: return "Send chat";
    case ActionKind::ChangeEnvironment: return "Change environment";
    case ActionKind::ChangeCamera: return "Change camera";
    case ActionKind::TakeSnapshot: return "Take snapshot";
    case ActionKind::Touch: return "Touch";
    case ActionKind::Rez: return "Rez";
    case ActionKind::Edit: return "Edit";
    case ActionKind::PayAvatar: return "Pay resident";
    case ActionKind::PayObject: return "Pay object";
    case ActionKind::GiveInventory: return "Give inventory";
    case ActionKind::Count: break;
    }
    return "Unknown action";
}

Decision Gate::evaluate(const ActionRequest& request) const
{
    Decision d;
    if (request.kind == ActionKind::Count)
    {
        d.verdict = Verdict::Blocked;
        d.explanation = "Unknown action; not run.";
        return d;
    }
    if (!mState.isEnabled())
    {
        d.verdict = Verdict::Allowed;
        return d;
    }
    if (!mState.isReady())
    {
        d.verdict = Verdict::NotReady;
        d.explanation = std::string(actionName(request.kind)) +
                        " is waiting: RLV is turned on but has not finished starting, so nothing runs until it can be checked.";
        return d;
    }
    if (mState.allows(request))
    {
        d.verdict = Verdict::Allowed;
        return d;
    }

    d.verdict = Verdict::Blocked;
    d.because = mState.restrictionsFor(request);
    std::string text = std::string(actionName(request.kind)) + " is blocked by an active RLV restriction";
    if (!d.because.empty())
    {
        const Restriction& r = d.because.front();
        text += " (@" + r.behaviour;
        if (!r.option.empty())
        {
            text += ":" + r.option;
        }
        text += ")";
        if (!r.sourceName.empty())
        {
            text += " held by \"" + r.sourceName + "\"";
        }
    }
    text += ". 6|7 World Studio will not work around it.";
    d.explanation = std::move(text);
    return d;
}

std::vector<Decision> Gate::evaluateBatch(const std::vector<ActionRequest>& requests) const
{
    std::vector<Decision> out;
    out.reserve(requests.size());
    for (const ActionRequest& r : requests)
    {
        out.push_back(evaluate(r));
    }
    return out;
}

std::string describeStop(std::string_view workflowName, std::size_t activeRestrictionCount)
{
    std::string text = "Stopped \"" + std::string(workflowName) + "\". No further steps will run.";
    if (activeRestrictionCount > 0)
    {
        text += " " + std::to_string(activeRestrictionCount) +
                (activeRestrictionCount == 1 ? " RLV restriction is" : " RLV restrictions are") +
                " still active: stopping a workflow does not release RLV restrictions, which belong to the objects that set them.";
    }
    return text;
}

} // namespace sw67::rlv
