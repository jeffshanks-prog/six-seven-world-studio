/**
 * @file sw67rlvstate.h
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

#pragma once

#include "sw67/rlv/gate.h"

/// The viewer's RLVa, as the automation gate sees it. Every answer comes
/// from RLVa's own checks (RlvActions, the lock managers); this class only
/// maps 6|7 actions onto them. Where RLVa has no specific check for an
/// action, the adapter treats any restriction on the relevant behaviours
/// as blocking, so it errs toward not acting.
class SW67RlvState final : public sw67::rlv::IRlvState
{
public:
    bool isEnabled() const override;
    bool isReady() const override;
    bool allows(const sw67::rlv::ActionRequest& request) const override;
    std::vector<sw67::rlv::Restriction> restrictionsFor(const sw67::rlv::ActionRequest& request) const override;
    std::vector<sw67::rlv::Restriction> activeRestrictions() const override;

    /// The gate over this state, for callers that just want a decision.
    static sw67::rlv::Decision check(const sw67::rlv::ActionRequest& request);
};
