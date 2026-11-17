/**
 * @file photo_test.cpp
 * @brief Product photography: camera placement, crops, watermark placement,
 *        safe and unique file names, originals kept, AI output labelled.
 *
 * Copyright (c) 2026, 6|7 World Studio contributors. LGPL-2.1.
 */

#include "sw67/photo/product.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <set>

using namespace sw67::photo;
using Catch::Approx;

TEST_CASE("Camera angles orbit the subject at the asked distance", "[photo]")
{
    const Vec3 centre{128.0, 128.0, 25.0};
    const CameraPlacement front = placeCamera({"front", 0.0, 0.0, 2.0}, centre, 1.5, 0.0);
    CHECK(front.focus.x == centre.x);
    CHECK(front.eye.x == Approx(131.0));
    CHECK(front.eye.y == Approx(128.0));
    CHECK(front.eye.z == Approx(25.0));

    // A subject turned 90 degrees: "front" follows it.
    const CameraPlacement turned = placeCamera({"front", 0.0, 0.0, 2.0}, centre, 1.5, 90.0);
    CHECK(turned.eye.x == Approx(128.0).margin(1e-9));
    CHECK(turned.eye.y == Approx(131.0));

    const CameraPlacement top = placeCamera({"top", 0.0, 89.0, 2.0}, centre, 1.5, 0.0);
    CHECK(top.eye.z == Approx(25.0 + 3.0 * std::sin(89.0 * 3.14159265358979 / 180.0)));

    // A tiny subject still leaves room for the camera.
    const CameraPlacement tiny = placeCamera({"x", 0.0, 0.0, 2.0}, centre, 0.01, 0.0);
    CHECK(tiny.eye.x - centre.x == Approx(0.5));
}

TEST_CASE("Cover crops keep the target's shape without distortion", "[photo]")
{
    CHECK(coverCrop(2048, 2048, 1080, 1080) == PixelRect{0, 0, 2048, 2048});
    CHECK(coverCrop(2048, 2048, 1920, 1080) == PixelRect{0, 448, 2048, 1152});
    CHECK(coverCrop(1920, 1080, 1080, 1080) == PixelRect{420, 0, 1080, 1080});
    CHECK(coverCrop(0, 10, 10, 10) == PixelRect{});
}

TEST_CASE("Watermarks sit inside the image in the asked corner", "[photo]")
{
    Watermark w;
    w.corner = Corner::BottomRight;
    w.widthFraction = 0.25;
    w.marginFraction = 0.05;
    const PixelRect r = placeWatermark(w, 4.0, 1000, 800);
    CHECK(r.width == 250);
    CHECK(r.height == 63);
    CHECK(r.x == 1000 - 40 - 250);
    CHECK(r.y == 800 - 40 - 63);

    // An absurd mark is shrunk to fit rather than spilling off the image.
    w.widthFraction = 5.0;
    const PixelRect tall = placeWatermark(w, 0.1, 400, 300);
    CHECK(tall.x >= 0);
    CHECK(tall.y >= 0);
    CHECK(tall.x + tall.width <= 400);
    CHECK(tall.y + tall.height <= 300);
}

TEST_CASE("File names are safe on every platform", "[photo]")
{
    CHECK(safeFileStem("Oak Bench: front/back?") == "Oak_Bench_front_back");
    CHECK(safeFileStem("..\\..\\evil") == "evil");
    CHECK(safeFileStem("con") == "_con");
    CHECK(safeFileStem("LPT1.backup") == "_LPT1.backup");
    CHECK(safeFileStem("") == "image");
    CHECK(safeFileStem("....") == "image");
    CHECK(safeFileStem(std::string(200, 'a')).size() == 80);
    // A long name of two-byte characters is cut between characters.
    std::string accents;
    for (int i = 0; i < 60; ++i) accents += "\xC3\xA9";
    const std::string cut = safeFileStem(accents);
    CHECK(cut.size() % 2 == 0);
}

TEST_CASE("Name patterns expand and stay unique", "[photo]")
{
    const std::map<std::string, std::string> v{{"project", "Oak Bench"}, {"angle", "front"}, {"date", "2026-11-17"}};
    CHECK(expandName("{project}_{angle}_{index:3}", v, 7) == "Oak_Bench_front_007");
    CHECK(expandName("{date}-{index}-{unknown}", v, 12) == "2026-11-17-12");
    CHECK(uniqueStem("a", {"a", "a-2"}) == "a-3");
    CHECK(uniqueStem("b", {"a"}) == "b");
}

TEST_CASE("A batch keeps every original and derives each export from it", "[photo]")
{
    PhotoSession s;
    s.project = "Oak Bench";
    s.angles = {builtinAngles()[0], builtinAngles()[1]};
    s.backgrounds = {"white", "garden"};
    s.lighting = {"Studio soft"};
    s.sizes = {builtinExportSizes()[0], builtinExportSizes()[4]};
    s.watermark = Watermark{"Oak & Co", "", Corner::BottomRight, 0.2, 0.03, 0.6};
    s.exportFormat = "jpg";
    s.date = "2026-11-17";

    const std::set<std::string> existing{"originals/Oak_Bench_front_001.png"};
    const auto shots = planBatch(s, existing);
    REQUIRE(shots.size() == 4);
    std::set<std::string> paths;
    for (const Shot& shot : shots)
    {
        CHECK(shot.original.original);
        CHECK(shot.original.path.rfind("originals/", 0) == 0);
        CHECK(shot.original.path.substr(shot.original.path.size() - 4) == ".png"); // lossless
        CHECK(shot.original.steps.empty());
        CHECK(paths.insert(shot.original.path).second);
        REQUIRE(shot.exports.size() == 2);
        for (const PlannedFile& f : shot.exports)
        {
            CHECK_FALSE(f.original);
            CHECK(f.derivedFrom == shot.original.path);
            CHECK(f.path.rfind("exports/", 0) == 0);
            CHECK(f.steps.back().kind == EditStep::Kind::Watermark);
            CHECK(paths.insert(f.path).second);
        }
    }
    // The existing file's name was not reused.
    CHECK(shots[0].original.path == "originals/Oak_Bench_front_001-2.png");
    // The wide export of a square capture is cropped first.
    CHECK(shots[0].exports[1].steps.front().kind == EditStep::Kind::CoverCrop);

    const std::string manifest = writeBatchManifest(s, shots);
    CHECK(manifest.find("\"original\": true") != std::string::npos);
    CHECK(manifest.find("Originals are never overwritten") != std::string::npos);
}

TEST_CASE("AI edits are labelled, kept apart and traceable to the original", "[photo]")
{
    PhotoSession s;
    s.angles = {builtinAngles()[0]};
    s.sizes = {builtinExportSizes()[0]};
    const auto shots = planBatch(s);
    REQUIRE(shots.size() == 1);

    const PlannedFile fromOriginal = planAIEdit(shots[0].original, "background removal");
    CHECK(fromOriginal.aiAltered);
    CHECK(fromOriginal.path.find(kAIMarker) != std::string::npos);
    CHECK(fromOriginal.path.rfind("exports/ai/", 0) == 0);
    CHECK(fromOriginal.derivedFrom == shots[0].original.path);
    CHECK(fromOriginal.path != shots[0].original.path);

    const PlannedFile fromExport = planAIEdit(shots[0].exports[0], "relight");
    CHECK(fromExport.derivedFrom == shots[0].original.path);
    CHECK(fromExport.steps.back().kind == EditStep::Kind::AIEdit);
    CHECK(fromExport.steps.size() == shots[0].exports[0].steps.size() + 1);
}
