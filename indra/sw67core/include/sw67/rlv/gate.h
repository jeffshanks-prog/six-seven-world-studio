/**
 * @file gate.h
 * @brief The RLV gate every 6|7 World Studio automation passes through.
 *
 * Automation never decides for itself whether RLV allows an action: it asks
 * the gate, which asks the viewer's RLVa through an IRlvState adapter
 * (newview/sw67rlvstate.cpp). With RLV turned off the gate allows
 * everything RLV would have governed; with RLV on and the state not yet
 * known (before login completes), it fails closed.
 *
 * Stopping a 6|7 workflow never releases an RLV restriction: restrictions
 * belong to the in-world objects that set them. describeStop() is the one
 * place the UI takes that wording from.
 *
 * Copyright (c) 2026, 6|7 World Studio contributors.
 *
 * The source code in this file is provided to you under the terms of the
 * GNU Lesser General Public License, version 2.1, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE. Terms of the LGPL can be found in doc/LGPL-license.txt
 * in this distribution, or online at http://www.gnu.org/licenses/lgpl-2.1.txt
 */

#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sw67::rlv
{

/// The kinds of action 6|7 automation can request.
enum class ActionKind
{
    Teleport,          ///< To a landmark or location.
    TeleportLocal,     ///< Within the region / sim-local.
    Sit,
    Stand,
    WearItem,          ///< Wear or attach an inventory item.
    RemoveItem,        ///< Take off or detach an item.
    MoveInventory,
    RenameInventory,
    DeleteInventory,   ///< Move to trash.
    SendIM,
    StartIM,
    SendChat,          ///< Local chat or a channel.
    ChangeEnvironment, ///< Local environment / windlight.
    ChangeCamera,
    TakeSnapshot,
    Touch,
    Rez,
    Edit,
    PayAvatar,
    PayObject,
    GiveInventory,
    Count
};

/// What else an action is besides an RLV question: whether it needs the
/// user's explicit authorization in the automation engine.
struct ActionTraits
{
    bool spendsMoney = false;
    bool sendsMessage = false;
    bool publishes = false;    ///< Visible to others: rez, give, chat.
    bool destructive = false;  ///< Hard or impossible to undo.
};

ActionTraits traitsOf(ActionKind kind);

/// True when the automation engine must hold the action for the user's
/// explicit authorization (once per action, or once for a defined batch).
bool requiresExplicitAuthorization(ActionKind kind);

/// A request, as specific as the action allows. Ids are UUID strings.
struct ActionRequest
{
    ActionKind kind = ActionKind::Count;
    std::string targetId;     ///< Item, folder, object or avatar.
    std::string destinationId;///< Destination folder, for moves.
    int channel = 0;          ///< For SendChat.
    std::string location;     ///< For teleports: "Region/x/y/z".
};

/// One active RLV restriction, for display.
struct Restriction
{
    std::string behaviour;   ///< e.g. "tploc".
    std::string option;      ///< e.g. a UUID or "" for none.
    std::string sourceId;    ///< The object that holds it.
    std::string sourceName;  ///< That object's name, when known.
};

/// The viewer side of the gate. The newview adapter answers with RlvActions
/// and the lock managers; tests answer with a fake.
class IRlvState
{
public:
    virtual ~IRlvState() = default;

    /// RLV turned on in preferences (takes effect from the next login).
    virtual bool isEnabled() const = 0;

    /// RLVa has initialised for this session, so its answers mean something.
    virtual bool isReady() const = 0;

    /// Whether RLV allows the action. Only asked when enabled and ready.
    virtual bool allows(const ActionRequest& request) const = 0;

    /// The restrictions that bear on an action, for the explanation.
    virtual std::vector<Restriction> restrictionsFor(const ActionRequest& request) const = 0;

    /// Every active restriction, for the visibility panel.
    virtual std::vector<Restriction> activeRestrictions() const = 0;
};

enum class Verdict
{
    Allowed,
    Blocked,        ///< RLV forbids it now.
    NotReady,       ///< RLV is on but not yet initialised: fail closed.
};

struct Decision
{
    Verdict verdict = Verdict::NotReady;
    std::vector<Restriction> because;  ///< The restrictions, when Blocked.
    std::string explanation;           ///< User-facing text.

    bool allowed() const { return verdict == Verdict::Allowed; }
};

class Gate
{
public:
    explicit Gate(const IRlvState& state) : mState(state) {}

    Decision evaluate(const ActionRequest& request) const;

    /// Evaluates a batch: every item gets its own decision, in order, so a
    /// preview can show which items RLV will block before anything runs.
    std::vector<Decision> evaluateBatch(const std::vector<ActionRequest>& requests) const;

private:
    const IRlvState& mState;
};

/// The RLV behaviours that can bear on an action, by their command names.
/// For documentation and for explaining a block when the adapter does not
/// name the restriction itself.
std::vector<std::string_view> behavioursFor(ActionKind kind);

std::string_view actionName(ActionKind kind);

/// The text the UI shows after the user stops a 6|7 workflow. It says what
/// stopped and, when RLV restrictions are active, that they stay in place.
std::string describeStop(std::string_view workflowName, std::size_t activeRestrictionCount);

} // namespace sw67::rlv
