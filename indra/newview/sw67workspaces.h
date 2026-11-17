/**
 * @file sw67workspaces.h
 * @brief Connects sw67core's workspace profiles to the viewer: reads which
 *        windows are open and where, the toolbar and shortcut files, and
 *        preference values; applies a previewed plan; stores the user's
 *        saved profiles in <user settings>/sw67/workspaces.
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

#include "sw67/workspace/profile.h"

#include <string>
#include <vector>

namespace sw67viewer
{
/// The viewer as the profile's plan needs to see it: every window the
/// profile names, every open window, the preferences it names, and the
/// toolbar and shortcut files.
sw67::workspace::CurrentState captureWorkspace(const sw67::workspace::WorkspaceProfile& profile);

/// The current layout as a new profile: open windows with their positions,
/// the toolbar layout and shortcuts. Closes others when applied.
sw67::workspace::WorkspaceProfile profileFromCurrent(const std::string& name);

/// Runs a plan from planApply. Returns how many changes failed.
int applyWorkspacePlan(const std::vector<sw67::workspace::Change>& plan,
                       const sw67::workspace::WorkspaceProfile& profile);

std::string workspacesDir();
std::vector<sw67::workspace::WorkspaceProfile> savedWorkspaces();
bool saveWorkspace(const sw67::workspace::WorkspaceProfile& profile, std::vector<sw67::workspace::Note>* notes = nullptr);
bool deleteWorkspace(const std::string& name);
} // namespace sw67viewer
