/**
 * @file director_test.cpp
 * @brief Director Mode: paths pass through their keys, ease smoothly, cut
 *        under reduced motion, replay around a moved subject; frame plans,
 *        shot lists and files.
 *
 * Copyright (c) 2026, 6|7 World Studio contributors. LGPL-2.1.
 */

#include "sw67/director/director.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace sw67::director;
using sw67::pose::Shot;
using sw67::pose::Subject;
using Catch::Approx;

namespace
{
Shot at(double x, double y, double z, double fov = 60.0)
{
    Shot s;
    s.eyeOffset = {x, y, z};
    s.focusOffset = {0.0, 0.0, 1.0};
    s.fovDeg = fov;
    return s;
}

CameraPath orbit()
{
    CameraPath p;
    p.name = "Orbit";
    p.keys = {{0.0, at(3, 0, 1.5)}, {2.0, at(0, 3, 1.5)}, {4.0, at(-3, 0, 1.5, 40.0)}, {6.0, at(0, -3, 1.5)}};
    return p;
}
} // namespace

TEST_CASE("A path passes through every key at its time", "[director]")
{
    const CameraPath p = orbit();
    REQUIRE(validate(p).empty());
    REQUIRE(p.duration() == Approx(6.0));
    for (const Key& k : p.keys)
    {
        const Shot s = sample(p, k.atSeconds);
        REQUIRE(s.eyeOffset.x == Approx(k.shot.eyeOffset.x).margin(1e-9));
        REQUIRE(s.eyeOffset.y == Approx(k.shot.eyeOffset.y).margin(1e-9));
        REQUIRE(s.fovDeg == Approx(k.shot.fovDeg));
    }
    // Before the start and after the end it holds.
    REQUIRE(sample(p, -1.0).eyeOffset.x == Approx(3.0));
    REQUIRE(sample(p, 99.0).eyeOffset.y == Approx(-3.0));
}

TEST_CASE("Smooth easing moves continuously and slows into each key", "[director]")
{
    const CameraPath p = orbit();
    double maxJump = 0.0;
    for (int n = 1; n <= 600; ++n)
    {
        const Shot a = sample(p, (n - 1) * 0.01);
        const Shot b = sample(p, n * 0.01);
        maxJump = std::max(maxJump, std::hypot(b.eyeOffset.x - a.eyeOffset.x, b.eyeOffset.y - a.eyeOffset.y));
    }
    REQUIRE(maxJump < 0.1); // no jumps at 100 samples a second
    // Near a key the camera barely moves; mid-segment it moves most.
    const auto speed = [&](double t) {
        const Shot a = sample(p, t - 0.005), b = sample(p, t + 0.005);
        return std::hypot(b.eyeOffset.x - a.eyeOffset.x, b.eyeOffset.y - a.eyeOffset.y);
    };
    REQUIRE(speed(2.0) < speed(1.0) * 0.1);
    // The field of view moves between keys 2 and 3.
    REQUIRE(sample(p, 3.0).fovDeg < 60.0);
    REQUIRE(sample(p, 3.0).fovDeg > 40.0);
}

TEST_CASE("Reduced motion cuts between keys with no movement", "[director]")
{
    const CameraPath p = orbit();
    for (double t : {0.0, 0.5, 1.99})
        REQUIRE(sample(p, t, true).eyeOffset.x == Approx(3.0));
    REQUIRE(sample(p, 2.0, true).eyeOffset.y == Approx(3.0));
    REQUIRE(sample(p, 3.9, true).eyeOffset.y == Approx(3.0));
}

TEST_CASE("A path replays around a subject that moved and turned", "[director]")
{
    const CameraPath p = orbit();
    Subject here{{128, 128, 20}, 0.0, 1.8};
    Subject there{{40, 200, 50}, 90.0, 1.8};
    const auto a = sampleWorld(p, 1.3, here);
    const auto b = sampleWorld(p, 1.3, there);
    const double da = std::hypot(a.eye.x - here.position.x, a.eye.y - here.position.y);
    const double db = std::hypot(b.eye.x - there.position.x, b.eye.y - there.position.y);
    REQUIRE(da == Approx(db));
    REQUIRE(b.eye.z - there.position.z == Approx(a.eye.z - here.position.z));
}

