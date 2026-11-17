/**
 * @file creator_test.cpp
 * @brief Creator projects: manifests round trip, paths stay in the project,
 *        inventory is linked and never claimed as backed up, and release
 *        readiness reports what is left.
 *
 * Copyright (c) 2026, 6|7 World Studio contributors. LGPL-2.1.
 */

#include "sw67/creator/project.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <set>
#include <string>

using namespace sw67::creator;

namespace
{
CreatorProject sample()
{
    CreatorProject p = newProject("Oak Bench");
    p.description = "A garden bench.";
    p.assets.push_back({AssetKind::MeshSource, "source/bench.blend", "Main model", std::nullopt});
    p.assets.push_back({AssetKind::TextureSource, "textures/oak.psd", "", std::nullopt});
    p.assets.push_back({AssetKind::Script, "scripts/sit.lsl", "", std::nullopt});
    InventoryLink link{"0b8a3c3e-1f2d-4a5b-9c8d-7e6f5a4b3c2d", "Oak Bench v2", "object", {true, true, true, true}};
    p.assets.push_back({AssetKind::Export, "", "Uploaded copy", link});
    p.exportSettings["lod.high"] = "8000 tris";
    addRevision(p, "2026-11-17", "First upload");
    p.testingNotes = "Sit target checked on three avatars.";
    return p;
}
} // namespace

TEST_CASE("A project manifest round trips", "[creator]")
{
    const CreatorProject p = sample();
    const LoadResult back = readManifest(writeManifest(p));
    REQUIRE(back.project);
    CHECK(*back.project == p);
    CHECK(back.issues.empty());
}

TEST_CASE("Project paths cannot leave the project folder", "[creator]")
{
    CHECK(isSafeRelativePath("source/bench.blend"));
    CHECK(isSafeRelativePath("a b/c.png"));
    for (const char* bad : {"", "/etc/passwd", "../outside.txt", "a/../../b", "C:/Windows/x", "a\\b", "a//b", "./a", "a/",
                            "a/\x01"})
    {
        CAPTURE(bad);
        CHECK_FALSE(isSafeRelativePath(bad));
    }
    const LoadResult r = readManifest(R"({"format":"sw67.creator.project","version":1,"name":"x",
        "assets":[{"kind":"script","path":"../../secret.lsl"},{"kind":"script","path":"ok.lsl"}]})");
    REQUIRE(r.project);
    REQUIRE(r.project->assets.size() == 1);
    CHECK(r.project->assets[0].path == "ok.lsl");
    CHECK_FALSE(r.issues.empty());
}

TEST_CASE("Inventory links are references, never claimed as backups", "[creator]")
{
    const CreatorProject p = sample();
    const ProjectAsset& linked = p.assets.back();
    CHECK(holdingOf(linked, false) == Holding::LinkOnly);
    const std::string text = describeHolding(linked, false);
    CHECK(text.find("keeps no copy") != std::string::npos);
    CHECK(text.find("backed up") == std::string::npos);
    CHECK(writeManifest(p).find("holds no copy of any Second Life asset") != std::string::npos);

    CHECK(holdingOf(p.assets[0], true) == Holding::LocalFile);
    CHECK(holdingOf(p.assets[0], false) == Holding::LocalFileMissing);
    CHECK(describePermissions({true, false, false, false}) == "Copy, no modify, no transfer. You are not the creator.");
}

TEST_CASE("Malformed links and revisions are dropped with an issue", "[creator]")
{
    const LoadResult r = readManifest(R"({"format":"sw67.creator.project","version":1,"name":"x",
        "assets":[{"kind":"export","inventoryLink":{"itemId":"not-a-uuid","name":"x"}}],
        "revisions":[{"number":0,"date":"2026-11-17"},{"number":2,"date":"17/11/2026"},{"number":3,"date":"2026-11-17","summary":"ok"}]})");
    REQUIRE(r.project);
    CHECK(r.project->assets.empty());
    REQUIRE(r.project->revisions.size() == 1);
    CHECK(r.project->revisions[0].number == 3);
    CHECK(r.issues.size() == 4);
    CHECK(isUuid("0b8a3c3e-1f2d-4a5b-9c8d-7e6f5a4b3c2d"));
    CHECK_FALSE(isUuid("0B8A3C3E-1F2D-4A5B-9C8D-7E6F5A4B3C2D"));
}

TEST_CASE("A newer manifest is refused but says the files are untouched", "[creator]")
{
    const LoadResult r = readManifest(R"({"format":"sw67.creator.project","version":7,"name":"x"})");
    CHECK_FALSE(r.project);
    REQUIRE_FALSE(r.issues.empty());
    CHECK(r.issues[0].text.find("untouched") != std::string::npos);
}

TEST_CASE("Revisions number themselves", "[creator]")
{
    CreatorProject p = newProject("x");
    CHECK(addRevision(p, "2026-11-01", "a") == 1);
    CHECK(addRevision(p, "2026-11-02", "b") == 2);
    p.revisions.push_back({10, "2026-11-03", "imported"});
    CHECK(addRevision(p, "2026-11-04", "c") == 11);
}

TEST_CASE("Release readiness lists what is left", "[creator]")
{
    CreatorProject p = sample();
    const std::set<std::string> onDisk{"source/bench.blend", "scripts/sit.lsl"};
    auto issues = releaseReadiness(p, [&](const std::string& path) { return onDisk.count(path) > 0; });
    auto has = [&](const std::string& needle) {
        return std::any_of(issues.begin(), issues.end(), [&](const ProjectIssue& i) { return i.text.find(needle) != std::string::npos; });
    };
    CHECK(has("Missing file: textures/oak.psd"));
    CHECK(has("Not done: Physics shape set and tested"));
    CHECK_FALSE(has("no testing notes"));

    p.testingNotes.clear();
    p.assets.back().inventory->permissions.userIsCreator = false;
    issues = releaseReadiness(p, [&](const std::string& path) { return onDisk.count(path) > 0; });
    CHECK(has("no testing notes"));
    CHECK(has("made by someone else"));

    for (ChecklistItem& c : p.releaseChecklist) c.done = true;
    p.testingNotes = "done";
    p.assets.pop_back();
    p.assets.erase(p.assets.begin() + 1);
    issues = releaseReadiness(p, [&](const std::string& path) { return onDisk.count(path) > 0; });
    CHECK(issues.empty());
}

TEST_CASE("Asset kinds are guessed from file extensions", "[creator]")
{
    CHECK(guessAssetKind("a/b/model.BLEND") == AssetKind::MeshSource);
    CHECK(guessAssetKind("upload.dae") == AssetKind::Export);
    CHECK(guessAssetKind("t.psd") == AssetKind::TextureSource);
    CHECK(guessAssetKind("s.luau") == AssetKind::Script);
    CHECK(guessAssetKind("ref.pdf") == AssetKind::Reference);
    CHECK(guessAssetKind("folder.v2/README") == AssetKind::Other);
}
