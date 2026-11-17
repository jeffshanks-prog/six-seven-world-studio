/**
 * @file entitlements.h
 * @brief Which features a subscription includes, read from a configurable
 *        plan catalogue (prices, plans, the AI Creator add-on, AI credit
 *        allowances and packs). Nothing here charges anyone: billing is a
 *        separate, server-side service, and the viewer only reads the
 *        entitlements it reports.
 *
 *        Local data is never gated: opening, reading, exporting and
 *        deleting the user's own files (creator projects, photos, workspace
 *        profiles, settings backups) works with no subscription at all.
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

#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace sw67::plan
{

/// Feature keys, as the catalogue names them. A key the viewer does not
/// know is ignored.
namespace feature
{
inline constexpr std::string_view LocalData = "local_data"; ///< Always included; see isAlwaysIncluded.
inline constexpr std::string_view WorkspaceProfiles = "workspace_profiles";
inline constexpr std::string_view PerformanceAssistant = "performance_assistant";
inline constexpr std::string_view CreatorProjects = "creator_projects";
inline constexpr std::string_view ProductPhotoManual = "product_photo_manual";
inline constexpr std::string_view PosePhotoManual = "pose_photo_manual";
inline constexpr std::string_view OutfitPlanner = "outfit_planner";
inline constexpr std::string_view RecordingPrivacy = "recording_privacy";
inline constexpr std::string_view SettingsBackup = "settings_backup";
inline constexpr std::string_view Accessibility = "accessibility"; ///< Always included.
inline constexpr std::string_view EventRehearsal = "event_rehearsal";
inline constexpr std::string_view StageLighting = "stage_lighting";
inline constexpr std::string_view PerformanceSequencer = "performance_sequencer";
inline constexpr std::string_view DJWorkspace = "dj_workspace";
inline constexpr std::string_view HostingWorkspace = "hosting_workspace";
inline constexpr std::string_view AIDirector = "ai_director";
inline constexpr std::string_view AIDirectorExpanded = "ai_director_expanded";
inline constexpr std::string_view AIScenePlanning = "ai_scene_planning";
inline constexpr std::string_view AIProductPhoto = "ai_product_photo";
inline constexpr std::string_view AIListingDrafts = "ai_listing_drafts";
inline constexpr std::string_view AIMesh = "ai_mesh";
inline constexpr std::string_view AIScript = "ai_script";
inline constexpr std::string_view AITexture = "ai_texture";
inline constexpr std::string_view Captioning = "captioning";
} // namespace feature

/// Features no subscription state can take away.
bool isAlwaysIncluded(std::string_view featureKey);

struct Plan
{
    std::string id;
    std::string name;
    int priceCentsPerMonth = 0;
    std::string currency = "USD";
    std::vector<std::string> includesPlans; ///< Plans whose features this one also has.
    std::set<std::string> features;
    int aiCreditsPerMonth = 0; ///< Metered allowance; 0 for none.
    bool isAddOn = false;
};

struct CreditPack
{
    std::string id;
    int credits = 0;
    int priceCents = 0;
};

struct Catalogue
{
    int version = 0;
    bool sandbox = true; ///< Prices are proposals; no real charges.
    std::string currency = "USD";
    std::vector<Plan> plans;
    std::vector<CreditPack> creditPacks;
    std::optional<int> trialDays; ///< Unset: no trial decided.
    const Plan* find(std::string_view id) const;
};

struct CatalogueResult
{
    std::optional<Catalogue> catalogue;
    std::vector<std::string> problems;
};

/// Reads the catalogue (app_settings/sw67_plans.json). Refuses plans that
/// include themselves in a loop, add-ons that include plans, negative
/// prices, and any plan offering unlimited AI.
CatalogueResult readCatalogue(std::string_view json);

/// What the billing service says the user has. `active` is false when it
/// lapsed or was cancelled.
struct Subscription
{
    std::string planId;
    std::set<std::string> addOns;
    bool active = false;
};

/// Every feature the subscription includes, the always-included ones too.
std::set<std::string> featuresOf(const Catalogue& catalogue, const Subscription& sub);
bool hasFeature(const Catalogue& catalogue, const Subscription& sub, std::string_view featureKey);

/// The monthly AI credit allowance: the plan's, any included plan's, and
/// each add-on's, summed. Never unlimited.
int monthlyAICredits(const Catalogue& catalogue, const Subscription& sub);

/// "$19.99" for 1999 cents.
std::string formatPrice(int cents, std::string_view currency = "USD");

} // namespace sw67::plan