TEST_CASE("Looping paths wrap and invalid paths are explained", "[director]")
{
    CameraPath p = orbit();
    p.loop = true;
    REQUIRE(sample(p, 6.5).eyeOffset.x == Approx(sample(p, 0.5).eyeOffset.x));

    CameraPath one;
    one.keys = {{0.0, at(1, 0, 0)}};
    REQUIRE_FALSE(validate(one).empty());

    CameraPath close = orbit();
    close.keys[1].atSeconds = 0.05;
    REQUIRE_FALSE(validate(close).empty());
    sortKeys(close);
    REQUIRE(validate(close).empty());
    REQUIRE(close.keys[1].atSeconds == Approx(0.1));

    CameraPath far = orbit();
    far.keys[0].shot.eyeOffset.x = 500.0;
    REQUIRE_FALSE(validate(far).empty());
}

TEST_CASE("Frame plans number every frame and cap long exports", "[director]")
{
    const FramePlan f = planFrames(orbit(), 30, "Club intro");
    REQUIRE(f.times.size() == 181); // 0 to 6 s inclusive
    REQUIRE(f.stems.front() == "Club_intro_00001");
    REQUIRE(f.stems.back() == "Club_intro_00181");
    REQUIRE(f.times.back() == Approx(6.0));
    REQUIRE_FALSE(f.capped);
    REQUIRE(planFrames(orbit(), 0, "x").times.empty());

    CameraPath longPath = orbit();
    longPath.keys.back().atSeconds = 3600.0;
    const FramePlan big = planFrames(longPath, 30, "long");
    REQUIRE(big.capped);
    REQUIRE(big.times.size() == static_cast<size_t>(kMaxExportFrames));
}

TEST_CASE("Productions keep angles, paths and shot lists", "[director]")
{
    Production p;
    p.name = "Store video";
    p.angles = {at(2, 0, 1.6)};
    p.angles[0].name = "Front";
    p.paths = {orbit()};
    p.shots = {{"Intro", "Front", "", 4.0, "smile", false}, {"Spin", "", "Orbit", 6.0, "", true},
               {"Missing", "Nope", "", 3.0, "", false}};
    REQUIRE(p.plannedSeconds() == Approx(13.0));
    REQUIRE(p.doneCount() == 1);
    const auto problems = checkShotList(p);
    REQUIRE(problems.size() == 1);
    REQUIRE(problems[0].find("\"Nope\"") != std::string::npos);

    const ReadResult r = readProduction(writeProduction(p));
    REQUIRE(r.production);
    REQUIRE(r.notes.empty());
    REQUIRE(r.production->paths.size() == 1);
    REQUIRE(r.production->paths[0].keys.size() == 4);
    REQUIRE(r.production->paths[0].keys[2].shot.fovDeg == Approx(40.0));
    REQUIRE(r.production->shots[1].done);
    REQUIRE(r.production->findAngle("Front"));
    REQUIRE(fileNameFor(p) == "Store_video.sw67director.json");
}

TEST_CASE("Production files drop far keys and refuse other formats", "[director]")
{
    REQUIRE_FALSE(readProduction("[]").production);
    REQUIRE_FALSE(readProduction(R"({"format":"sw67.director","version":2})").production);
    const auto r = readProduction(R"({"format":"sw67.director","version":1,"paths":[{"name":"p","keys":[
        {"at":0,"eye":[1,0,0],"focus":[0,0,1]},{"at":1,"eye":[900,0,0],"focus":[0,0,1]},
        {"at":-4,"eye":[1,0,0],"focus":[0,0,1]},{"at":2,"eye":[0,1,0],"focus":[0,0,1]}]}]})");
    REQUIRE(r.production);
    REQUIRE(r.production->paths[0].keys.size() == 2);
    REQUIRE(r.notes.size() == 2);
}
